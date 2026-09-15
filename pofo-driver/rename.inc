; rename.inc - RENAME (0x8B) command: rename/move a file or directory on
; the Portfolio (int 0x21 AH=0x56, Rename File). DOS's rename is a
; directory-entry rewrite, not a data copy - it also works as a "move"
; within the same drive (changing the path component without changing the
; name), but NOT across drives (C: -> D:) - that would need a separate
; copy+delete sequence at the PFTD/ESP32 level, out of scope here.
;
; Command byte 0x8B continues the 0x80+ PFTD command space - see hello.inc's
; header comment for why that space is safe.
;
; Request layout: payload[0]=0x8B, payload[1..2] unused/reserved (0x00,
; 0x70 - mirrors the ROM's own convention), payload[3..] TWO consecutive
; ASCIIZ strings: old path, then new path (each NUL-terminated, new path
; immediately follows old path's NUL). Unlike mkdir.inc/delete.inc/
; rmdir.inc, which only need one path, AH=0x56 needs two (DS:DX=old,
; ES:DI=new) - both copied into local buffers before DS/ES are repointed
; at CS, same reasoning as the single-path commands (list.inc's DS
; discipline comment).
;
; Response layout (fixed 2 bytes), identical shape to mkdir.inc/delete.inc/
; rmdir.inc's:
;   offset 0  status  (0x20 = ok, 0x10 = error)
;   offset 1  errcode (0 on success; on failure: 1=not found, 4=access
;                       denied (also covers "destination already exists" -
;                       DOS 2.x's AH=0x56 has no separate code for this on
;                       related calls per this project's established
;                       findings, unverified for AH=0x56 specifically),
;                       0xFF=critical error fired - see PROTOCOL.md's
;                       response status/errcode convention section, shared
;                       with mkdir.inc/delete.inc/rmdir.inc.)
;
; Built on int 0x21 AH=0x56, DS:DX=old ASCIIZ path, ES:DI=new ASCIIZ path.
; Safe to call from inside a dispatcher for the same reason as
; mkdir.inc/delete.inc/rmdir.inc (dispatch runs while the ROM's own
; receive-block loop is blocked, so DOS is reentrant-safe here). Same
; int 0x24 dependency - see critical_error.inc; not separately verified for
; AH=0x56 on real hardware (only AH=0x39/AH=0x41/AH=0x3A have been - see
; ROM_RESEARCH_NOTES.md), assumed by analogy since this also does real
; directory-entry disk I/O.
;
; DOS 2.x/DIP DOS's actual extended error codes reachable from AH=0x56 are
; a best-effort guess pending real-hardware verification, same caveat as
; the other commands - see PROTOCOL.md's "Adding a new command" checklist
; item 5. Cross-drive rename behavior (old/new path on different drive
; letters) is untested - DOS is documented to fail this, but the exact
; error code on DIP DOS is unknown; PFTD does not pre-check drive letters
; itself, whatever AH=0x56 returns is passed through the same mapping.

RENAME_CMD equ 0x8B

rename_response:  resb 2    ; status(1B) + errcode(1B)
rename_old_path:  resb 80   ; local copy of the request's first ASCIIZ
                             ; path, same pattern as mkdir_path/delete_path
rename_new_path:  resb 80   ; local copy of the request's second ASCIIZ
                             ; path, immediately follows the first in the
                             ; request buffer

; dispatch_rename: called from pftd_int61_handler with AL = payload[0] and
; CS=DS. Uses list_src_ds/list_src_si to pull both paths out of the
; foreign buffer, same as dispatch_mkdir/dispatch_delete/dispatch_rmdir/
; dispatch_list - but copies two consecutive ASCIIZ strings instead of one.
dispatch_rename:
        cmp     al, RENAME_CMD
        jne     .not_rename

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
        mov     di, rename_old_path
.copy_old:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_old

        ; si now points one past the old path's NUL - straight into the
        ; new path within the same foreign buffer, still DS=foreign here.
        mov     di, rename_new_path
.copy_new:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_new

        push    cs
        pop     ds              ; DS=CS from here on - nothing below this
                                 ; line ever uses the foreign segment again

        mov     byte [cs:critical_error_flag], 0

        mov     dx, rename_old_path
        push    cs
        pop     es
        mov     di, rename_new_path
        mov     ah, 0x56        ; Rename File
        int     0x21

        ; CF must be tested immediately after int 0x21, before anything
        ; else (even a CMP against critical_error_flag) clobbers it - see
        ; mkdir.inc's dispatch_mkdir for the real-hardware bug this
        ; ordering fixes.
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
        mov     byte [rename_response], 0x20
        mov     byte [rename_response+1], 0
        jmp     .respond

.critical_fired:
        mov     byte [rename_response], 0x10
        mov     byte [rename_response+1], 0xFF
        jmp     .respond

.dos_error:
        ; AX = DOS extended error code. Mapping below is a best-effort
        ; guess for DOS 2.x/DIP DOS - VERIFY ON REAL HARDWARE (see header
        ; comment).
        mov     byte [rename_response], 0x10
        cmp     ax, 2                    ; file not found
        je      .err_notfound
        cmp     ax, 3                    ; path not found
        je      .err_notfound
        cmp     ax, 5                    ; access denied (also covers
        je      .err_denied              ; "destination already exists",
                                          ; cross-drive rename)
        cmp     ax, 17                   ; not same device (cross-drive,
        je      .err_denied              ; if DOS distinguishes it - see
                                          ; header comment)
        mov     byte [rename_response+1], 0xFF   ; unrecognized code
        jmp     .respond
.err_notfound:
        mov     byte [rename_response+1], 1
        jmp     .respond
.err_denied:
        mov     byte [rename_response+1], 4

.respond:
        mov     dx, rename_response
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
.not_rename:
        ret
