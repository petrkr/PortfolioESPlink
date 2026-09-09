; drives.inc - DRIVES (0x87) command: how many logical drives DOS
; knows about (A=1, B=2, ...), so a client can offer A:..<Nth letter>
; without probing letters one by one.
;
; Command byte 0x87 continues the same command space as HELLO (0x80)
; and LIST extended (0x86) - see hello.inc's header comment for why
; this is safe. New command rather than folding into HELLO/LIST: this
; is conceptually unrelated to either (not a capability flag, not a
; directory listing).
;
; Request layout: payload[0]=0x87, nothing else needed (same as HELLO).
;
; Response layout (fixed 1 byte):
;   offset 0  drive count (number of logical drives DOS knows about,
;             1..N maps to A:..Nth letter)
;
; Why just a count, not a per-letter bitmask or media-type probe: this
; command exists specifically BECAUSE int 0x21 AH=0x36 (Get Disk Free
; Space) and AH=0x32 (Get Drive Parameter Block) were both found, on
; real Portfolio hardware, to do real I/O on a drive with no media
; inserted - printing "Insert disk in drive X:" (from Portfolio
; ROM/driver level, not DOS) and raising a critical error (int 0x24)
; that blocks on "Abort, Retry, Ignore?" without a handler installed.
; Even with a handler (AL=0 Ignore, the only value guaranteed on DOS
; 2.x - AL=3 "Fail" is DOS 3.0+ only), the returned error code after
; Ignore was NOT a consistent sentinel (0xFFFF on one drive, 0x0003 on
; another) - so per-letter existence can't be determined this way
; without accepting the "Insert disk" message as a side effect anyway.
;
; int 0x21 AH=0x0E (Select Default Drive) sidesteps this entirely: per
; Ralf Brown's Interrupt List it's a pure DOS-internal variable
; read/write (no media access at all, unlike AH=0x32 which RBIL notes
; DOES read the disk to build the DPB) and this was confirmed silent
; on real Portfolio hardware (dosbox-test iteration history:
; DRIVEDBG.asm in the repo root, now superseded by this file). AH=0x19
; (Get Current Default Drive) is called first so AH=0x0E can re-select
; the SAME drive (DL = that drive) instead of actually changing it -
; the standard trick to read the count as a side effect only.
;
; "1..count are all present, no gaps" is confirmed on real Portfolio
; hardware in two ways, and architecturally guaranteed (not just an
; observed coincidence) - see ROM_RESEARCH_NOTES.md's "Kontiguita
; 1..count je architektonicky garantovaná" section for the full
; reasoning: DOS 2.x/DIP DOS has no mechanism to leave a gap (ASSIGN
; only redirects an existing drive, SUBST/JOIN/LASTDRIVE= are DOS
; 3.0+/nonexistent on 2.x, and CONFIG.SYS DEVICE= drivers always claim
; the next free letter in boot order - confirmed by adding a 4th test
; drive, which landed on D: as expected, count going 3->4). A drive
; within that range might still have no media inserted right now -
; that's accepted: DRIVES only answers "does DOS know this letter at
; all", not "is it ready this instant" - the latter is discovered
; naturally by whatever command (LIST/upload) the client tries next,
; and an "Insert disk" message at that point is normal/expected, not a
; background-probe side effect to avoid.

DRIVES_CMD equ 0x87

drives_response: resb 1

; dispatch_drives: called from pftd_int61_handler with AL = payload[0]
; (no extra payload needed, same as dispatch_hello). Preserves
; AX/CX/DX/DS - see dispatch_hello's header comment for why (the
; caller's .chain forwards these straight to old61/the real ROM).
dispatch_drives:
        cmp     al, DRIVES_CMD
        jne     .not_drives

        push    ax
        push    cx
        push    dx
        push    ds

        push    cs
        pop     ds

        mov     ah, 0x19                ; Get Current Default Drive
        int     0x21
        mov     dl, al
        mov     ah, 0x0E                ; Select Default Drive - DL is
        int     0x21                    ; the drive we just read, so
                                         ; this doesn't actually change
                                         ; anything, only returns the
                                         ; count in AL as a side effect
        mov     [drives_response], al

        mov     dx, drives_response
        mov     cx, 1
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61

        pop     ds
        pop     dx
        pop     cx
        pop     ax
.not_drives:
        ret
