; PFTD.COM - PortfolioESPlink Portfolio Transfer Daemon, v1 (HELLO only).
;
; Installs a TSR hook on int 0x61 (the Portfolio's smart-cable API) so the
; ESP32 side can detect that this driver is present and query its protocol
; version/capabilities, in addition to the stock ROM File transfer Server
; commands (payload[0] in [2,6] - see ROM_RESEARCH_NOTES.md). See
; hello.inc for the command layout and response format.
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
; To answer a recognized command before the pending receive-block call
; returns, dispatch_hello (hello.inc) issues a REAL int 0x61 (AH=0x30
; AL=0, transmit) from inside our own hook. This re-enters
; pftd_int61_handler recursively, but AX=0x3000 (not 0x3001) so the
; recursive instance falls straight through to the chain - it never
; touches pending/saved_*. This works because the ROM's own receive loop
; waits forever for the next handshake byte - there is no timeout on its
; side, so delaying our return costs nothing (see ROM_RESEARCH_NOTES.md,
; tested on real hardware via HOOK3).
;
; int 0x21 (DOS API) must never be called from inside pftd_int61_handler -
; DOS is not re-entrant and this crashes (observed as garbage-filled
; screen). Install time code below is not subject to this - it never
; re-enters DOS.
;
; This logic is written so it can move into a PFTD.SYS device driver
; later without changes: everything below is self-contained around the
; int 0x61 vector and does not depend on how it was installed. Same
; product, not a separate tool - see pofo-driver/ directory-level intent.
;
; Usage:
;   PFTD                 <- install (stays resident)
;
; Assemble: nasm -f bin PFTD.asm -o PFTD.COM

CPU 8086
ORG 0x100

start:
        jmp     install

old61     dd 0
pending   db 0        ; 1 = a receive-block buffer is waiting to be inspected
saved_ds  dw 0
saved_dx  dw 0
payload0  db 0        ; captured payload[0] byte, read out safely below

%include "hello.inc"

; --- new int 0x61 handler ---
; CPU already pushed FLAGS, CS, IP of the caller. We NEVER call the
; original as a subroutine - always inspect-then-JMP.
pftd_int61_handler:
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

        call    dispatch_hello

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
%include "hexprint.inc"

install:
        mov     dx, msg_installing
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x3561
        int     0x21
        mov     [old61], bx
        mov     [old61+2], es

        push    ds
        mov     dx, pftd_int61_handler
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

msg_installing db 'PFTD v1 - installing...', 13, 10, '$'
msg_ok         db 'Resident. Build $'
