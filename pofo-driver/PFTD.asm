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
; Assemble (real Portfolio):  nasm -f bin PFTD.asm -o PFTD.COM
; Assemble (DOSBox/testing):  nasm -f bin -dCHECK_POFO=0 PFTD.asm -o PFTD.COM
;   (skips the is_pofo hardware check - DOSBox's port 0x61 doesn't echo
;   back 0x61 like a real Portfolio does, so the check would always fail
;   there. Never ship a CHECK_POFO=0 build to real hardware.)

CPU 8086
ORG 0x100

; Set to 0 to skip the is_pofo hardware check at install time (see
; pofodetect.inc) - useful for testing in DOSBox or other emulators that
; don't echo port 0x61 back as 0x61. Leave at 1 for real builds.
%ifndef CHECK_POFO
CHECK_POFO equ 1
%endif

start:
        jmp     install

old61     dd 0
pending   db 0        ; 1 = a receive-block buffer is waiting to be inspected
saved_ds  dw 0
saved_dx  dw 0
payload0  db 0        ; captured payload[0] byte, read out safely below

%include "hello.inc"
%include "list.inc"
%include "drives.inc"
%include "mkdir.inc"
%include "delete.inc"
%include "critical_error.inc"
%include "residentcheck.inc"

; list_src_ds/list_src_si: copy of saved_ds/saved_dx taken at the same
; time payload0 is read, handed to dispatch_list so it can pull more
; than one byte (the ASCIIZ pattern) out of the foreign receive buffer -
; see list.inc's dispatch_list header comment for why HELLO doesn't need
; this but LIST does.
list_src_ds dw 0
list_src_si dw 0

; --- new int 0x61 handler ---
; CPU already pushed FLAGS, CS, IP of the caller. We NEVER call the
; original as a subroutine - always inspect-then-JMP.
pftd_int61_handler:
        ; Must run before anything is pushed: check_already_resident
        ; returns via RET (stack still just has FLAGS/CS/IP from the
        ; int 0x61 itself underneath), and if it answered the probe we
        ; IRET immediately, right here, before pushing anything else -
        ; that stack shape is exactly what IRET expects.
        call    check_already_resident
        cmp     ax, PROBE_ANSWER
        jne     .not_probe
        iret
.not_probe:

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

        ; stash the same DS:DX pair for dispatch_list, which (unlike
        ; dispatch_hello) needs more than just payload[0] out of the
        ; foreign buffer - see list.inc. This clobbers AX/AL, so AL
        ; (payload[0]) is reloaded from payload0 below before either
        ; dispatcher runs - both require AL = payload[0] on entry.
        mov     ax, [cs:saved_ds]
        mov     [cs:list_src_ds], ax
        mov     ax, [cs:saved_dx]
        mov     [cs:list_src_si], ax

        mov     al, [cs:payload0]
        call    dispatch_hello
        mov     al, [cs:payload0]
        call    dispatch_list
        mov     al, [cs:payload0]
        call    dispatch_drives
        mov     al, [cs:payload0]
        call    dispatch_mkdir
        mov     al, [cs:payload0]
        call    dispatch_delete

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
%include "pofodetect.inc"

install:
%if CHECK_POFO
        ; Refuse to install on anything that isn't a real Atari Portfolio
        ; - see pofodetect.inc for the port 0x61 probe this relies on.
        call    is_pofo
        je      .is_portfolio

        mov     dx, msg_not_portfolio
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4c01
        int     0x21
%endif

.is_portfolio:
        ; Refuse to double-install: ask any already-resident PFTD hook
        ; whether it's there (see residentcheck.inc). Only safe to try if
        ; int 0x61 actually points somewhere - on real Portfolio hardware
        ; the ROM always has its own int 0x61 handler installed, but a
        ; generic PC/DOS (or DOSBox) normally has a NULL vector there,
        ; and calling through a NULL vector hangs/crashes instead of
        ; harmlessly returning. So check the vector segment:offset isn't
        ; 0000:0000 first.
        mov     ax, 0x3561
        int     0x21
        mov     ax, es
        or      ax, bx
        jz      .not_resident           ; vector is 0000:0000 - nothing to ask

        mov     ax, PROBE_CMD
        int     0x61
        cmp     ax, PROBE_ANSWER
        jne     .not_resident

        mov     dx, msg_already_resident
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4c01
        int     0x21

.not_resident:
        ; Print "PFTD v" then VERSION as a decimal number - e.g. "PFTD v1"
        mov     dx, msg_pftd_v
        mov     ah, 0x09
        int     0x21

        mov     al, VERSION
        call    print_dec8

        ; Print " (" then BUILD_ID as 8 lowercase hex digits, e.g.
        ; " (ffff0005" - completes the banner to "PFTD v1 (ffff0005"
        mov     dx, msg_build_open
        mov     ah, 0x09
        int     0x21

        mov     dx, (BUILD_ID >> 16) & 0xFFFF
        mov     ax, BUILD_ID & 0xFFFF
        call    print_hex32

        ; Close the banner: ") - Installing...\r\n" -> full first line is
        ; "PFTD v1 (ffff0005) - Installing..."
        mov     dx, msg_installing
        mov     ah, 0x09
        int     0x21

        ; Read the current int 0x61 vector (DOS Get Interrupt Vector,
        ; AH=0x35) and stash it in old61 so pftd_int61_handler can chain
        ; to it later - this must happen before we install our own hook.
        mov     ax, 0x3561
        int     0x21
        mov     [old61], bx
        mov     [old61+2], es

        ; Point int 0x61 at pftd_int61_handler (DOS Set Interrupt Vector,
        ; AH=0x25). DS must be CS (the segment pftd_int61_handler lives
        ; in) while DX holds the offset - saved/restored around the call
        ; since DS is otherwise whatever DOS gave us at program start.
        push    ds
        mov     dx, pftd_int61_handler
        mov     ax, 0x2561
        int     0x21
        pop     ds

        ; Read and save the current int 0x24 vector (critical_error.inc),
        ; then install pftd_int24_handler - same AH=0x35/AH=0x25 DOS API
        ; pair used above for int 0x61, same DS=CS/DX=offset discipline.
        ; Installed once here, resident for the life of the TSR - see
        ; critical_error.inc's header for why (needed by mkdir.inc/
        ; delete.inc, both of which do real disk I/O and can raise int 0x24
        ; on a drive with no/write-protected media).
        mov     ax, 0x3524
        int     0x21
        mov     [old24], bx
        mov     [old24+2], es

        push    ds
        mov     dx, pftd_int24_handler
        mov     ax, 0x2524
        int     0x21
        pop     ds

        ; Vector is live - print "Installed" to confirm before we go
        ; resident (nothing after this point can print anything else).
        mov     dx, msg_installed
        mov     ah, 0x09
        int     0x21

        ; Terminate and Stay Resident (DOS AH=0x31): keep everything up to
        ; resident_end (code + hook state) allocated after this program
        ; exits, so pftd_int61_handler keeps working once the shell
        ; prompt returns. Size is in 16-byte paragraphs, rounded up.
        mov     dx, resident_end
        add     dx, 0x0F
        mov     cl, 4
        shr     dx, cl
        mov     ax, 0x3100
        int     0x21

msg_pftd_v        db 'PFTD v$'
msg_build_open    db ' ($'
msg_installing    db ') - Installing...', 13, 10, '$'
msg_installed     db 'Installed', 13, 10, '$'
msg_not_portfolio db 'This is not an Atari Portfolio.', 13, 10, '$'
msg_already_resident db 'PFTD is already resident.', 13, 10, '$'
