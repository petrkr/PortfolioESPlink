#include "PortfolioLink.h"

#include <stdlib.h>
#include <string.h>

#include <PofoSmartCable.h>

namespace {

const uint8_t kMkdirCommand = 0x88;
const size_t kMkdirResponseLength = 2;
const uint8_t kStatusOk = 0x20;

}  // namespace

// See ~/git/POFOSCAB/src/pftd/mkdir.inc for the wire format: request is
// [0x88, 0x00, 0x70, ASCIIZ path], response is [status, errcode] with
// status 0x20=ok/0x10=error (errcode only meaningful on error: 1=not found,
// 2=already exists, 3=disk full, 4=access denied, 0xFF=critical error).
PofoResult PortfolioLink::mkdir(const char* path, uint8_t* errCode) {
  if (path == 0 || errCode == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }
  *errCode = 0;

  const size_t pathLength = strlen(path);
  if (pathLength > 0xffffU - 4U) {
    return PofoResult::INVALID_ARGUMENT;
  }
  const size_t requestLength = 4 + pathLength;

  uint8_t* request = static_cast<uint8_t*>(malloc(requestLength));
  if (request == 0) {
    return PofoResult::OUT_OF_MEMORY;
  }
  request[0] = kMkdirCommand;
  request[1] = 0x00;
  request[2] = 0x70;
  memcpy(request + 3, path, pathLength);
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

  if (length < kMkdirResponseLength) {
    PofoSmartCable::releaseBlock(payload);
    return PofoResult::FRAME_ERROR;
  }

  if (payload[0] != kStatusOk) {
    *errCode = payload[1];
    PofoSmartCable::releaseBlock(payload);
    return PofoResult::REMOTE_ERROR;
  }

  PofoSmartCable::releaseBlock(payload);
  return PofoResult::OK;
}
