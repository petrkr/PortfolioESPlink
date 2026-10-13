#include "PortfolioLink.h"

#include <stdlib.h>
#include <string.h>

#include <PofoSmartCable.h>

namespace {

const uint8_t kDrawAsciiCommand = 0x8F;
const size_t kDrawAsciiResponseLength = 2;
const uint8_t kStatusOk = 0x20;

}  // namespace

// Wire format: request is [0x8F, 0x00, 0x70, row, col, ASCIIZ text].
// Diagnostic/debug tool, not a file-transfer command - writes arbitrary
// text at an arbitrary row (0-7)/col (0-39) on the Portfolio's own LCD, no
// bounds checking on the Atari side. Response is a 2-byte status/errcode
// pair (confirmed against real hardware: status 0x20 ok, errcode 0 on
// success). An older PFTD build reportedly sent no response at all for
// this command - if that recurs, this will read as a timeout
// (PofoResult::TIMEOUT) from receiveBlock(), not distinguished here from
// any other transport failure.
PofoResult PortfolioLink::drawAscii(uint8_t row, uint8_t col, const char* text) {
  if (text == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }

  const size_t textLength = strlen(text);
  if (textLength > 0xffffU - 6U) {
    return PofoResult::INVALID_ARGUMENT;
  }
  const size_t requestLength = 5 + textLength + 1;

  uint8_t* request = static_cast<uint8_t*>(malloc(requestLength));
  if (request == 0) {
    return PofoResult::OUT_OF_MEMORY;
  }
  request[0] = kDrawAsciiCommand;
  request[1] = 0x00;
  request[2] = 0x70;
  request[3] = row;
  request[4] = col;
  memcpy(request + 5, text, textLength);
  request[requestLength - 1] = 0;

  PofoResult result = cable_.sendBlock(request, requestLength);
  free(request);
  if (result != PofoResult::OK) {
    return result;
  }

  uint8_t* payload = 0;
  size_t length = 0;
  result = cable_.receiveBlock(&payload, &length);
  if (result != PofoResult::OK) {
    return result;
  }

  if (length < kDrawAsciiResponseLength) {
    PofoSmartCable::releaseBlock(payload);
    return PofoResult::FRAME_ERROR;
  }

  if (payload[0] != kStatusOk) {
    PofoSmartCable::releaseBlock(payload);
    return PofoResult::REMOTE_ERROR;
  }

  PofoSmartCable::releaseBlock(payload);
  return PofoResult::OK;
}
