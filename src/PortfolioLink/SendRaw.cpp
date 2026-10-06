#include "PortfolioLink.h"

#include <PofoSmartCable.h>

// Sends an arbitrary pre-built block and returns whatever comes back,
// unparsed - a protocol-level escape hatch for probing/debugging commands
// that don't have (or don't yet have) their own typed method here. Caller
// owns the data it passes in; *responsePayload is caller-owned on return
// too (release with PofoSmartCable::releaseBlock()), same convention as
// PofoSmartCable::receiveBlock() itself.
PofoResult PortfolioLink::sendRaw(const uint8_t* data, size_t length,
                                  uint8_t** responsePayload, size_t* responseLength) {
  if (data == 0 || responsePayload == 0 || responseLength == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }
  *responsePayload = 0;
  *responseLength = 0;

  PofoResult result = cable_.sendBlock(data, length);
  if (result != PofoResult::OK) {
    return result;
  }

  return cable_.receiveBlock(responsePayload, responseLength);
}
