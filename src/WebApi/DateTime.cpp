#include "WebApiInternal.h"

#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

namespace {

bool parseDecimal(const String& value, size_t offset, size_t length, uint8_t* output) {
  if (offset + length > value.length()) {
    return false;
  }

  uint8_t parsed = 0;
  for (size_t i = 0; i < length; ++i) {
    char c = value.charAt(offset + i);
    if (c < '0' || c > '9') {
      return false;
    }
    parsed = static_cast<uint8_t>(parsed * 10 + c - '0');
  }
  *output = parsed;
  return true;
}

// Parses "YYYY-MM-DDTHH:MM:SS" (ISO 8601, no timezone offset - DOS packed
// date/time has no timezone concept either) into packed DOS fields.
bool parseIsoDateTime(const String& value, uint16_t* dosDate, uint16_t* dosTime) {
  if (value.length() != 19 || value.charAt(4) != '-' || value.charAt(7) != '-' ||
      value.charAt(10) != 'T' || value.charAt(13) != ':' || value.charAt(16) != ':') {
    return false;
  }

  uint8_t yearHi = 0;
  uint8_t yearLo = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  if (!parseDecimal(value, 0, 2, &yearHi) || !parseDecimal(value, 2, 2, &yearLo) ||
      !parseDecimal(value, 5, 2, &month) || !parseDecimal(value, 8, 2, &day) ||
      !parseDecimal(value, 11, 2, &hour) || !parseDecimal(value, 14, 2, &minute) ||
      !parseDecimal(value, 17, 2, &second)) {
    return false;
  }

  const uint16_t year = static_cast<uint16_t>(yearHi) * 100 + yearLo;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  static const uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t maxDay = month >= 1 && month <= 12 ? daysPerMonth[month - 1] : 0;
  if (month == 2 && leap) {
    maxDay = 29;
  }
  if (year < 1980 || year > 2107 || day < 1 || day > maxDay || hour > 23 || minute > 59 || second > 59) {
    return false;
  }

  *dosDate = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
  *dosTime = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
  return true;
}

// Formats packed DOS date/time fields as "YYYY-MM-DDTHH:MM:SS" (ISO 8601,
// no timezone offset).
String formatIsoDateTime(uint16_t dosDate, uint16_t dosTime) {
  const uint16_t year = 1980 + (dosDate >> 9);
  const uint8_t month = (dosDate >> 5) & 0x0F;
  const uint8_t day = dosDate & 0x1F;
  const uint8_t hour = dosTime >> 11;
  const uint8_t minute = (dosTime >> 5) & 0x3F;
  const uint8_t second = (dosTime & 0x1F) * 2;
  char buf[20];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u", year, month, day, hour, minute, second);
  return String(buf);
}

}  // namespace

// GET /datetimeAtari -> {datetime:"YYYY-MM-DDTHH:MM:SS"}. PFTD-only (see
// PortfolioLink::getDateTime()); 503 if the link is down, 404 if PFTD
// hasn't confirmed presence.
static void handleGetDatetimeAtari() {
  if (!cableLink.online()) {
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  if (!cableLink.helloChecked() || !cableLink.pftdPresent()) {
    server.send(404, "application/json", "{\"ok\":false,\"message\":\"PFTD not present\"}");
    return;
  }

  uint16_t dosDate = 0;
  uint16_t dosTime = 0;
  const PofoResult result = cableLink.portfolioLink.getDateTime(&dosDate, &dosTime);
  if (result != PofoResult::OK) {
    logger.warnf("GETDATETIME failed: %u", static_cast<unsigned>(result));
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"GETDATETIME failed\"}");
    return;
  }

  String out = "{\"ok\":true,\"datetime\":\"";
  out += formatIsoDateTime(dosDate, dosTime);
  out += "\"}";
  server.send(200, "application/json", out);
}

// POST /datetimeAtari?datetime=YYYY-MM-DDTHH:MM:SS -> {ok,datetime} on
// success, {ok:false,message,errcode?} on failure. PFTD-only (see
// PortfolioLink::setDateTime()); 503 if the link is down, 404 if PFTD
// hasn't confirmed presence, 409 if the Portfolio rejected the request
// (errcode is the PFTD error code in that case).
static void handleSetDatetimeAtari() {
  if (!server.hasArg("datetime")) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  if (!cableLink.helloChecked() || !cableLink.pftdPresent()) {
    server.send(404, "application/json", "{\"ok\":false,\"message\":\"PFTD not present\"}");
    return;
  }

  uint16_t dosDate = 0;
  uint16_t dosTime = 0;
  if (!parseIsoDateTime(server.arg("datetime"), &dosDate, &dosTime)) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Invalid datetime\"}");
    return;
  }

  logger.infof("setDatetimeAtari: %s", server.arg("datetime").c_str());

  uint8_t errCode = 0;
  const PofoResult result = cableLink.portfolioLink.setDateTime(dosDate, dosTime, &errCode);

  if (result == PofoResult::REMOTE_ERROR) {
    String out = "{\"ok\":false,\"message\":\"set datetime failed\",\"errcode\":";
    out += errCode;
    out += "}";
    server.send(409, "application/json", out);
    return;
  }

  if (result != PofoResult::OK) {
    logger.warnf("SETDATETIME failed: %u", static_cast<unsigned>(result));
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"SETDATETIME failed\"}");
    return;
  }

  String out = "{\"ok\":true,\"datetime\":\"";
  out += formatIsoDateTime(dosDate, dosTime);
  out += "\"}";
  server.send(200, "application/json", out);
}

void registerDateTimeRoutes() {
  server.on("/datetimeAtari", HTTP_GET, handleGetDatetimeAtari);
  server.on("/datetimeAtari", HTTP_POST, handleSetDatetimeAtari);
}
