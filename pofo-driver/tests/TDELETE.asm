; TDELETE.asm - DOSBox-only test helper, NEVER for real Portfolio hardware.
; Exercises PFTD's DELETE (0x89, see delete.inc) detection and dispatch
; path the same way TDRIVES/TLISTEXT/TMKDIR exercise their own commands.
;
; What this can and can't verify:
;   - CAN verify: PFTD's "watch the next int 0x61 call" detection trick
;     correctly recognizes payload[0]=0x89, copies the ASCIIZ path out of
;     the receive buffer, calls int 0x21 AH=0x41 (Unlink) against the real
;     DOSBox filesystem, and calls dispatch_delete without crashing/
;     hanging. With STUB61 loaded below PFTD, the 2-byte status+errcode
;     response is logged as hex and can be eyeballed.
;   - CANNOT verify: DOS 2.x/DIP DOS's actual extended error code behavior
;     on AH=0x41 (delete.inc's error mapping is a best-effort guess pending
;     real-hardware verification - see PROTOCOL.md), or the int 0x24
;     critical-error path at all - same caveat as TMKDIR.
;
; This test targets TESTDEL.TXT - create that file in the DOSBox mount
; before running for a status=0x20 (success) response, or run without it
; present to instead exercise the "file not found" (status=0x10, errcode=1)
; path; either way the test only checks "completed without hanging", not
; the specific response bytes.
;
; Usage in DOSBox:
;   STUB61                          <- installs transmit-logging int 0x61 stub
;   PFTDN                           <- must be built with -dCHECK_POFO=0
;   TDELETE                         <- runs this test, deletes TESTDEL.TXT
;
; Assemble: nasm -f bin TDELETE.asm -o TDELETE.COM

CPU 8086
ORG 0x100

start:
        ; Bail out early with a clear message if int 0x61 is NULL - means
        ; STUB61 (or PFTD) isn't loaded, and the test below would hang.
        mov     ax, 0x3561
        int     0x21
        mov     ax, es
        or      ax, bx
        jnz     .vector_ok

        mov     dx, msg_no_vector
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4c01
        int     0x21

.vector_ok:
        ; Step 1: simulate the ROM's receive-block call (AH=0x30 AL=1)
        ; with payload_buf holding payload[0]=0x89 followed by the ASCIIZ
        ; path at offset 3 (same request layout PortfolioLink::runDelete
        ; uses: [cmd][0][0x70] + ASCIIZ path). This makes PFTD remember
        ; DS:DX and set pending=1, then fall through to .chain (STUB61's
        ; IRET) - safe because STUB61 is loaded.
        mov     dx, payload_buf
        mov     ax, 0x3001
        int     0x61

        ; Step 2: any further int 0x61 call makes PFTD check pending,
        ; read payload_buf[0]=0x89, copy the path, and call
        ; dispatch_delete. AX=0x3000 here (transmit, harmless/no-op via
        ; STUB61) rather than something PFTD might special-case.
        mov     dx, dummy_buf
        mov     cx, 1
        mov     ax, 0x3000
        int     0x61

        ; If we get here, PFTD's DELETE detection/dispatch path ran to
        ; completion without hanging or crashing - that's the thing under
        ; test. The actual response bytes (status+errcode) were logged by
        ; STUB61 above, if loaded.
        mov     dx, msg_done
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4c00
        int     0x21

msg_no_vector db 'int 0x61 vector is NULL - load STUB61 and PFTDN first.', 13, 10, '$'
msg_done      db 'DELETE dispatch completed without hanging - check STUB61 TX= log above.', 13, 10, '$'

payload_buf db 0x89, 0x00, 0x70, 'TESTDEL.TXT', 0
dummy_buf   db 0
