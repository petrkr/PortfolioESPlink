#ifndef VERSION_H_
#define VERSION_H_

// Dev snapshot marker for this ESP32 firmware build, bumped by 1 on every
// change that should be distinguishable over /status - same convention as
// PFTD's own BUILD_ID (version.inc in the POFOSCAB repo - see PROTOCOL.md),
// just a separate counter since this firmware and the Atari-side TSR are
// built/flashed separately.
#define FW_BUILD_ID 0x00000009

#endif
