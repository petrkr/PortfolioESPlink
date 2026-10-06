#pragma once

#include <stddef.h>
#include <stdint.h>

#include <PofoSmartCableResult.h>

// Parsed response to the PFTD LIST_EXT command (payload[0]=0x86). See
// ~/git/POFOSCAB/src/pftd/list.inc for the wire format this parses: a
// count-prefixed list of (attr, size, date, time, name) entries, followed
// by free/total bytes on the pattern's drive.
class PortfolioLinkListExt {
 public:
  PortfolioLinkListExt();
  ~PortfolioLinkListExt();

  size_t count() const { return count_; }
  const char* name(size_t index) const;

  // Full DOS attribute byte (see list.inc: bit0=read-only, bit1=hidden,
  // bit2=system, bit4=directory, bit5=archive).
  uint8_t attr(size_t index) const;
  bool isDirectory(size_t index) const;
  uint32_t size(size_t index) const;

  // "YYYY-MM-DD HH:MM", decoded from the DOS packed date/time DTA fields.
  // Empty string if index is out of range.
  const char* modified(size_t index) const;

  uint32_t freeBytes() const { return freeBytes_; }
  uint32_t totalBytes() const { return totalBytes_; }

  void clear();

 private:
  friend class PortfolioLink;

  struct Entry {
    uint8_t attr;
    uint32_t size;
    uint16_t date;
    uint16_t time;
    const char* name;
    char modified[17];  // "YYYY-MM-DD HH:MM\0"
  };

  // Takes ownership of payload (must have come from
  // PofoSmartCable::receiveBlock()); frees it on failure or in clear()/the
  // destructor.
  PofoResult take(uint8_t* payload, size_t length);

  PortfolioLinkListExt(const PortfolioLinkListExt&);
  PortfolioLinkListExt& operator=(const PortfolioLinkListExt&);

  uint8_t* payload_;
  Entry* entries_;
  size_t count_;
  uint32_t freeBytes_;
  uint32_t totalBytes_;
};
