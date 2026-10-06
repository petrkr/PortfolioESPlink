#include "WebApiInternal.h"

#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

// POST /rmdirAtari?path=<Portfolio full path> -> {ok,message} on success,
// {ok:false,message,errcode?} on failure. PFTD-only (see
// PortfolioLink::rmdir()); 503 if the link is down, 404 if PFTD hasn't
// confirmed presence, 409 if the Portfolio rejected the request (errcode is
// the PFTD error code in that case).
static void handleRmdirAtari() {
  if (!server.hasArg("path")) {
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

  const String path = server.arg("path");
  logger.infof("rmdirAtari: %s", path.c_str());

  uint8_t errCode = 0;
  const PofoResult result = cableLink.portfolioLink.rmdir(path.c_str(), &errCode);

  if (result == PofoResult::REMOTE_ERROR) {
    String out = "{\"ok\":false,\"message\":\"rmdir failed\",\"errcode\":";
    out += errCode;
    out += "}";
    server.send(409, "application/json", out);
    return;
  }

  if (result != PofoResult::OK) {
    logger.warnf("RMDIR failed: %u", static_cast<unsigned>(result));
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"RMDIR failed\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Directory removed\"}");
}

void registerRmdirRoutes() {
  server.on("/rmdirAtari", HTTP_POST, handleRmdirAtari);
}
