#include "PortfolioLink.h"

#include <PofoSmartCable.h>

namespace {

const uint8_t kGetDateTimeCommand = 0x8D;
const uint8_t kSetDateTimeCommand = 0x8E;
const size_t kGetDateTimeResponseLength = 4;
const size_t kSetDateTimeResponseLength = 2;
const uint8_t kStatusOk = 0x20;

}  // namespace

// See ~/git/POFOSCAB/src/pftd/datetime.inc for the wire format: request is
// a single byte (payload[0]=0x8D, no path - same shape as DRIVES/HELLO),
// response is a fixed 4 bytes: packed date(2B) + packed time(2B), no
// status/errcode byte (AH=0x2A/AH=0x2C have no documented failure mode).
// Packed layout: date bits 15-9=year-1980/8-5=month/4-0=day, time bits
// 15-11=hour/10-5=minute/4-0=second/2.
PofoResult PortfolioLink::getDateTime(uint16_t* dosDate, uint16_t* dosTime) {
  if (dosDate == 0 || dosTime == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }

  const uint8_t request[] = {kGetDateTimeCommand};
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

  if (length < kGetDateTimeResponseLength) {
    PofoSmartCable::releaseBlock(payload);
    return PofoResult::FRAME_ERROR;
  }

  *dosDate = static_cast<uint16_t>(payload[0]) | (static_cast<uint16_t>(payload[1]) << 8);
  *dosTime = static_cast<uint16_t>(payload[2]) | (static_cast<uint16_t>(payload[3]) << 8);
  PofoSmartCable::releaseBlock(payload);
  return PofoResult::OK;
}

// Request is [0x8E, 0x00, 0x70, packed date(2B), packed time(2B)] - a
// fixed-size binary payload, not an ASCIIZ string. Response (fixed 2
// bytes), same status/errcode shape as mkdir()/deleteFile()/rmdir()/
// rename()/copy(): status 0x20=ok/0x10=error, errcode 4=access denied
// (out-of-range date or time value), 0xFF=critical error.
PofoResult PortfolioLink::setDateTime(uint16_t dosDate, uint16_t dosTime, uint8_t* errCode) {
  if (errCode == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }
  *errCode = 0;

  const uint8_t request[] = {
      kSetDateTimeCommand,
      0x00,
      0x70,
      static_cast<uint8_t>(dosDate & 0xFF),
      static_cast<uint8_t>(dosDate >> 8),
      static_cast<uint8_t>(dosTime & 0xFF),
      static_cast<uint8_t>(dosTime >> 8),
  };
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

  if (length < kSetDateTimeResponseLength) {
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
