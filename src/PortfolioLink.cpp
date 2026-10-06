#include "PortfolioLink.h"

#include <PofoSmartCable.h>

namespace {

const uint8_t kHelloCommand = 0x80;
const size_t kHelloResponseLength = 12;

const uint8_t kDrivesCommand = 0x87;
const size_t kDrivesResponseLength = 1;

}  // namespace

PortfolioLink::PortfolioLink(PofoSmartCable& cable)
    : cable_(cable), fileTransfer_(cable) {}

void PortfolioLink::setProgressCallback(PofoFileTransferProgress progress) {
  fileTransfer_.setProgressCallback(progress);
}

PofoResult PortfolioLink::list(const char* path,
                               PofoFileTransferList* response) {
  return fileTransfer_.list(path, response);
}

PofoResult PortfolioLink::receiveFile(const char* path, Stream& output) {
  return fileTransfer_.receiveFile(path, output);
}

PofoResult PortfolioLink::transmitFile(const char* path, Stream& input,
                                       size_t length, bool overwrite,
                                       time_t timestamp) {
  return fileTransfer_.transmitFile(path, input, length, overwrite,
                                    timestamp);
}

PofoResult PortfolioLink::hello(PortfolioLinkHello* response) {
  if (response == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }

  const uint8_t request[] = {kHelloCommand};
  PofoResult result = cable_.sendBlock(request, sizeof(request));
  if (result != PofoResult::OK) {
    return result;
  }

  uint8_t* payload = 0;
  size_t length = 0;
  result = cable_.receiveBlock(&payload, &length);
  if (result != PofoResult::OK) {
    return result;
  }

  if (length < kHelloResponseLength || payload[0] != 'P' ||
      payload[1] != 'F' || payload[2] != 'D' || payload[3] != '1') {
    PofoSmartCable::releaseBlock(payload);
    return PofoResult::FRAME_ERROR;
  }

  response->buildId = static_cast<uint32_t>(payload[4]) |
      (static_cast<uint32_t>(payload[5]) << 8) |
      (static_cast<uint32_t>(payload[6]) << 16) |
      (static_cast<uint32_t>(payload[7]) << 24);
  response->versionMajor = payload[8];
  response->versionMinor = payload[9];
  response->versionPatch = payload[10];

  PofoSmartCable::releaseBlock(payload);
  return PofoResult::OK;
}

PofoResult PortfolioLink::drives(uint8_t* driveCount) {
  if (driveCount == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }

  const uint8_t request[] = {kDrivesCommand};
  PofoResult result = cable_.sendBlock(request, sizeof(request));
  if (result != PofoResult::OK) {
    return result;
  }

  uint8_t* payload = 0;
  size_t length = 0;
  result = cable_.receiveBlock(&payload, &length);
  if (result != PofoResult::OK) {
    return result;
  }

  if (length < kDrivesResponseLength) {
    PofoSmartCable::releaseBlock(payload);
    return PofoResult::FRAME_ERROR;
  }

  *driveCount = payload[0];
  PofoSmartCable::releaseBlock(payload);
  return PofoResult::OK;
}
