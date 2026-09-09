; residentcheck.inc - "are we already installed?" probe, so running PFTD
; a second time doesn't stack a duplicate int 0x61 hook on top of itself.
;
; This is deliberately NOT built on the same payload[0]/pending mechanism
; as HELLO (hello.inc) - that path only fires after a real ROM receive-
; block handshake (AH=0x30 AL=1) and answers over the smart cable, which
; needs the ESP32 side alive on the other end. The installer needs to ask
; "is a PFTD hook already live?" synchronously, with nothing attached to
; the cable, so this uses a private AX subfunction that legitimate ROM
; code and the HELLO/transmit path never produce: AX=0x6101. If that
; comes back with AX=PROBE_ANSWER, a PFTD hook answered directly out of
; registers - no ROM call, no cable I/O, no pending/saved_* state touched.
;
; PROBE_CMD (0x6101) is chosen from the same "far outside anything the ROM
; dispatches" reasoning as HELLO_CMD in hello.inc - AH=0x61 is not a value
; the ROM's own AH=0x30 transport ever produces, so it can never collide
; with a real receive-block handshake landing in the pending/payload0
; path in PFTD.asm.

PROBE_CMD    equ 0x6101
PROBE_ANSWER equ 0x4650        ; 'F','P' - answered directly in AX, no cable I/O

; check_already_resident: called from pftd_int61_handler with the
; original caller's AX still intact (must run before AX is disturbed).
; If AX == PROBE_CMD, answers with AX = PROBE_ANSWER and returns (ret) -
; the caller (pftd_int61_handler) must IRET immediately on return, never
; push anything before checking. This routine itself must never IRET: it
; was reached via CALL, which only pushed a 2-byte return address, not
; the 6-byte FLAGS/CS/IP frame IRET expects - doing so here previously
; popped the call's own return address as if it were IP/CS/FLAGS and
; jumped into garbage (observed on real hardware as int 6 - invalid
; opcode - the second time PFTD was run in DOSBox).
; If AX != PROBE_CMD, ZF is clear on return and pftd_int61_handler
; continues normally.
check_already_resident:
        cmp     ax, PROBE_CMD
        jne     .not_probe
        mov     ax, PROBE_ANSWER
.not_probe:
        ret
