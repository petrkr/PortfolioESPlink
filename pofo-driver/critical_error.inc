; critical_error.inc - resident int 0x24 (DOS critical error) handler.
;
; Needed because MKDIR (mkdir.inc, AH=0x39) and DELETE (delete.inc, AH=0x41)
; are the first PFTD commands that do real disk I/O from inside the int 0x61
; hook - unlike DRIVES (AH=0x0E/AH=0x19, both confirmed I/O-free on real
; Portfolio hardware) or LIST's Find First/Next against our own DTA. Hitting
; a drive with no media, write-protected media, or a full disk during
; AH=0x39/AH=0x41 is expected/routine, not a rare edge case - see
; ROM_RESEARCH_NOTES.md's "DIP DOS critical error chovani na jednotkach bez
; media" section (found during DRIVES development): such a call prints
; "Insert disk in drive X:" from Portfolio ROM/driver level, then raises
; int 0x24, which blocks on "Abort, Retry, Ignore?" waiting for a keypress
; if no handler is installed.
;
; RBIL int 0x24 contract: entry AH=error code, AL=drive (if disk error),
; BP:SI=device driver header, DI=error code detail; return AL=0/1/2/3 for
; Ignore/Retry/Fail/Abort, via IRET (a real interrupt entry - unlike the
; dispatch_* routines in hello.inc/list.inc/drives.inc/mkdir.inc/delete.inc,
; which are CALLed subroutines that RET).
;
; AL=0 (Ignore) unconditionally, no branching on AH, no chaining to the old
; vector: AL=3 (Fail) is DOS 3.0+ only and undefined on DOS 2.x/DIP DOS
; (same reasoning already established in drives.inc's header), so Ignore is
; the only value guaranteed safe here. Chaining to the old handler is
; deliberately not done - the default DOS 2.x critical-error handler is
; exactly the "Abort, Retry, Ignore?" prompt this handler exists to avoid,
; so there is nothing useful to chain to. Not branching on AH (error class)
; is safe because this handler is only ever live during MKDIR/DELETE's own
; int 0x21 window in practice - nothing else in this TSR calls DOS in a way
; that could raise int 0x24 (see list.inc's header for why int 0x21 is safe
; to call from inside dispatch at all).
;
; Even with Ignore, AX after the failing int 0x21 call is NOT a consistent
; sentinel across drives (observed 0x0003 on one drive, 0xFFFF on another,
; per ROM_RESEARCH_NOTES.md) - that is why mkdir.inc/delete.inc do not trust
; AX when this handler has fired. Instead they clear critical_error_flag
; before each attempt and check it after: if set, the response's errcode is
; 0xFF (unknown/critical error fired) regardless of whatever ended up in AX.
;
; Installed once at TSR install time (PFTD.asm's install: section, next to
; the int 0x61 AH=0x25 vector set), resident for the life of the TSR - not
; installed/restored per MKDIR/DELETE call, since install: already never
; re-enters DOS (see PFTD.asm's header) and the handler must be in place
; before any MKDIR/DELETE dispatch can safely run.

critical_error_flag: resb 1   ; set by pftd_int24_handler when it fires,
                               ; cleared by mkdir.inc/delete.inc before each
                               ; attempt
old24: resd 1                 ; saved original int 0x24 vector (AH=0x35 at
                               ; install time) - kept for symmetry with
                               ; old61, never chained to (see header above)

; pftd_int24_handler: real interrupt entry point (installed via DOS
; AH=0x25 INT=0x24), not a CALLed subroutine - must IRET, never RET.
pftd_int24_handler:
        push    ds
        push    cs
        pop     ds
        mov     byte [cs:critical_error_flag], 1
        pop     ds
        mov     al, 0           ; AL=0 = Ignore, the only value guaranteed
                                 ; safe on DOS 2.x/DIP DOS
        iret
