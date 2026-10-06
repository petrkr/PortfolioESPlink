#include "PortfolioLink.h"

#include <stdlib.h>
#include <string.h>

#include <PofoSmartCable.h>

namespace {

const uint8_t kRenameCommand = 0x8B;
const size_t kRenameResponseLength = 2;
const uint8_t kStatusOk = 0x20;

}  // namespace

// See ~/git/POFOSCAB/src/pftd/rename.inc for the wire format: request is
// [0x8B, 0x00, 0x70, ASCIIZ old path, ASCIIZ new path] (two consecutive
// NUL-terminated strings), response is [status, errcode] with status
// 0x20=ok/0x10=error (errcode only meaningful on error: 1=not found,
// 4=access denied (also covers "destination already exists" and cross-drive
// rename), 0xFF=critical error).
PofoResult PortfolioLink::rename(const char* oldPath, const char* newPath,
                                 uint8_t* errCode) {
  if (oldPath == 0 || newPath == 0 || errCode == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }
  *errCode = 0;

  const size_t oldLength = strlen(oldPath);
  const size_t newLength = strlen(newPath);
  if (oldLength > 0xffffU - 5U - newLength) {
    return PofoResult::INVALID_ARGUMENT;
  }
  const size_t requestLength = 3 + oldLength + 1 + newLength + 1;

  uint8_t* request = static_cast<uint8_t*>(malloc(requestLength));
  if (request == 0) {
    return PofoResult::OUT_OF_MEMORY;
  }
  request[0] = kRenameCommand;
  request[1] = 0x00;
  request[2] = 0x70;
  memcpy(request + 3, oldPath, oldLength);
  request[3 + oldLength] = 0;
  memcpy(request + 3 + oldLength + 1, newPath, newLength);
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

  if (length < kRenameResponseLength) {
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
