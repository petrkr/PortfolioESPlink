; TMKDIRA.asm - standalone one-shot test, NOT part of PFTD. Calls
; int 0x21 AH=0x39 (Create Directory) directly on drive A:, with NO
; int 0x24 handler installed by this test (whatever DOS's own default is
; on this machine) and NO PFTD/int 0x61 involvement at all.
;
; Purpose: same isolation approach as TUNLKDIR.asm, this time checking
; whether MKDIR on a drive with no media inserted (A: card slot empty)
; actually hangs on "Abort, Retry, Ignore?" without a handler, or whether
; it behaves like the ROM's own LIST (which prints "Insert disk" but does
; NOT block) - see ROM_RESEARCH_NOTES.md's DIP DOS critical-error section,
; which documented AH=0x36 blocking on real hardware but never tested
; AH=0x39/AH=0x41 directly, only inferred the same risk by analogy.
;
; This test intentionally does NOT protect against the prompt - if DOS's
; default int 0x24 handler does show "Abort, Retry, Ignore?" and wait for
; a keypress, that IS the result: press any key and see what AX/CF end up
; being printed afterward.
;
; Usage on real Portfolio: remove any card from A: first, then run
; TMKDIRA. (Also works, less interestingly, in DOSBox, though DOSBox
; won't raise a critical error the way DIP DOS does - see
; ROM_RESEARCH_NOTES.md.)
;
; Assemble: nasm -f bin TMKDIRA.asm -o TMKDIRA.COM

CPU 8086
ORG 0x100

start:
        mov     dx, msg_before
        mov     ah, 0x09
        int     0x21

        mov     dx, target_path
        mov     ah, 0x39        ; Create Directory
        int     0x21

        ; Stash CF and AX immediately - nothing between int 0x21 and here
        ; touches flags/AX.
        pushf
        mov     [result_ax], ax

        mov     dx, msg_after
        mov     ah, 0x09
        int     0x21

        mov     dx, msg_ax
        mov     ah, 0x09
        int     0x21
        mov     ax, [result_ax]
        call    print_hex16

        popf
        jc      .was_error
        mov     dx, msg_cf_clear
        mov     ah, 0x09
        int     0x21
        jmp     .done
.was_error:
        mov     dx, msg_cf_set
        mov     ah, 0x09
        int     0x21

.done:
        mov     dx, msg_newline
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4c00
        int     0x21

; print_hex16: prints AX as 4 uppercase hex digits, DOS AH=0x02. 8086-safe.
print_hex16:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     bx, 4          ; digits remaining
.next_digit:
        mov     cl, 4
        rol     ax, cl
        push    ax
        and     al, 0x0F
        cmp     al, 10
        jl      .digit
        add     al, 'A' - 10
        jmp     .print
.digit:
        add     al, '0'
.print:
        mov     dl, al
        mov     ah, 0x02
        int     0x21
        pop     ax
        dec     bx
        jnz     .next_digit
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

msg_before   db 'Calling int 0x21 AH=0x39 (Mkdir) on A:\TESTDIR directly, no int 0x24 handler installed by this test.', 13, 10
             db 'If DOS shows Abort/Retry/Ignore and waits, that IS the result - press a key to let it continue.', 13, 10, '$'
msg_after    db 13, 10, 'int 0x21 returned.', 13, 10, '$'
msg_ax       db 'AX=$'
msg_cf_set   db ' CF=1 (error)', '$'
msg_cf_clear db ' CF=0 (no error)', '$'
msg_newline  db 13, 10, '$'

target_path  db 'A:\TESTDIR', 0
result_ax    dw 0
