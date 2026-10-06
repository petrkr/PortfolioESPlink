#include "WebApiInternal.h"

#include <log4mcu.h>

#include <PofoSmartCable.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

namespace {

const char kHexDigits[] = "0123456789ABCDEF";

String toHex(const uint8_t* data, size_t length) {
  String out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    out += kHexDigits[(data[i] >> 4) & 0x0F];
    out += kHexDigits[data[i] & 0x0F];
  }
  return out;
}

}  // namespace

// POST /sendRaw?data=<hex> -> {ok,response:"<hex>"} on success,
// {ok:false,message} on failure. Sends the given bytes as a single block
// straight to the cable and returns whatever comes back, unparsed - a
// protocol-level debug/probing escape hatch (see PortfolioLink::sendRaw()).
// Works regardless of whether PFTD is present - it's the caller's job to
// build a valid request; only requires the link itself to be online.
static void handleSendRaw() {
  if (!server.hasArg("data")) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  const String hex = server.arg("data");
  if (hex.length() % 2 != 0) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"hex data must have even length\"}");
    return;
  }

  const size_t length = hex.length() / 2;
  uint8_t* request = static_cast<uint8_t*>(malloc(length));
  if (length > 0 && request == 0) {
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"OOM\"}");
    return;
  }
  for (size_t i = 0; i < length; i++) {
    request[i] = static_cast<uint8_t>(strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), 0, 16));
  }

  logger.infof("sendRaw: %u bytes", static_cast<unsigned>(length));

  uint8_t* responsePayload = 0;
  size_t responseLength = 0;
  const PofoResult result = cableLink.portfolioLink.sendRaw(request, length, &responsePayload, &responseLength);
  free(request);

  if (result != PofoResult::OK) {
    logger.warnf("sendRaw failed: %u", static_cast<unsigned>(result));
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"sendRaw failed\"}");
    return;
  }

  String out = "{\"ok\":true,\"response\":\"";
  out += toHex(responsePayload, responseLength);
  out += "\"}";
  PofoSmartCable::releaseBlock(responsePayload);

  server.send(200, "application/json", out);
}

void registerSendRawRoutes() {
  server.on("/sendRaw", HTTP_POST, handleSendRaw);
}
