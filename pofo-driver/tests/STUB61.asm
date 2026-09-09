; STUB61.asm - DOSBox-only test helper, NEVER for real Portfolio hardware.
;
; Installs a minimal int 0x61 handler so that PFTD's .chain (jmp far
; [old61]) has somewhere safe to land when testing in DOSBox, instead of
; a NULL vector (which hangs/crashes - see PFTD.asm's residentcheck fix
; for the same class of bug). On real Portfolio hardware, the ROM always
; has its own int 0x61 handler already installed, so this stub is
; neither needed nor wanted there - it does not implement any real cable
; transport behaviour.
;
; On AX=0x3000 (AH=0x30 AL=0, transmit block) - the call PFTD's
; dispatch_hello issues to answer HELLO - this logs the DS:DX/CX bytes
; being "transmitted" as hex via int 0x10 AH=0x0E (BIOS teletype, safe to
; call re-entrantly from inside an interrupt handler; int 0x21 is NOT -
; see PFTD.asm header comment on DOS re-entrancy). This is how
; TESTHELLO's run makes hello_response's actual bytes visible instead of
; only checking them statically via xxd on the assembled PFTD.COM.
; Anything else just IRETs, same as a pure no-op.
;
; Usage in DOSBox: STUB61, then PFTDN (built with -dCHECK_POFO=0), then
; TESTHELLO.
;
; Assemble: nasm -f bin STUB61.asm -o STUB61.COM

CPU 8086
ORG 0x100

start:
        jmp     install

hexdig db '0123456789abcdef'

; tty: print AL via BIOS teletype. Preserves AX/BX/CX/DX.
tty:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     ah, 0x0E
        mov     bx, 0x0007
        int     0x10
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; print_hex8: print AL as 2 lowercase hex digits via tty. Preserves
; AX/BX/CX/DX.
print_hex8:
        push    ax
        push    bx

        mov     bl, al
        shr     al, 1
        shr     al, 1
        shr     al, 1
        shr     al, 1
        and     al, 0x0F
        push    bx
        xor     bx, bx
        mov     bl, al
        mov     al, [cs:hexdig+bx]
        call    tty
        pop     bx

        mov     al, bl
        and     al, 0x0F
        push    bx
        xor     bx, bx
        mov     bl, al
        mov     al, [cs:hexdig+bx]
        call    tty
        pop     bx

        pop     bx
        pop     ax
        ret

new61:
        cmp     ax, 0x3000
        jne     .passthrough

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    ds

        mov     al, 'T'
        call    tty
        mov     al, 'X'
        call    tty
        mov     al, '='
        call    tty

        mov     si, dx                  ; DS:SI walks the caller's buffer
        mov     bx, cx                  ; bx = byte count from caller
.byteloop:
        cmp     bx, 0
        je      .done
        mov     al, [si]
        call    print_hex8
        mov     al, ' '
        call    tty
        inc     si
        dec     bx
        jmp     .byteloop
.done:
        mov     al, 13
        call    tty
        mov     al, 10
        call    tty

        pop     ds
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax

.passthrough:
        iret

resident_end:

install:
        mov     dx, msg_installing
        mov     ah, 0x09
        int     0x21

        push    ds
        mov     dx, new61
        mov     ax, 0x2561
        int     0x21
        pop     ds

        mov     dx, resident_end
        add     dx, 0x0F
        mov     cl, 4
        shr     dx, cl
        mov     ax, 0x3100
        int     0x21

msg_installing db 'STUB61 (DOSBox test helper) resident - logs transmit bytes.', 13, 10, '$'
