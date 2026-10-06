#include "WebApiInternal.h"

#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

// POST /copyAtari?srcPath=<Portfolio full path>&dstPath=<Portfolio full
// path> -> {ok,message} on success, {ok:false,message,errcode?} on failure.
// PFTD-only (see PortfolioLink::copy()); 503 if the link is down, 404 if
// PFTD hasn't confirmed presence, 409 if the Portfolio rejected the request
// (errcode is the PFTD error code in that case). Blocks for the whole copy
// duration - same tradeoff as every other synchronous PFTD command here,
// but likely to be noticeably longer for large files.
static void handleCopyAtari() {
  if (!server.hasArg("srcPath") || !server.hasArg("dstPath")) {
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

  const String srcPath = server.arg("srcPath");
  const String dstPath = server.arg("dstPath");
  logger.infof("copyAtari: %s -> %s", srcPath.c_str(), dstPath.c_str());

  uint8_t errCode = 0;
  const PofoResult result = cableLink.portfolioLink.copy(srcPath.c_str(), dstPath.c_str(), &errCode);

  if (result == PofoResult::REMOTE_ERROR) {
    String out = "{\"ok\":false,\"message\":\"copy failed\",\"errcode\":";
    out += errCode;
    out += "}";
    server.send(409, "application/json", out);
    return;
  }

  if (result != PofoResult::OK) {
    logger.warnf("COPY failed: %u", static_cast<unsigned>(result));
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"COPY failed\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Copied\"}");
}

void registerCopyRoutes() {
  server.on("/copyAtari", HTTP_POST, handleCopyAtari);
}
