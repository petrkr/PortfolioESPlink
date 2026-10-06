#include "PortfolioLink.h"

#include <PofoSmartCable.h>

namespace {

const uint8_t kHelloCommand = 0x80;
const size_t kHelloResponseLength = 12;

}  // namespace

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
