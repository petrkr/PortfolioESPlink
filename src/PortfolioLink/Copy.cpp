#include "PortfolioLink.h"

#include <stdlib.h>
#include <string.h>

#include <PofoSmartCable.h>

namespace {

const uint8_t kCopyCommand = 0x8C;
const size_t kCopyResponseLength = 2;
const uint8_t kStatusOk = 0x20;

}  // namespace

// See ~/git/POFOSCAB/src/pftd/copy.inc for the wire format: request is
// [0x8C, 0x00, 0x70, ASCIIZ source path, ASCIIZ destination path] (two
// consecutive NUL-terminated strings, identical shape to RENAME's request),
// response is [status, errcode] with status 0x20=ok/0x10=error (errcode
// only meaningful on error: 1=not found, 3=disk full, 4=access denied,
// 0xFF=critical error). Files only, no directory recursion. Unlike
// rename(), this works cross-drive - it's a real data copy, not a
// directory-entry rewrite. The destination is always overwritten if it
// exists.
PofoResult PortfolioLink::copy(const char* srcPath, const char* dstPath,
                               uint8_t* errCode) {
  if (srcPath == 0 || dstPath == 0 || errCode == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }
  *errCode = 0;

  const size_t srcLength = strlen(srcPath);
  const size_t dstLength = strlen(dstPath);
  if (srcLength > 0xffffU - 5U - dstLength) {
    return PofoResult::INVALID_ARGUMENT;
  }
  const size_t requestLength = 3 + srcLength + 1 + dstLength + 1;

  uint8_t* request = static_cast<uint8_t*>(malloc(requestLength));
  if (request == 0) {
    return PofoResult::OUT_OF_MEMORY;
  }
  request[0] = kCopyCommand;
  request[1] = 0x00;
  request[2] = 0x70;
  memcpy(request + 3, srcPath, srcLength);
  request[3 + srcLength] = 0;
  memcpy(request + 3 + srcLength + 1, dstPath, dstLength);
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

  if (length < kCopyResponseLength) {
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
