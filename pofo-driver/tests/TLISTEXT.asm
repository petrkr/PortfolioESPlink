; TLISTEXT.asm - DOSBox-only test helper, NEVER for real Portfolio
; hardware. Exercises PFTD's LIST extended (0x86, see list.inc) detection
; and dispatch path the same way THELLO exercises HELLO (0x80).
;
; What this can and can't verify:
;   - CAN verify: PFTD's "watch the next int 0x61 call" detection trick
;     correctly recognizes payload[0]=0x86, copies the ASCIIZ pattern out
;     of the receive buffer, runs Find First/Find Next against the real
;     DOS filesystem (int 0x21 AH=0x4E/0x4F - identical between DOSBox
;     and Portfolio DIP DOS, see ROM_RESEARCH_NOTES.md), and calls
;     dispatch_list without crashing/hanging.
;   - CAN verify the actual response bytes too, unlike THELLO: with
;     STUB61 loaded below PFTD, the transmit dispatch_list issues is
;     logged as hex by STUB61's new61 handler - this test's pattern
;     (see below) is chosen to match files that actually exist next to
;     this .COM in DOSBox, so the logged bytes can be eyeballed against
;     a DIR listing.
;   - CANNOT verify: behaviour against real Portfolio DIP DOS filesystem
;     geometry/attributes - only that the DOS Find First/Next call
;     sequence and PFTD's own buffer handling don't hang or corrupt
;     state. End-to-end still needs real Portfolio + ESP32 hardware.
;
; Usage in DOSBox:
;   STUB61                          <- installs transmit-logging int 0x61 stub
;   PFTDN                           <- must be built with -dCHECK_POFO=0
;   TLISTEXT                        <- runs this test, pattern "*.*"
;
; Assemble: nasm -f bin TLISTEXT.asm -o TLISTEXT.COM

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
        ; with payload_buf holding payload[0]=0x86 followed by the
        ; ASCIIZ pattern at offset 3 (same request layout PortfolioLink
        ; ::runListExt uses: [cmd][0][0x70] + ASCIIZ pattern). This makes
        ; PFTD remember DS:DX and set pending=1, then fall through to
        ; .chain (STUB61's IRET) - safe because STUB61 is loaded.
        mov     dx, payload_buf
        mov     ax, 0x3001
        int     0x61

        ; Step 2: any further int 0x61 call makes PFTD check pending,
        ; read payload_buf[0]=0x86, copy the pattern, and call
        ; dispatch_list. AX=0x3000 here (transmit, harmless/no-op via
        ; STUB61) rather than something PFTD might special-case.
        mov     dx, dummy_buf
        mov     cx, 1
        mov     ax, 0x3000
        int     0x61

        ; If we get here, PFTD's LIST detection/dispatch path ran to
        ; completion without hanging or crashing - that's the thing
        ; under test. The actual response bytes (count + attr/size/name
        ; per entry) were logged by STUB61 above, if loaded.
        mov     dx, msg_done
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4c00
        int     0x21

msg_no_vector db 'int 0x61 vector is NULL - load STUB61 and PFTDN first.', 13, 10, '$'
msg_done      db 'LIST EXT dispatch completed without hanging - check STUB61 TX= log above.', 13, 10, '$'

payload_buf db 0x86, 0x00, 0x70, '*.*', 0
dummy_buf   db 0
