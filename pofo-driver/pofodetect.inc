; pofodetect.inc - check whether we're actually running on an Atari
; Portfolio, not a generic PC/DOS clone or emulator that doesn't implement
; Portfolio-specific hardware.
;
; Technique: read I/O port 0x61 and check that it echoes back 0x61. This
; is the same check used by RAM_TEST.COM (Klaus Peichl, 1998), a
; third-party Portfolio utility - found by disassembling its .COM binary
; (no source available). On real Portfolio hardware, port 0x61 reads back
; its own port number; on a generic PC (where port 0x61 is the PPI
; keyboard/speaker control port with unrelated semantics), this check
; fails.
;
; is_pofo: returns with ZF set (use "je") if running on a real Portfolio,
; ZF clear otherwise. Preserves AX.
is_pofo:
        push    ax
        in      al, 0x61
        cmp     al, 0x61
        pop     ax
        ret
