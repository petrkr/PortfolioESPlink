; TDRIVES.asm - DOSBox-only test helper, NEVER for real Portfolio
; hardware. Exercises PFTD's DRIVES (0x87, see drives.inc) detection
; and dispatch path the same way TLISTEXT exercises LIST extended.
;
; What this can and can't verify:
;   - CAN verify: PFTD's "watch the next int 0x61 call" detection trick
;     correctly recognizes payload[0]=0x87 and calls dispatch_drives
;     without crashing/hanging, and (via STUB61's transmit log) the
;     actual response byte (drive count from int 0x21 AH=0x0E/AH=0x19).
;   - CANNOT verify: whether "1..count are all present, no gaps" holds
;     on real Portfolio DIP DOS, or the exact count on real hardware -
;     that was already confirmed separately via DRIVEDBG.COM (repo
;     root) on real Portfolio hardware. This only proves PFTD's
;     detection/dispatch plumbing works, same scope as TLISTEXT.
;
; Usage in DOSBox:
;   STUB61                          <- installs transmit-logging int 0x61 stub
;   PFTDN                           <- must be built with -dCHECK_POFO=0
;   TDRIVES                         <- runs this test
;
; Assemble: nasm -f bin TDRIVES.asm -o TDRIVES.COM

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
        ; with payload_buf holding payload[0]=0x87. This makes PFTD
        ; remember DS:DX and set pending=1, then fall through to
        ; .chain (STUB61's IRET) - safe because STUB61 is loaded.
        mov     dx, payload_buf
        mov     ax, 0x3001
        int     0x61

        ; Step 2: any further int 0x61 call makes PFTD check pending,
        ; read payload_buf[0]=0x87, and call dispatch_drives. AX=0x3000
        ; here (transmit, harmless/no-op via STUB61) rather than
        ; something PFTD might special-case.
        mov     dx, dummy_buf
        mov     cx, 1
        mov     ax, 0x3000
        int     0x61

        ; If we get here, PFTD's DRIVES detection/dispatch path ran to
        ; completion without hanging or crashing - that's the thing
        ; under test. The actual response byte (drive count) was
        ; logged by STUB61 above, if loaded.
        mov     dx, msg_done
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4c00
        int     0x21

msg_no_vector db 'int 0x61 vector is NULL - load STUB61 and PFTDN first.', 13, 10, '$'
msg_done      db 'DRIVES dispatch completed without hanging - check STUB61 TX= log above.', 13, 10, '$'

payload_buf db 0x87, 0x00, 0x70, 0
dummy_buf   db 0
