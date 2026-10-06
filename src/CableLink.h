#pragma once

#include <Arduino.h>
#include <PofoSmartCable.h>
#include <PofoFileTransfer.h>

// Owns the PofoSmartCable link and a one-file-at-a-time transmit queue fed by
// the web upload handler. processPending() must be called from loop() so a
// queued file is sent to the Portfolio once the link is online.
class CableLink {
 public:
  bool begin(int clkIn, int dataIn, int clkOut, int dataOut);
  void loop();

  bool online() const;
  bool busy() const;

  // Queues a local file for transfer to pofoPath. Overwrites any not-yet-sent
  // queued file.
  void queueTransmit(const String& localPath, const String& pofoPath, bool overwrite);

  // path is required, for example "*.*" or "C:\\*.*".
  PofoResult list(const char* path, PofoFileTransferList* response);

  // Progress of the in-flight transmit, in bytes. total is 0 when idle.
  size_t transferredBytes() const { return transferred_; }
  size_t totalBytes() const { return total_; }

  // Called by the PofoFileTransfer progress callback; not for application use.
  void setProgress(size_t transferred, size_t total) {
    transferred_ = transferred;
    total_ = total;
  }

 private:
  void processPending();

  PofoSmartCable cable_;
  PofoFileTransfer fileTransfer_{cable_};

  String pendingLocalPath_;
  String pendingPofoPath_;
  bool pendingOverwrite_ = false;
  bool busy_ = false;

  size_t transferred_ = 0;
  size_t total_ = 0;
};

extern CableLink cableLink;
