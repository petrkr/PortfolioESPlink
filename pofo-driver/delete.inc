; delete.inc - DELETE (0x89) command: delete a FILE on the Portfolio.
; Files only - int 0x21 AH=0x41 (Unlink), NOT AH=0x3A (RMDIR). Directory
; deletion is its own separate command - see rmdir.inc (0x8A).
;
; Command byte 0x89 continues the 0x80+ PFTD command space - see hello.inc's
; header comment for why that space is safe.
;
; Request layout: payload[0]=0x89, payload[1..2] unused/reserved (0x00,
; 0x70 - mirrors the ROM's own convention), payload[3..] ASCIIZ target path.
;
; Response layout (fixed 2 bytes), identical shape to mkdir.inc's:
;   offset 0  status  (0x20 = ok, 0x10 = error)
;   offset 1  errcode (0 on success; on failure: 1=not found, 3=disk full,
;                       4=access denied, 0xFF=critical error fired - see
;                       PROTOCOL.md's status/errcode convention section,
;                       shared with mkdir.inc. errcode 2 "already exists"
;                       is defined by that shared enum for MKDIR's use only
;                       and never produced here.)
;
; Built on int 0x21 AH=0x41 (Unlink), DX=ASCIIZ path. Safe to call from
; inside a dispatcher for the same reason as mkdir.inc/list.inc (dispatch
; runs while the ROM's own receive-block loop is blocked, so DOS is
; reentrant-safe here). Same int 0x24 dependency as mkdir.inc - see
; critical_error.inc and mkdir.inc's header for the full rationale; not
; repeated in detail here.
;
; DOS 2.x/DIP DOS's actual extended error codes reachable from AH=0x41 are
; a best-effort guess pending real-hardware verification, same caveat as
; mkdir.inc - see PROTOCOL.md's "Adding a new command" checklist item 5.

DELETE_CMD equ 0x89

delete_response: resb 2    ; status(1B) + errcode(1B)
delete_path:      resb 80  ; local copy of the request's ASCIIZ path, same
                            ; pattern as mkdir_path/list_pattern

; dispatch_delete: called from pftd_int61_handler with AL = payload[0] and
; CS=DS. Uses list_src_ds/list_src_si to pull the path out of the foreign
; buffer, same as dispatch_mkdir/dispatch_list.
dispatch_delete:
        cmp     al, DELETE_CMD
        jne     .not_delete

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
        mov     di, delete_path
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

        mov     dx, delete_path
        mov     ah, 0x41        ; Unlink (delete file)
        int     0x21

        ; CF must be tested immediately after int 0x21, before anything
        ; else (even a CMP against critical_error_flag) clobbers it - see
        ; mkdir.inc's dispatch_mkdir for the real-hardware bug this
        ; ordering fixes (DELETE on a nonexistent file incorrectly
        ; reported success the same way).
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
        mov     byte [delete_response], 0x20
        mov     byte [delete_response+1], 0
        jmp     .respond

.critical_fired:
        mov     byte [delete_response], 0x10
        mov     byte [delete_response+1], 0xFF
        jmp     .respond

.dos_error:
        ; AX = DOS extended error code. Mapping below is a best-effort
        ; guess for DOS 2.x/DIP DOS - VERIFY ON REAL HARDWARE (see header
        ; comment).
        mov     byte [delete_response], 0x10
        cmp     ax, 2                    ; file not found
        je      .err_notfound
        cmp     ax, 5                    ; access denied (read-only file,
        je      .err_denied              ; write-protected media)
        mov     byte [delete_response+1], 0xFF   ; unrecognized code
        jmp     .respond
.err_notfound:
        mov     byte [delete_response+1], 1
        jmp     .respond
.err_denied:
        mov     byte [delete_response+1], 4

.respond:
        mov     dx, delete_response
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
.not_delete:
        ret
