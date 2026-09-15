; hello.inc - HELLO (0x80) command: presence/capability discovery response
; and its dispatch inside the int 0x61 hook.
;
; Command space: this driver only ever reacts to payload[0] >= 0x80, a
; block far outside the ROM's own dispatch range ([2,6], with 4 unused),
; so there is no collision with anything the ROM itself understands now or
; could plausibly understand in a future revision.
;
;   0x80  HELLO     -> replies with the 12-byte hello block described below
;   0x86  LIST_EXT  -> see list.inc
;   0x87  DRIVES    -> see drives.inc
;   0x88  MKDIR     -> see mkdir.inc
;   0x89  DELETE    -> see delete.inc
;   0x8A  RMDIR     -> see rmdir.inc
;   0x8B  RENAME    -> see rename.inc
;   0x81-0x85, 0x8C+ (reserved for future subcommands)
;
; HELLO response layout (fixed 12 bytes):
;   offset 0-3  magic "PFD1"
;   offset 4-7  build id (4 bytes, binary). VERSION/BUILD_ID come from
;               version.inc - see that file for how the build id is
;               assigned before vs. after this driver is committed.
;   offset 8    version   (1 = this format; 0xFF = extended - offset 9 is
;                          then an extended version number and everything
;                          from there on is redefined per that version,
;                          capabilities included)
;   offset 9    capabilities bitmask - see CAP_* constants below
;   offset 10-11 reserved (0x00, 0x00)
;
; Capabilities bitmask (offset 9): defined here (not in the .inc file of
; the command each bit describes) since this is the one place a client
; can discover them all at once, before knowing which other commands
; exist.
;   bit 0  CAP_LIST_EXT - LIST extended (0x86, see list.inc) supported
;   bit 1  CAP_DRIVES   - DRIVES (0x87, see drives.inc) supported
;   bit 2  CAP_MKDIR    - MKDIR (0x88, see mkdir.inc) supported
;   bit 3  CAP_DELETE   - DELETE (0x89, see delete.inc) supported
;   bit 4  CAP_RMDIR    - RMDIR (0x8A, see rmdir.inc) supported
;   bit 5  CAP_RENAME   - RENAME (0x8B, see rename.inc) supported

%include "version.inc"

HELLO_CMD equ 0x80
CAP_LIST_EXT equ 0x01
CAP_DRIVES   equ 0x02
CAP_MKDIR    equ 0x04
CAP_DELETE   equ 0x08
CAP_RMDIR    equ 0x10
CAP_RENAME   equ 0x20

hello_response:
        db      'PFD1'          ; magic
        dd      BUILD_ID        ; build id / dev snapshot marker
        db      VERSION
        db      CAP_LIST_EXT | CAP_DRIVES | CAP_MKDIR | CAP_DELETE | CAP_RMDIR | CAP_RENAME ; capabilities - see list.inc/drives.inc/mkdir.inc/delete.inc/rmdir.inc/rename.inc
        db      0x00, 0x00      ; reserved
hello_response_len equ $ - hello_response

; dispatch_hello: called from pftd_int61_handler with AL = payload[0],
; CS=DS. Returns (ret) either way - caller must treat this as a normal
; subroutine call, never fall into it via %include placement, or the
; missing ret unbalances the stack on every HELLO hit. Preserves
; AX/CX/DX/DS - pftd_int61_handler's .chain forwards whatever's in those
; registers straight to old61 (the real ROM on real hardware), so this
; must not leak the transmit call's own DX=hello_response/CX=
; hello_response_len into that forwarded call. (Found via TESTHELLO in
; DOSBox: a stray second "transmit" appeared downstream, made of leftover
; register bytes from here rather than a real second call - see
; dosbox-test/ - the caller wasn't clobbering CX/DX either, so both sides
; needed the same discipline.) Answers over the smart cable using the
; same AH=0x30 API the ROM itself uses - see PFTD.asm header comment for
; why this is safe to do from inside the int 0x61 hook.
dispatch_hello:
        cmp     al, HELLO_CMD
        jne     .not_hello

        push    ax
        push    cx
        push    dx
        push    ds

        push    cs
        pop     ds
        mov     dx, hello_response
        mov     cx, hello_response_len
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61
        ; DL = error code on return (ignored here)

        pop     ds
        pop     dx
        pop     cx
        pop     ax
.not_hello:
        ret
