; hello.inc - HELLO (0x80) command: presence/capability discovery response
; and its dispatch inside the int 0x61 hook.
;
; Command space: this driver only ever reacts to payload[0] >= 0x80, a
; block far outside the ROM's own dispatch range ([2,6], with 4 unused),
; so there is no collision with anything the ROM itself understands now or
; could plausibly understand in a future revision.
;
;   0x80  HELLO     -> replies with the 12-byte hello block described below
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

HELLO_CMD equ 0x80

BUILD_ID  equ 0xFFFF0002       ; dev snapshot marker, see header comment above

hello_response:
        db      'PFD1'          ; magic
        dd      BUILD_ID        ; build id / dev snapshot marker
        db      1               ; version 1
        db      0x00            ; capabilities (none yet)
        db      0x00, 0x00      ; reserved
hello_response_len equ $ - hello_response

; dispatch_hello: called from pftd_int61_handler with AL = payload[0],
; CS=DS. Returns (ret) either way - caller must treat this as a normal
; subroutine call, never fall into it via %include placement, or the
; missing ret unbalances the stack on every HELLO hit. Answers over the
; smart cable using the same AH=0x30 API the ROM itself uses - see
; PFTD.asm header comment for why this is safe to do from inside the
; int 0x61 hook.
dispatch_hello:
        cmp     al, HELLO_CMD
        jne     .not_hello

        push    cs
        pop     ds
        mov     dx, hello_response
        mov     cx, hello_response_len
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61
        ; DL = error code on return (ignored here)
.not_hello:
        ret
