#include "WebApiInternal.h"

#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

// POST /drawAsciiAtari?row=0-7&col=0-39&text=TEXT -> {ok,message} on
// success, {ok:false,message} on failure. Diagnostic/debug tool, not a
// file-transfer command. PFTD-only; 503 if the link is down, 404 if PFTD
// hasn't confirmed presence, 409 if the Portfolio reported failure. No
// bounds checking on row/col/text here either.
static void handleDrawAsciiAtari() {
  if (!server.hasArg("row") || !server.hasArg("col") || !server.hasArg("text")) {
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

  const uint8_t row = static_cast<uint8_t>(server.arg("row").toInt());
  const uint8_t col = static_cast<uint8_t>(server.arg("col").toInt());
  const String text = server.arg("text");
  logger.infof("drawAsciiAtari: row=%u col=%u text=%s", row, col, text.c_str());

  const PofoResult result = cableLink.portfolioLink.drawAscii(row, col, text.c_str());

  if (result == PofoResult::REMOTE_ERROR) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"draw ascii failed\"}");
    return;
  }

  if (result != PofoResult::OK) {
    logger.warnf("DRAW_ASCII failed: %u", static_cast<unsigned>(result));
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"DRAW_ASCII failed\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Drawn\"}");
}

void registerDrawAsciiRoutes() {
  server.on("/drawAsciiAtari", HTTP_POST, handleDrawAsciiAtari);
}
