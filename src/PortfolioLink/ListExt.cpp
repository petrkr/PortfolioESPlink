#include "ListExt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PofoSmartCable.h>

#include "PortfolioLink.h"

namespace {

const uint8_t kListExtCommand = 0x86;

const size_t kCountFieldLength = 2;
const size_t kTrailerLength = 8;  // free bytes (4) + total bytes (4)
const size_t kAttrOffset = 0;
const size_t kSizeOffset = 1;
const size_t kDateOffset = 5;
const size_t kTimeOffset = 7;
const size_t kNameOffset = 9;
const size_t kFixedEntryFields = kNameOffset;  // bytes before the ASCIIZ name

void formatModified(uint16_t date, uint16_t time, char* out) {
  const unsigned year = 1980 + (date >> 9);
  const unsigned month = (date >> 5) & 0x0f;
  const unsigned day = date & 0x1f;
  const unsigned hour = time >> 11;
  const unsigned minute = (time >> 5) & 0x3f;
  snprintf(out, 17, "%04u-%02u-%02u %02u:%02u", year, month, day, hour,
           minute);
}

}  // namespace

PortfolioLinkListExt::PortfolioLinkListExt()
    : payload_(0), entries_(0), count_(0), freeBytes_(0), totalBytes_(0) {}

PortfolioLinkListExt::~PortfolioLinkListExt() {
  clear();
}

void PortfolioLinkListExt::clear() {
  free(entries_);
  free(payload_);
  payload_ = 0;
  entries_ = 0;
  count_ = 0;
  freeBytes_ = 0;
  totalBytes_ = 0;
}

const char* PortfolioLinkListExt::name(size_t index) const {
  return index < count_ ? entries_[index].name : "";
}

uint8_t PortfolioLinkListExt::attr(size_t index) const {
  return index < count_ ? entries_[index].attr : 0;
}

bool PortfolioLinkListExt::isDirectory(size_t index) const {
  return (attr(index) & 0x10) != 0;
}

uint32_t PortfolioLinkListExt::size(size_t index) const {
  return index < count_ ? entries_[index].size : 0;
}

const char* PortfolioLinkListExt::modified(size_t index) const {
  return index < count_ ? entries_[index].modified : "";
}

PofoResult PortfolioLinkListExt::take(uint8_t* payload, size_t length) {
  clear();
  if (payload == 0 || length < kCountFieldLength) {
    free(payload);
    return PofoResult::FRAME_ERROR;
  }

  const size_t count = static_cast<size_t>(payload[0]) |
      (static_cast<size_t>(payload[1]) << 8);

  Entry* entries = 0;
  if (count != 0) {
    if (count > static_cast<size_t>(-1) / sizeof(*entries)) {
      free(payload);
      return PofoResult::OUT_OF_MEMORY;
    }
    entries = static_cast<Entry*>(malloc(count * sizeof(*entries)));
    if (entries == 0) {
      free(payload);
      return PofoResult::OUT_OF_MEMORY;
    }
  }

  size_t offset = kCountFieldLength;
  for (size_t index = 0; index < count; ++index) {
    if (offset + kFixedEntryFields > length) {
      free(entries);
      free(payload);
      return PofoResult::FRAME_ERROR;
    }

    Entry& entry = entries[index];
    entry.attr = payload[offset + kAttrOffset];
    entry.size = static_cast<uint32_t>(payload[offset + kSizeOffset]) |
        (static_cast<uint32_t>(payload[offset + kSizeOffset + 1]) << 8) |
        (static_cast<uint32_t>(payload[offset + kSizeOffset + 2]) << 16) |
        (static_cast<uint32_t>(payload[offset + kSizeOffset + 3]) << 24);
    entry.date = static_cast<uint16_t>(payload[offset + kDateOffset]) |
        (static_cast<uint16_t>(payload[offset + kDateOffset + 1]) << 8);
    entry.time = static_cast<uint16_t>(payload[offset + kTimeOffset]) |
        (static_cast<uint16_t>(payload[offset + kTimeOffset + 1]) << 8);
    formatModified(entry.date, entry.time, entry.modified);

    entry.name = reinterpret_cast<const char*>(payload + offset + kNameOffset);
    offset += kFixedEntryFields;
    while (offset < length && payload[offset] != 0) {
      ++offset;
    }
    if (offset == length) {
      free(entries);
      free(payload);
      return PofoResult::FRAME_ERROR;
    }
    ++offset;
  }

  if (offset + kTrailerLength > length) {
    free(entries);
    free(payload);
    return PofoResult::FRAME_ERROR;
  }

  freeBytes_ = static_cast<uint32_t>(payload[offset]) |
      (static_cast<uint32_t>(payload[offset + 1]) << 8) |
      (static_cast<uint32_t>(payload[offset + 2]) << 16) |
      (static_cast<uint32_t>(payload[offset + 3]) << 24);
  totalBytes_ = static_cast<uint32_t>(payload[offset + 4]) |
      (static_cast<uint32_t>(payload[offset + 5]) << 8) |
      (static_cast<uint32_t>(payload[offset + 6]) << 16) |
      (static_cast<uint32_t>(payload[offset + 7]) << 24);

  payload_ = payload;
  entries_ = entries;
  count_ = count;
  return PofoResult::OK;
}

PofoResult PortfolioLink::listExt(const char* path,
                                  PortfolioLinkListExt* response) {
  if (path == 0 || response == 0) {
    return PofoResult::INVALID_ARGUMENT;
  }
  response->clear();

  const size_t pathLength = strlen(path);
  if (pathLength > 0xffffU - 4U) {
    return PofoResult::INVALID_ARGUMENT;
  }
  const size_t requestLength = 4 + pathLength;

  uint8_t* request = static_cast<uint8_t*>(malloc(requestLength));
  if (request == 0) {
    return PofoResult::OUT_OF_MEMORY;
  }
  request[0] = kListExtCommand;
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
  return response->take(payload, length);
}
