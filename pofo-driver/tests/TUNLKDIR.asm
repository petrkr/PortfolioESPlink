; TUNLKDIR.asm - standalone one-shot test, NOT part of PFTD. Calls
; int 0x21 AH=0x41 (Unlink) directly on a hardcoded directory path, with
; NO int 0x24 handler installed (DOS's own default one, whatever that is
; on this machine) and NO PFTD/int 0x61 involvement at all.
;
; Purpose: dispatch_delete (delete.inc) previously reported
; status=0x10/errcode=0xFF ("critical error fired") when DELETE was called
; on a directory instead of a file - but that observation was made WITH
; critical_error.inc's handler installed (Ignore-and-continue). Whether
; AH=0x41 on a directory would otherwise have hung on "Abort, Retry,
; Ignore?" without any handler was never actually observed - this test
; checks that directly, in isolation, so the claim in ROM_RESEARCH_NOTES.md
; is either confirmed or corrected with a real observation instead of an
; assumption.
;
; This test intentionally does NOT protect against the prompt - if DOS's
; default int 0x24 handler does show "Abort, Retry, Ignore?" and wait for
; a keypress, that IS the result: press any key (Retry/Ignore should both
; let it continue) and see what AX/CF end up being printed afterward.
;
; Target path is hardcoded to C:\TESTDIR2 - the same leftover empty
; directory from the MKDIR real-hardware test matrix (see
; ROM_RESEARCH_NOTES.md). Edit target_path below before assembling if a
; different path is needed.
;
; Usage on real Portfolio (or DOSBox, though DOSBox won't raise a critical
; error the way DIP DOS does - see ROM_RESEARCH_NOTES.md):
;   TUNLKDIR
;
; Assemble: nasm -f bin TUNLKDIR.asm -o TUNLKDIR.COM

CPU 8086
ORG 0x100

start:
        mov     dx, msg_before
        mov     ah, 0x09
        int     0x21

        mov     dx, target_path
        mov     ah, 0x41        ; Unlink
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

; print_hex16: prints AX as 4 uppercase hex digits, DOS AH=0x02. 8086-safe
; (no ROL reg,imm/CL-count-4 in one shot needed - rotates 4 bits at a time
; via CL, BX holds the remaining-digit counter since CL/CX are in use by
; the rotate itself).
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

msg_before   db 'Calling int 0x21 AH=0x41 (Unlink) on target_path directly, no int 0x24 handler installed by this test.', 13, 10
             db 'If DOS shows Abort/Retry/Ignore and waits, that IS the result - press a key to let it continue.', 13, 10, '$'
msg_after    db 13, 10, 'int 0x21 returned.', 13, 10, '$'
msg_ax       db 'AX=$'
msg_cf_set   db ' CF=1 (error)', '$'
msg_cf_clear db ' CF=0 (no error)', '$'
msg_newline  db 13, 10, '$'

target_path  db 'C:\TESTDIR2', 0
result_ax    dw 0
