#pragma once

#include <Arduino.h>
#include <PofoSmartCable.h>
#include <PofoFileTransfer.h>

// Owns the PofoSmartCable link and the single pending transfer (upload to or
// download from the Portfolio) requested by the web API. loop() must be
// called regularly from the sketch's loop() so a requested transfer runs
// once the link is online; HTTP handlers only call requestTransmit()/
// requestReceive() and return immediately, polling busy()/lastError() via
// /status to report progress.
class CableLink {
 public:
  bool begin(int clkIn, int dataIn, int clkOut, int dataOut);
  void loop();

  bool online() const;
  bool busy() const;
  const String& lastError() const { return lastError_; }

  // Requests sending localPath (LittleFS) to pofoPath (Portfolio) once the
  // link is online. Returns false if a transfer is already pending/running.
  bool requestTransmit(const String& localPath, const String& pofoPath, bool overwrite);

  // Requests receiving pofoPath (Portfolio) into localPath (LittleFS) once
  // the link is online. Returns false if a transfer is already
  // pending/running, or if localPath exists and overwrite is false.
  bool requestReceive(const String& pofoPath, const String& localPath, bool overwrite);

  // path is required, for example "*.*" or "C:\\*.*".
  PofoResult list(const char* path, PofoFileTransferList* response);

  // Progress of the in-flight transfer, in bytes. total is 0 when idle.
  size_t transferredBytes() const { return transferred_; }
  size_t totalBytes() const { return total_; }

  // Called by the PofoFileTransfer progress callback; not for application use.
  void setProgress(size_t transferred, size_t total) {
    transferred_ = transferred;
    total_ = total;
  }

 private:
  enum class Direction { NONE, TRANSMIT, RECEIVE };

  void processPending();

  PofoSmartCable cable_;
  PofoFileTransfer fileTransfer_{cable_};

  Direction pendingDirection_ = Direction::NONE;
  String pendingLocalPath_;
  String pendingPofoPath_;
  bool pendingOverwrite_ = false;
  bool busy_ = false;
  String lastError_;

  size_t transferred_ = 0;
  size_t total_ = 0;
};

extern CableLink cableLink;
