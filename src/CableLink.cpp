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

bool CableLink::requestTransmit(const String& localPath, const String& pofoPath, bool overwrite) {
  if (pendingDirection_ != Direction::NONE) {
    return false;
  }

  pendingDirection_ = Direction::TRANSMIT;
  pendingLocalPath_ = localPath;
  pendingPofoPath_ = pofoPath;
  pendingOverwrite_ = overwrite;
  lastError_ = "";
  return true;
}

bool CableLink::requestReceive(const String& pofoPath, const String& localPath, bool overwrite) {
  if (pendingDirection_ != Direction::NONE) {
    return false;
  }

  if (!overwrite && LittleFS.exists(localPath)) {
    return false;
  }

  pendingDirection_ = Direction::RECEIVE;
  pendingPofoPath_ = pofoPath;
  pendingLocalPath_ = localPath;
  pendingOverwrite_ = overwrite;
  lastError_ = "";
  return true;
}

PofoResult CableLink::list(const char* path, PofoFileTransferList* response) {
  return fileTransfer_.list(path, response);
}

void CableLink::processPending() {
  if (pendingDirection_ == Direction::NONE || !cable_.online()) {
    return;
  }

  const Direction direction = pendingDirection_;
  busy_ = true;
  transferred_ = 0;
  total_ = 0;

  PofoResult result;
  if (direction == Direction::TRANSMIT) {
    File file = LittleFS.open(pendingLocalPath_, "r");
    if (!file) {
      lastError_ = "Cannot open local file";
      logger.warnf("Cannot open queued file: %s", pendingLocalPath_.c_str());
      pendingDirection_ = Direction::NONE;
      busy_ = false;
      return;
    }

    const size_t length = file.size();
    total_ = length;
    logger.infof("Transmitting %s to %s (%u bytes)", pendingLocalPath_.c_str(),
                 pendingPofoPath_.c_str(), static_cast<unsigned>(length));

    result = fileTransfer_.transmitFile(pendingPofoPath_.c_str(), file, length, pendingOverwrite_);
    file.close();
  } else {
    File file = LittleFS.open(pendingLocalPath_, "w");
    if (!file) {
      lastError_ = "Cannot create local file";
      logger.warnf("Cannot create local file: %s", pendingLocalPath_.c_str());
      pendingDirection_ = Direction::NONE;
      busy_ = false;
      return;
    }

    logger.infof("Receiving %s into %s", pendingPofoPath_.c_str(), pendingLocalPath_.c_str());

    result = fileTransfer_.receiveFile(pendingPofoPath_.c_str(), file);
    file.close();
  }

  if (result != PofoResult::OK) {
    lastError_ = "Transfer failed (" + String(static_cast<unsigned>(result)) + ")";
    logger.warnf("Transfer failed: %u", static_cast<unsigned>(result));
  } else {
    logger.info("Transfer complete");
  }

  pendingDirection_ = Direction::NONE;
  pendingLocalPath_ = "";
  pendingPofoPath_ = "";
  busy_ = false;
  total_ = 0;
  transferred_ = 0;
}
