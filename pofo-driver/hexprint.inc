; hexprint.inc - print a 16/32-bit value as lowercase hex via DOS char
; output (int 0x21 AH=0x02). Install-time only - never call these from
; inside the int 0x61 hook (see PFTD.asm header comment on int 0x21
; re-entrancy).
;
; Written using only 8086-level instructions: CPU 8086 rejects "rol reg,
; imm8" (that form is 286+) and there is no 32-bit register to rotate, so
; a nibble is extracted via four single-bit rol's and DX:AX (high:low
; word) stands in for a 32-bit value.

hexdig db '0123456789abcdef'

; print_hex16: print BX as 4 lowercase hex digits. Preserves AX/BX/CX/DX.
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

; print_dec8: print AL (0-255) as decimal, no leading zeros (so "1" stays
; "1", not "001" - unlike print_hex16/32 this is meant for small
; human-facing numbers like VERSION, not a fixed-width machine value).
; Preserves AX/BX/CX/DX.
print_dec8:
        push    ax
        push    bx
        push    cx
        push    dx

        xor     ah, ah          ; ax = 0..255
        mov     bx, 10
        xor     cx, cx          ; cx = digit count pushed so far

.divide:
        xor     dx, dx          ; dx:ax is the dividend - dx must be 0,
                                 ; ax alone is <= 255 so this is safe
        div     bx              ; ax = ax/10, dx = ax%10
        push    dx              ; remainder = next digit, least significant
                                 ; first
        inc     cx
        cmp     ax, 0
        jne     .divide

.print:
        pop     dx
        add     dl, '0'
        mov     ah, 0x02
        int     0x21
        loop    .print

        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
