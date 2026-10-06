#pragma once

#include <stdint.h>

#include <PofoFileTransfer.h>
#include <PofoSmartCableResult.h>

class PofoSmartCable;

// Response to the PFTD HELLO command (payload[0]=0x80). See
// ~/git/POFOSCAB/src/pftd/hello.inc for the wire format this parses:
// magic "PFD1" + 4-byte build id + major/minor/patch + 1 reserved byte.
struct PortfolioLinkHello {
  uint32_t buildId = 0;
  uint8_t versionMajor = 0;
  uint8_t versionMinor = 0;
  uint8_t versionPatch = 0;
};

// Wraps PofoFileTransfer (stock ROM File Transfer Server commands) and adds
// PFTD (Portfolio Transfer Daemon) extension commands - payload[0] >= 0x80,
// a TSR the Atari side optionally runs on top of the ROM server. PFTD lives
// in this project (not the PofoSmartCable library) until it has proven
// itself: the library currently only exposes native Smart Cable / ROM File
// Transfer Server functionality.
//
// list()/receiveFile()/transmitFile() delegate straight to an internal
// PofoFileTransfer; hello() and future PFTD commands use cable_ directly,
// the same way PofoFileTransfer itself does.
class PortfolioLink {
 public:
  explicit PortfolioLink(PofoSmartCable& cable);

  void setProgressCallback(PofoFileTransferProgress progress);
  PofoResult list(const char* path, PofoFileTransferList* response);
  PofoResult receiveFile(const char* path, Stream& output);
  PofoResult transmitFile(const char* path, Stream& input, size_t length,
                          bool overwrite, time_t timestamp = 0);

  // Queries whether PFTD is installed on the Portfolio and, if so, its
  // build id and version. Returns PofoResult::OK with response populated
  // when PFTD answered; PofoResult::TIMEOUT (no answer - either no PFTD
  // installed, or a stock ROM waiting on something else entirely) is the
  // expected "not present" outcome, not a real error.
  PofoResult hello(PortfolioLinkHello* response);

  // Queries how many logical drives DOS knows about (1 = A: only, 2 = A:
  // and B:, ...). PFTD-only (see ~/git/POFOSCAB/src/pftd/drives.inc);
  // requires hello() to have reported PFTD present first.
  PofoResult drives(uint8_t* driveCount);

 private:
  PofoSmartCable& cable_;
  PofoFileTransfer fileTransfer_;
};
