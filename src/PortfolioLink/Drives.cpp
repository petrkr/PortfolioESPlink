#include "PortfolioLink.h"

#include <PofoSmartCable.h>

namespace {

const uint8_t kDrivesCommand = 0x87;
const size_t kDrivesResponseLength = 1;

}  // namespace

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
