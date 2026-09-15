# PFTD protocol reference

Command byte layout is source-of-truth in code (`pofo-driver/*.inc` on
the Atari side, `src/PortfolioLink.cpp` on the ESP side) - this is a
summary to keep in sync with it, not a replacement for reading the code.

## Overview

`payload[0]` selects the command:

- `[2, 6]` - built into the Portfolio ROM's "File transfer Server" mode,
  fixed dispatch, cannot be extended (see `ROM_RESEARCH_NOTES.md` for
  the reverse-engineering that established this).
- `0x80+` - PFTD-only command space, handled entirely by the
  `pofo-driver` TSR, no ROM involvement.

Transport for both ranges is the same `sendBlock`/`receiveBlock`
handshake (`src/PortfolioLink.cpp`) over `int 0x61 AH=0x30`.

## ROM commands (2-6)

| Code | Purpose | ESP side |
|---|---|---|
| `0x02` | Receive file (Portfolio -> ESP) | `PortfolioLink::runDownload`, `receiveFileInit_` (`PortfolioLink.h`) |
| `0x03` | Transmit init (ESP -> Portfolio) | `PortfolioLink::runUpload`, `transmitInit_` (`PortfolioLink.h`) |
| `0x05` | Transmit overwrite confirm | `runUpload`, `TRANSMIT_OVERWRITE` (`PortfolioLink.cpp`) |
| `0x06` | List (legacy, names only) | `PortfolioLink::runList`, `receiveInit_` (`PortfolioLink.h`) |
| `0x00` | Cancel transfer | `TRANSMIT_CANCEL` (`PortfolioLink.cpp`), sent when overwrite is declined |

Response control byte conventions: `0x10` = error (bad path/disk
full/not found), `0x20` = ok / file exists (upload) / transfer done.

## PFTD commands (0x80+)

### HELLO (`0x80`)

Presence/capability discovery. Request: single byte `0x80`. Response
(fixed 12 bytes):

| Offset | Size | Content |
|---|---|---|
| 0-3 | 4B | magic `"PFD1"` |
| 4-7 | 4B | build id (binary, LE) |
| 8 | 1B | version (`1` = this format; `0xFF` = extended, offset 9 carries an extended version number and everything after is redefined) |
| 9 | 1B | capabilities bitmask |
| 10-11 | 2B | reserved (`0x00, 0x00`) |

- ESP: `PortfolioLink::runHello` (`PortfolioLink.cpp`), public API
  `helloDaemon()`; auto-probed on connect in `taskLoop`.
- Atari: `pofo-driver/hello.inc` (`HELLO_CMD`, `hello_response`,
  `dispatch_hello`).

### LIST extended (`0x86`)

Same request prefix as ROM's `list` (`0x86, 0x00, 0x70` + ASCIIZ
pattern), response adds attributes/size/date/time per entry plus
free/total drive space at the end.

Response: `count(2B LE)` + N x `[attr(1B) + size(4B LE) + date(2B
packed DOS) + time(2B packed DOS) + name(ASCIIZ)]` + `free(4B LE) +
total(4B LE)`. Free/total is per-drive (from the pattern's drive
letter, or the current default drive), always present regardless of
capability bit.

- ESP: `PortfolioLink::runListExt` (`PortfolioLink.cpp`), public API
  `listFilesExtended()`.
- Atari: `pofo-driver/list.inc` (`LIST_CMD`, `dispatch_list`).

### DRIVES (`0x87`)

How many logical DOS drives exist (`A=1, B=2, ...`). Request: single
byte `0x87`. Response: single byte, drive count.

- ESP: `PortfolioLink::runDrives` (`PortfolioLink.cpp`), public API
  `listDrives()`.
- Atari: `pofo-driver/drives.inc` (`DRIVES_CMD`, `dispatch_drives`).

## Capabilities bitmask (HELLO response, offset 9)

| Bit | Constant | Meaning |
|---|---|---|
| 0 (`0x01`) | `CAP_LIST_EXT` | LIST extended (`0x86`) supported |
| 1 (`0x02`) | `CAP_DRIVES` | DRIVES (`0x87`) supported |

Defined in `pofo-driver/hello.inc`; currently both bits are always set
(`CAP_LIST_EXT | CAP_DRIVES`). ESP side reads the raw byte into
`pftdCapabilities()` (`PortfolioLink.h`/`.cpp`) without named-bit
helpers - callers mask it themselves.

## Version / BUILD_ID

`pofo-driver/version.inc`:

```asm
VERSION   equ 1
BUILD_ID  equ 0xFFFF0010   ; dev snapshot marker
```

`BUILD_ID` is meant to be replaced with a binary git short hash at
build time (see comment in `version.inc`) - the value above is a dev
placeholder, not a real release marker.

## Adding a new command

1. Pick the next free code (`0x88+` - `0x81`-`0x85` are reserved,
   unused so far).
2. Give it its own capability bit in the HELLO response (offset 9),
   same pattern as `CAP_LIST_EXT`/`CAP_DRIVES`.
3. Implement `dispatch_<name>` in a new or existing `pofo-driver/*.inc`,
   wire it into `PFTD.asm`'s command detection.
4. Add a `run<Name>`/public method pair on the ESP side in
   `PortfolioLink.cpp`/`.h`, following `runHello`/`runListExt`/
   `runDrives`.
5. Add a standalone DOSBox test tool in `pofo-driver/tests/` (see
   `TLISTEXT.COM`/`TDRIVES.COM`) and verify on real hardware before
   trusting any RBIL-documented DOS function contract - DIP DOS
   diverges from PC MS-DOS behavior in ways DOSBox won't reveal (see
   `ROM_RESEARCH_NOTES.md`'s DIP DOS critical error section).

## Reserved / not yet implemented

- `0x81`-`0x85`: reserved, unused.
- Planned ideas (mkdir via `int 0x21 AH=0x39`, delete via
  `AH=0x41`/`AH=0x3A`, SETTIME via `AH=0x2D`/`AH=0x2B`): rationale and
  DOS-version caveats are in `ROM_RESEARCH_NOTES.md`.
