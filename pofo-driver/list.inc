; list.inc - LIST extended (0x86) command: directory listing with
; attribute + size per entry, unlike the ROM's own list (payload[0]=6,
; see ROM_RESEARCH_NOTES.md) which only ever returns bare names.
;
; Command byte 0x86 was chosen as the direct analogue of the ROM's own
; list command (6) - see hello.inc's header comment for why the 0x80+
; command space is safe to use. Left as a new command rather than
; changing the ROM's list dispatch: legacy list (6) keeps working
; unmodified for anything that only speaks the stock protocol - the
; client picks 0x86 over 6 based on the HELLO capabilities bit
; (CAP_LIST_EXT below).
;
; Request layout (identical prefix to the ROM's own list request, see
; PortfolioLink::runList's receiveInit_): payload[0]=0x86, payload[1..2]
; unused (0x00, 0x70 - mirrors the ROM's own convention), payload[3..]
; ASCIIZ search pattern (e.g. "*.*").
;
; Response layout (count(2B LE) + N * entry - same count+N-entries shape
; as the ROM's own list, just with attr+size/date/time prepended to each
; entry so existing count/name-walking logic only needs extra fixed-size
; fields, not a different structure):
;   offset 0-1   count (2 bytes LE) - number of entries that follow
;   per entry:
;     attr       (1 byte)  full DOS attribute byte, straight from the
;                          Find First/Next DTA (bit0=read-only,
;                          bit1=hidden, bit2=system, bit4=directory,
;                          bit5=archive) - NOT just an isolated dir bit,
;                          client decodes whichever bits it cares about
;     size       (4 bytes LE) - 0 for directories
;     date       (2 bytes) DOS packed date, straight from the DTA:
;                          bits 15-9 = year-1980, 8-5 = month, 4-0 = day
;     time       (2 bytes) DOS packed time, straight from the DTA:
;                          bits 15-11 = hour, 10-5 = minute, 4-0 = second/2
;     name       ASCIIZ (8.3 format, up to 12 chars + NUL)
;
; date/time are passed through exactly as DOS stores them (packed, not
; decoded) - this is all Find First/Next exposes on DIP DOS/DOS 2.11
; (no created/accessed time, that needs LFN-era APIs this DOS doesn't
; have), so there is nothing gained by unpacking them here instead of on
; the client.
;
; Built on DOS Find First/Find Next (int 0x21 AH=0x4E/0x4F) via our own
; DTA (Disk Transfer Area). Calling int 0x21 from here is only safe
; because of WHEN dispatch_list runs: it is called from
; pftd_int61_handler while still inside the ROM's own int 0x61 call (the
; ROM's receive-block loop waits forever for the next handshake, see
; ROM_RESEARCH_NOTES.md - delaying the return costs nothing) - that
; ROM call is the Portfolio's own transport primitive (AH=0x30), which
; never enters a DOS critical section, so DOS is still idle from DOS's
; own reentrancy point of view. This is the same reasoning PFTD.asm's
; header comment gives for why dispatch_hello's recursive int 0x61
; transmit is safe, extended one level further to int 0x21 itself.
;
; DS discipline: only DS is used as a data segment override throughout
; (never ES) - int 0x21 does not reliably preserve ES across calls. DS
; is switched to the foreign segment ONLY for the initial pattern copy;
; every DOS call after that, and all writes into list_response, run
; with DS=CS, which DOS calls do not change out from under us (DS:DX is
; a call parameter, not something DOS repoints).

; CAP_LIST_EXT (HELLO capabilities bit 0) is defined in hello.inc, not
; here - see that file's capabilities bitmask comment for why.

LIST_CMD equ 0x86

LIST_BUFSIZE equ 2048   ; fixed cap - see list.inc design notes; entries
                        ; beyond this are silently truncated (count
                        ; reflects only what fits)

list_response:    resb LIST_BUFSIZE
list_pattern:     resb 80         ; local copy of the request's ASCIIZ
                                   ; pattern, copied out of the foreign
                                   ; buffer before DS is switched back
dta_buf:          resb 43         ; DOS Find First/Next DTA record size

; dispatch_list: called from pftd_int61_handler with AL = payload[0]
; and CS=DS (per pftd_int61_handler's "DS=CS again immediately"
; comment). Needs more than payload[0] out of the foreign receive
; buffer (the ASCIIZ pattern), so the caller also passes the buffer's
; original DS:DX via list_src_ds/list_src_si (set by
; pftd_int61_handler alongside payload0, see PFTD.asm) - unlike
; dispatch_hello, which only ever needs AL.
dispatch_list:
        cmp     al, LIST_CMD
        jne     .not_list

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds

        ; Copy the ASCIIZ pattern out of the foreign buffer
        ; (list_src_ds:list_src_si+3, skipping the 3-byte header) into
        ; our own list_pattern. ES=CS is the destination segment here;
        ; DS is switched to the foreign segment only for this loop.
        push    cs
        pop     es
        mov     ax, [cs:list_src_ds]
        mov     ds, ax
        mov     si, [cs:list_src_si]
        add     si, 3
        mov     di, list_pattern
.copy_pattern:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_pattern

        push    cs
        pop     ds                      ; DS=CS from here on - nothing
                                         ; below touches the foreign
                                         ; segment again

        ; Set our own DTA (int 0x21 AH=0x1A) so Find First/Next write
        ; into dta_buf rather than whatever DTA a foreground DOS
        ; process had set.
        mov     dx, dta_buf
        mov     ah, 0x1A
        int     0x21

        mov     di, list_response + 2   ; leave room for the count header

        mov     dx, list_pattern
        mov     cx, 0x16                ; attribute mask: include
                                         ; subdirectories + normal files
        mov     ah, 0x4E                ; Find First
        int     0x21
        jc      .no_matches             ; CF set = no match at all

        xor     cx, cx                  ; cx = entry count so far

.entry:
        ; dta_buf+0x15 = attribute, +0x16 = time (2B), +0x18 = date (2B),
        ; +0x1A = size (4B LE), +0x1E = name (ASCIIZ, up to 13 bytes
        ; incl. NUL) - standard DOS Find First/Next DTA layout.
        mov     ax, di
        sub     ax, list_response
        cmp     ax, LIST_BUFSIZE - 32
        jae     .done                   ; not enough room for another
                                         ; entry - truncate here, count
                                         ; already reflects only what fits

        mov     al, [dta_buf+0x15]      ; full DOS attribute byte,
        mov     [di], al                ; passed through as-is
        inc     di

        mov     ax, [dta_buf+0x1A]
        mov     [di], ax
        mov     ax, [dta_buf+0x1C]
        mov     [di+2], ax
        add     di, 4

        mov     ax, [dta_buf+0x18]      ; date (packed, passed through)
        mov     [di], ax
        add     di, 2

        mov     ax, [dta_buf+0x16]      ; time (packed, passed through)
        mov     [di], ax
        add     di, 2

        mov     si, dta_buf+0x1E
.name_copy:
        mov     al, [si]
        mov     [di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .name_copy

        inc     cx

        mov     ah, 0x4F                ; Find Next
        int     0x21
        jnc     .entry

        jmp     .done

.no_matches:
        xor     cx, cx

.done:
        mov     [list_response], cx     ; count (2 bytes LE, cx already
                                         ; holds it that way)

        mov     dx, list_response
        mov     ax, di
        sub     ax, list_response
        mov     cx, ax                  ; cx = total response length
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61

        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
.not_list:
        ret
