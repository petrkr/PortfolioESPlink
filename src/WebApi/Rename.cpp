#include "WebApiInternal.h"

#include <log4mcu.h>

#include "../ClientMode.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

// POST /renameAtari?oldPath=<Portfolio full path>&newPath=<Portfolio full
// path> -> {ok,message} on success, {ok:false,message,errcode?} on failure.
// PFTD-only; 503 if the link is down, 404 if PFTD hasn't confirmed
// presence, 409 if the Portfolio rejected the request (errcode is the PFTD
// error code in that case). No frontend wiring yet - index.htm's rename
// button is still disabled.
static void handleRenameAtari(AsyncWebServerRequest* request) {
  if (!request->hasArg("oldPath") || !request->hasArg("newPath")) {
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  if (!clientMode.online()) {
    request->send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  if (!clientMode.helloChecked() || !clientMode.pftdPresent()) {
    request->send(404, "application/json", "{\"ok\":false,\"message\":\"PFTD not present\"}");
    return;
  }

  const String oldPath = request->arg("oldPath");
  const String newPath = request->arg("newPath");
  logger.infof("renameAtari: %s -> %s", oldPath.c_str(), newPath.c_str());

  uint8_t errCode = 0;
  const PofoResult result = clientMode.portfolioLink.rename(oldPath.c_str(), newPath.c_str(), &errCode);

  if (result == PofoResult::REMOTE_ERROR) {
    String out = "{\"ok\":false,\"message\":\"rename failed\",\"errcode\":";
    out += errCode;
    out += "}";
    request->send(409, "application/json", out);
    return;
  }

  if (result != PofoResult::OK) {
    logger.warnf("RENAME failed: %u", static_cast<unsigned>(result));
    request->send(500, "application/json", "{\"ok\":false,\"message\":\"RENAME failed\"}");
    return;
  }

  request->send(200, "application/json", "{\"ok\":true,\"message\":\"Renamed\"}");
}

void registerRenameRoutes() {
  server.on("/renameAtari", HTTP_POST, handleRenameAtari);
}
