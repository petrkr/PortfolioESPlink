#include "CableLink.h"

#include <LittleFS.h>
#include <log4mcu.h>

#include <board_config.h>

CableLink cableLink;

static log4mcu::Logger& logger = log4mcu::Logger::get("CableLink");

static void onLinkStateChanged(bool isOnline) {
  logger.infof("Link %s", isOnline ? "online" : "offline");
  digitalWrite(LED2, isOnline ? 0 : 1);
  digitalWrite(LED3, isOnline ? 1 : 0);
}

static void onProgress(size_t transferred, size_t total) {
  static uint8_t lastPercent = 0xff;
  const uint8_t percent = total ? static_cast<uint8_t>(transferred * 100UL / total) : 100;
  if (percent != lastPercent) {
    logger.infof("Progress: %u%% (%u/%u bytes)", percent,
                 static_cast<unsigned>(transferred),
                 static_cast<unsigned>(total));
    lastPercent = percent;
  }
  cableLink.setProgress(transferred, total);
}

bool CableLink::begin(int clkIn, int dataIn, int clkOut, int dataOut) {
  if (!cable_.begin(clkIn, dataIn, clkOut, dataOut)) {
    logger.error("PofoSmartCable begin failed");
    return false;
  }
  cable_.setLinkStateCallback(onLinkStateChanged);
  fileTransfer_.setProgressCallback(onProgress);
  return true;
}

void CableLink::loop() {
  cable_.loop();
  processPending();
}

bool CableLink::online() const {
  return cable_.online();
}

bool CableLink::busy() const {
  return busy_;
}

void CableLink::queueTransmit(const String& localPath, const String& pofoPath, bool overwrite) {
  pendingLocalPath_ = localPath;
  pendingPofoPath_ = pofoPath;
  pendingOverwrite_ = overwrite;
}

PofoResult CableLink::list(const char* path, PofoFileTransferList* response) {
  return fileTransfer_.list(path, response);
}

void CableLink::processPending() {
  if (pendingLocalPath_.isEmpty() || !cable_.online()) {
    return;
  }

  File file = LittleFS.open(pendingLocalPath_, "r");
  if (!file) {
    logger.warnf("Cannot open queued file: %s", pendingLocalPath_.c_str());
    pendingLocalPath_ = "";
    pendingPofoPath_ = "";
    return;
  }

  const size_t length = file.size();
  logger.infof("Transmitting %s to %s (%u bytes)", pendingLocalPath_.c_str(),
               pendingPofoPath_.c_str(), static_cast<unsigned>(length));

  busy_ = true;
  total_ = length;
  transferred_ = 0;

  const PofoResult result =
      fileTransfer_.transmitFile(pendingPofoPath_.c_str(), file, length, pendingOverwrite_);
  file.close();

  if (result != PofoResult::OK) {
    logger.warnf("TransmitFile failed: %u", static_cast<unsigned>(result));
  } else {
    logger.info("Transmit complete");
  }

  pendingLocalPath_ = "";
  pendingPofoPath_ = "";
  busy_ = false;
  total_ = 0;
  transferred_ = 0;
}
