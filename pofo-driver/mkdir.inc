; mkdir.inc - MKDIR (0x88) command: create a directory on the Portfolio,
; the first PFTD command that performs real disk I/O from inside the int
; 0x61 hook (unlike LIST's DTA-only Find First/Next or DRIVES' I/O-free
; AH=0x0E/AH=0x19 pair) and therefore the first that can genuinely fail
; (bad path, already exists, disk full, no media, write-protected).
;
; Command byte 0x88 continues the 0x80+ PFTD command space - see hello.inc's
; header comment for why that space is safe.
;
; Request layout: payload[0]=0x88, payload[1..2] unused/reserved (0x00,
; 0x70 - mirrors the ROM's own convention, same as LIST extended's request),
; payload[3..] ASCIIZ target path.
;
; Response layout (fixed 2 bytes):
;   offset 0  status  (0x20 = ok, 0x10 = error - reusing the ROM commands'
;                       own convention, see PROTOCOL.md)
;   offset 1  errcode (0 on success; on failure: 1=not found, 2=already
;                       exists, 3=disk full, 4=access denied, 0xFF=critical
;                       error fired (AX not trustworthy) - see PROTOCOL.md's
;                       status/errcode convention section for the full,
;                       single-source-of-truth definition of this enum,
;                       shared with delete.inc)
;
; Built on int 0x21 AH=0x39 (Create Directory), DX=ASCIIZ path. Safe to call
; from inside a dispatcher for the same reason LIST's Find First/Next calls
; are (see list.inc's header): dispatch runs while the ROM's own
; receive-block loop is blocked waiting for the next handshake byte, so DOS
; is reentrant-safe from DOS's own point of view during this window.
;
; A resident int 0x24 (critical error) handler (critical_error.inc) MUST be
; installed before this dispatcher can safely run - AH=0x39 on a drive with
; no media/write-protected media/etc. raises int 0x24, which blocks on
; "Abort, Retry, Ignore?" without a handler. critical_error_flag is cleared
; immediately before the AH=0x39 call and checked immediately after; if set,
; AX is not trusted (see critical_error.inc's header for why) and errcode is
; unconditionally 0xFF.
;
; DOS 2.x/DIP DOS's actual extended error codes reachable from AH=0x39 are
; not certain from documentation alone - Ralf Brown's Interrupt List is
; written primarily for PC MS-DOS 3.0+, and ROM_RESEARCH_NOTES.md's DRIVES
; findings already showed DIP DOS diverging from RBIL's documented contract
; on other calls (AH=0x32, AH=0x0E). The mapping table below (.dos_error) is
; a best-effort guess pending real-hardware verification - see PROTOCOL.md's
; "Adding a new command" checklist item 5.
;
; DS discipline: same as list.inc - DS is switched to the foreign segment
; only for the initial path copy; every DOS call after that, and all writes
; into mkdir_response, run with DS=CS.

MKDIR_CMD equ 0x88

mkdir_response: resb 2    ; status(1B) + errcode(1B)
mkdir_path:     resb 80   ; local copy of the request's ASCIIZ path, copied
                           ; out of the foreign buffer before DS is switched
                           ; back - same pattern as list.inc's list_pattern

; dispatch_mkdir: called from pftd_int61_handler with AL = payload[0] and
; CS=DS. Needs more than payload[0] out of the foreign receive buffer (the
; ASCIIZ path), via list_src_ds/list_src_si (set generically by
; pftd_int61_handler, not LIST-specific despite the name - see list.inc's
; dispatch_list header comment).
dispatch_mkdir:
        cmp     al, MKDIR_CMD
        jne     .not_mkdir

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds

        ; Copy the ASCIIZ path out of the foreign buffer
        ; (list_src_ds:list_src_si+3, skipping the 3-byte header) into
        ; mkdir_path - identical pattern to list.inc's .copy_pattern loop.
        push    cs
        pop     es
        mov     ax, [cs:list_src_ds]
        mov     ds, ax
        mov     si, [cs:list_src_si]
        add     si, 3
        mov     di, mkdir_path
.copy_path:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_path

        push    cs
        pop     ds              ; DS=CS from here on - nothing below this
                                 ; line ever uses the foreign segment again

        mov     byte [cs:critical_error_flag], 0

        mov     dx, mkdir_path
        mov     ah, 0x39        ; Create Directory
        int     0x21

        ; CF must be tested immediately after int 0x21, before anything
        ; else (even a CMP against critical_error_flag) clobbers it - a
        ; prior version of this dispatcher checked critical_error_flag
        ; first via CMP, which overwrites CF with the result of THAT
        ; comparison and made the AH=0x39 error path unreachable (verified
        ; on real hardware: MKDIR on an already-existing directory
        ; incorrectly reported success). jc/jnc here stash the real
        ; int 0x21 outcome before anything else touches flags.
        jc      .int21_failed
        jmp     .int21_ok
.int21_failed:
        cmp     byte [cs:critical_error_flag], 0
        jne     .critical_fired
        jmp     .dos_error
.int21_ok:
        cmp     byte [cs:critical_error_flag], 0
        jne     .critical_fired

        ; success
        mov     byte [mkdir_response], 0x20
        mov     byte [mkdir_response+1], 0
        jmp     .respond

.critical_fired:
        mov     byte [mkdir_response], 0x10
        mov     byte [mkdir_response+1], 0xFF
        jmp     .respond

.dos_error:
        ; AX = DOS extended error code. Mapping below is a best-effort
        ; guess for DOS 2.x/DIP DOS - VERIFY ON REAL HARDWARE (see header
        ; comment). errcode 2 ("already exists") is deliberately not
        ; distinguished here: DOS 2.x's AH=0x39 is expected to return the
        ; same coarse "access denied" (5) for an existing path as for
        ; write-protected/full-disk cases, so it folds into errcode 4
        ; unless real-hardware testing shows a distinct code.
        mov     byte [mkdir_response], 0x10
        cmp     ax, 3                    ; path not found
        je      .err_notfound
        cmp     ax, 5                    ; access denied (catch-all, likely
        je      .err_denied              ; also covers "already exists")
        cmp     ax, 8                    ; insufficient memory / disk full
        je      .err_full
        mov     byte [mkdir_response+1], 0xFF   ; unrecognized code
        jmp     .respond
.err_notfound:
        mov     byte [mkdir_response+1], 1
        jmp     .respond
.err_denied:
        mov     byte [mkdir_response+1], 4
        jmp     .respond
.err_full:
        mov     byte [mkdir_response+1], 3

.respond:
        mov     dx, mkdir_response
        mov     cx, 2
        mov     ax, 0x3000      ; AH=0x30 AL=0 = transmit block
        int     0x61

        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
.not_mkdir:
        ret
