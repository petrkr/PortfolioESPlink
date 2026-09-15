; version.inc - protocol version and build id for PFTD.
; Machine-editable: CI/build tooling may regenerate or patch the two
; VERSION/BUILD_ID lines below without touching anything else in this
; driver. Keep the equ syntax exactly as-is if generating this file
; programmatically (a simple line replace is enough).

VERSION   equ 1                ; HELLO response protocol version (offset 8)
BUILD_ID  equ 0xFFFF0014       ; dev snapshot marker (offset 4-7) or, once
                                ; committed, the 4-byte binary git short
                                ; hash - see hello.inc for the full layout
