; rmdir.inc - RMDIR (0x8A) command: remove an EMPTY directory on the
; Portfolio (int 0x21 AH=0x3A). Separate command from delete.inc's DELETE
; (AH=0x41, files only) - the scope split was deliberate (see delete.inc's
; header), this is the follow-up half of that decision.
;
; Command byte 0x8A continues the 0x80+ PFTD command space - see hello.inc's
; header comment for why that space is safe.
;
; Request layout: payload[0]=0x8A, payload[1..2] unused/reserved (0x00,
; 0x70 - mirrors the ROM's own convention), payload[3..] ASCIIZ target path.
;
; Response layout (fixed 2 bytes), identical shape to mkdir.inc/delete.inc's:
;   offset 0  status  (0x20 = ok, 0x10 = error)
;   offset 1  errcode (0 on success; on failure: 1=not found, 4=access
;                       denied (also covers "not empty" - DOS 2.x has no
;                       separate code for this, same coarse-granularity
;                       situation as MKDIR's "already exists", confirmed
;                       on real hardware for AH=0x39 in
;                       ROM_RESEARCH_NOTES.md), 0xFF=critical error fired -
;                       see PROTOCOL.md's status/errcode convention
;                       section, shared with mkdir.inc/delete.inc.)
;
; Built on int 0x21 AH=0x3A (Remove Directory), DX=ASCIIZ path. Safe to
; call from inside a dispatcher for the same reason as mkdir.inc/delete.inc
; (dispatch runs while the ROM's own receive-block loop is blocked, so DOS
; is reentrant-safe here). Same int 0x24 dependency as mkdir.inc/
; delete.inc - see critical_error.inc for the full rationale; confirmed on
; real hardware that MKDIR (AH=0x39) on a drive with no media raises a
; critical error that hangs without the handler (see
; ROM_RESEARCH_NOTES.md's MKDIR/DELETE real-HW test results) - RMDIR does
; equivalent directory-entry disk I/O, so the same risk is assumed to
; apply, not separately re-verified.
;
; DOS 2.x/DIP DOS's actual extended error codes reachable from AH=0x3A are
; a best-effort guess pending real-hardware verification, same caveat as
; mkdir.inc/delete.inc - see PROTOCOL.md's "Adding a new command" checklist
; item 5.

RMDIR_CMD equ 0x8A

rmdir_response: resb 2    ; status(1B) + errcode(1B)
rmdir_path:     resb 80   ; local copy of the request's ASCIIZ path, same
                           ; pattern as mkdir_path/delete_path

; dispatch_rmdir: called from pftd_int61_handler with AL = payload[0] and
; CS=DS. Uses list_src_ds/list_src_si to pull the path out of the foreign
; buffer, same as dispatch_mkdir/dispatch_delete/dispatch_list.
dispatch_rmdir:
        cmp     al, RMDIR_CMD
        jne     .not_rmdir

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds

        push    cs
        pop     es
        mov     ax, [cs:list_src_ds]
        mov     ds, ax
        mov     si, [cs:list_src_si]
        add     si, 3
        mov     di, rmdir_path
.copy_path:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_path

        push    cs
        pop     ds              ; DS=CS from here on

        mov     byte [cs:critical_error_flag], 0

        mov     dx, rmdir_path
        mov     ah, 0x3A        ; Remove Directory
        int     0x21

        ; CF must be tested immediately after int 0x21, before anything
        ; else (even a CMP against critical_error_flag) clobbers it - see
        ; mkdir.inc's dispatch_mkdir for the real-hardware bug this
        ; ordering fixes (a prior version of this pattern checked
        ; critical_error_flag first via CMP, silently making the error
        ; path unreachable).
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
        mov     byte [rmdir_response], 0x20
        mov     byte [rmdir_response+1], 0
        jmp     .respond

.critical_fired:
        mov     byte [rmdir_response], 0x10
        mov     byte [rmdir_response+1], 0xFF
        jmp     .respond

.dos_error:
        ; AX = DOS extended error code. Mapping below is a best-effort
        ; guess for DOS 2.x/DIP DOS - VERIFY ON REAL HARDWARE (see header
        ; comment). "Not empty" is deliberately not distinguished from
        ; "access denied" (folds into errcode 4), same reasoning as
        ; MKDIR's "already exists" - see header comment.
        mov     byte [rmdir_response], 0x10
        cmp     ax, 3                    ; path not found
        je      .err_notfound
        cmp     ax, 5                    ; access denied (also covers "not
        je      .err_denied              ; empty")
        mov     byte [rmdir_response+1], 0xFF   ; unrecognized code
        jmp     .respond
.err_notfound:
        mov     byte [rmdir_response+1], 1
        jmp     .respond
.err_denied:
        mov     byte [rmdir_response+1], 4

.respond:
        mov     dx, rmdir_response
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
.not_rmdir:
        ret
