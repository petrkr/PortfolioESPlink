; PFDAEMON.COM - PortfolioESPlink companion driver, v1 (PING only).
;
; Installs a TSR hook on int 0x61 (the Portfolio's smart-cable API) so the
; ESP32 side can detect that this driver is present and query its protocol
; version/capabilities, in addition to the stock ROM File transfer Server
; commands (payload[0] in [2,6] - see ROM_RESEARCH_NOTES.md).
;
; Command space: this driver only ever reacts to payload[0] >= 0x80, a
; block far outside the ROM's own dispatch range ([2,6], with 4 unused),
; so there is no collision with anything the ROM itself understands now or
; could plausibly understand in a future revision.
;
;   0x80  HELLO     -> replies with the 8-byte hello block described below
;   0x81+ (reserved for future subcommands - mkdir/delete/free-space/list-v2)
;
; HELLO response layout (fixed 12 bytes):
;   offset 0-3  magic "PFD1"
;   offset 4-7  build id (4 bytes, binary). Until this driver is actually
;               committed, this is a manually-assigned dev snapshot marker
;               in the FFFF00xx range (FFFF0001, FFFF0002, ...) so it can't
;               be mistaken for a real git short-hash. Once committed, this
;               becomes the 4-byte binary form of the git short hash.
;   offset 8    version   (1 = this format; 0xFF = extended - offset 9 is
;                          then an extended version number and everything
;                          from there on is redefined per that version,
;                          capabilities included)
;   offset 9    capabilities bitmask (0x00 in v1 - nothing beyond HELLO yet)
;   offset 10-11 reserved (0x00, 0x00)
;
; Detection mechanism (identical to the one proven working in HOOK3.asm):
; DOS is single-tasking, so if int 0x61 fires a SECOND time after we saw an
; AH=0x30 AL=1 (receive block) call, the first call must have already
; completed. On AL=1 we remember DS:DX (the buffer) and set a flag; on the
; very next int 0x61 of any kind, we check that flag and peek at the
; remembered buffer for payload[0].
;
; We NEVER call the original int 0x61 handler as a subroutine (it ends in
; "retf word 0x2", not "iret" - a naive CALL/return-here breaks the stack).
; Always inspect-then-JMP far to the original vector.
;
; To answer HELLO before the pending receive-block call returns, we issue a
; REAL int 0x61 (AH=0x30 AL=0, transmit) from inside our own hook. This
; re-enters new61 recursively, but AX=0x3000 (not 0x3001) so the recursive
; instance falls straight through to the chain - it never touches
; pending/saved_*. This works because the ROM's own receive loop waits
; forever for the next handshake byte - there is no timeout on its side,
; so delaying our return costs nothing (see ROM_RESEARCH_NOTES.md, tested
; on real hardware via HOOK3).
;
; This logic is written so it can move into a PFDAEMON.SYS device driver
; later without changes: everything below is self-contained around the
; int 0x61 vector and does not depend on how it was installed.
;
; Usage:
;   PFDAEMON             <- install (stays resident)
;
; Assemble: nasm -f bin PFDAEMON.asm -o PFDAEMON.COM

CPU 8086
ORG 0x100

start:
        jmp     install

old61     dd 0
pending   db 0        ; 1 = a receive-block buffer is waiting to be inspected
saved_ds  dw 0
saved_dx  dw 0
payload0  db 0        ; captured payload[0] byte, read out safely below

HELLO_CMD equ 0x80

BUILD_ID  equ 0xFFFF0001       ; dev snapshot marker, see header comment above

hello_response:
        db      'PFD1'          ; magic
        dd      BUILD_ID        ; build id / dev snapshot marker
        db      1               ; version 1
        db      0x00            ; capabilities (none yet)
        db      0x00, 0x00      ; reserved
hello_response_len equ $ - hello_response

; --- new int 0x61 handler ---
; CPU already pushed FLAGS, CS, IP of the caller. We NEVER call the
; original as a subroutine - always inspect-then-JMP.
new61:
        push    ax
        push    bx
        push    si
        push    ds

        cmp     byte [cs:pending], 0
        je      .no_pending
        mov     byte [cs:pending], 0    ; consume the flag either way

        ; read payload[0] from the foreign buffer, DS temporarily switched
        mov     ax, [cs:saved_ds]
        mov     ds, ax
        mov     si, [cs:saved_dx]
        mov     al, [si]
        push    cs
        pop     ds                      ; DS=CS again immediately, nothing
                                         ; below this line ever uses the
                                         ; foreign segment again
        mov     [cs:payload0], al

        cmp     al, HELLO_CMD
        jne     .no_pending             ; not our command, nothing to do

        ; ANSWER: transmit hello_response back over the smart cable, using
        ; the same AH=0x30 API the ROM itself uses. DS:DX must point at
        ; the buffer, CX = byte count. This blocks (waits for 'Z' from the
        ; ESP32 side) but that's fine - the ROM's own receive loop is
        ; waiting patiently for us to return, with no timeout of its own.
        push    cs
        pop     ds
        mov     dx, hello_response
        mov     cx, hello_response_len
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61
        ; DL = error code on return (ignored here)

.no_pending:
        pop     ds
        pop     si
        pop     bx
        pop     ax

        cmp     ax, 0x3001
        jne     .chain

        mov     [cs:saved_ds], ds
        mov     [cs:saved_dx], dx
        mov     byte [cs:pending], 1

.chain:
        jmp     far [cs:old61]

resident_end:

; ---- installer ----
; print_hex16: print BX as 4 uppercase hex digits via int 0x21 AH=0x02
; (DOS char output - safe here, this runs at install time, not inside the
; int 0x61 hook). Preserves AX/BX/CX/DX.
print_hex16:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     cx, 4
.digit:
        rol     bx, 1
        rol     bx, 1
        rol     bx, 1
        rol     bx, 1
        mov     al, bl
        and     al, 0x0F
        xor     ah, ah
        push    bx
        mov     bx, ax
        mov     dl, [cs:hexdig+bx]
        pop     bx
        mov     ah, 0x02
        int     0x21
        loop    .digit
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; print_hex32: print DX:AX (DX = high word, AX = low word) via print_hex16
; twice. Preserves AX/BX/CX/DX.
print_hex32:
        push    ax
        push    bx
        push    dx
        mov     bx, dx
        call    print_hex16
        pop     dx
        pop     bx
        pop     ax
        push    dx
        mov     bx, ax
        call    print_hex16
        pop     dx
        ret

hexdig db '0123456789ABCDEF'

install:
        mov     dx, msg_installing
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x3561
        int     0x21
        mov     [old61], bx
        mov     [old61+2], es

        push    ds
        mov     dx, new61
        mov     ax, 0x2561
        int     0x21
        pop     ds

        mov     dx, msg_ok
        mov     ah, 0x09
        int     0x21

        mov     dx, (BUILD_ID >> 16) & 0xFFFF
        mov     ax, BUILD_ID & 0xFFFF
        call    print_hex32

        mov     dl, 13
        mov     ah, 0x02
        int     0x21
        mov     dl, 10
        mov     ah, 0x02
        int     0x21

        mov     dx, resident_end
        add     dx, 0x0F
        mov     cl, 4
        shr     dx, cl
        mov     ax, 0x3100
        int     0x21

msg_installing db 'PFDAEMON v1 - installing...', 13, 10, '$'
msg_ok         db 'Resident. Build $'
