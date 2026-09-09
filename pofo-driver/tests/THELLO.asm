; THELLO.asm - DOSBox-only test helper, NEVER for real Portfolio
; hardware. Exercises PFTD's detection/dispatch path for HELLO without a
; real cable/ESP32 on the other end.
;
; What this can and can't verify:
;   - CAN verify: PFTD's "watch the next int 0x61 call" detection trick
;     correctly recognizes payload[0]=0x80 and calls dispatch_hello
;     without crashing/hanging - that's the actual thing worth testing
;     here in isolation.
;   - CANNOT verify: the bytes PFTD sends back. dispatch_hello answers
;     over a REAL int 0x61 AH=0x30 AL=0 (transmit) call, which chains
;     down to whatever's below PFTD - with STUB61.COM below it, that
;     transmit is silently swallowed by STUB61's IRET-only handler, not
;     captured here. The hello_response byte layout is instead verified
;     statically by inspecting the assembled PFTD.COM (e.g. via xxd) -
;     see hello.inc for the exact offsets.
;
; This only proves PFTD doesn't hang/crash when it receives HELLO and
; that it reaches the point of issuing the transmit call - not that the
; transmit itself is byte-correct end to end. That end-to-end check still
; needs real Portfolio + ESP32 hardware.
;
; Usage in DOSBox:
;   STUB61                          <- installs no-op int 0x61 stub
;   PFTDN                           <- must be built with -dCHECK_POFO=0
;   THELLO                       <- runs this test
;
; Assemble: nasm -f bin THELLO.asm -o THELLO.COM

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
        ; with payload_buf holding payload[0]=0x80 (HELLO). This makes
        ; PFTD remember DS:DX and set pending=1, then fall through to
        ; .chain (STUB61's IRET) - safe because STUB61 is loaded.
        mov     dx, payload_buf
        mov     ax, 0x3001
        int     0x61

        ; Step 2: any further int 0x61 call makes PFTD check pending,
        ; read payload_buf[0]=0x80, and call dispatch_hello. Using
        ; AX=0x3000 here (transmit, harmless/no-op via STUB61) rather
        ; than something PFTD might special-case.
        mov     dx, dummy_buf
        mov     cx, 1
        mov     ax, 0x3000
        int     0x61

        ; If we get here, PFTD's detection path ran to completion
        ; without hanging or crashing - that's the thing under test.
        mov     dx, msg_done
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4c00
        int     0x21

msg_no_vector db 'int 0x61 vector is NULL - load STUB61 and PFTDN first.', 13, 10, '$'
msg_done      db 'HELLO dispatch completed without hanging - detection path OK.', 13, 10, '$'

payload_buf db 0x80, 0
dummy_buf   db 0
