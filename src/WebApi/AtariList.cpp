#include "WebApiInternal.h"

#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

static void handleListAtari() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "text/plain", "Portfolio not connected");
    return;
  }

  String path = server.arg("dir");
  logger.infof("listAtari: %s", path.c_str());

  PofoFileTransferList response;
  const PofoResult result = cableLink.portfolioLink.list(path.c_str(), &response);
  if (result != PofoResult::OK) {
    logger.warnf("LIST failed: %u", static_cast<unsigned>(result));
    server.send(500, "text/plain", "LIST failed");
    return;
  }

  String out = "{\"files\":[";
  for (size_t i = 0; i < response.count(); i++) {
    if (i > 0) {
      out += ',';
    }
    out += "\"";
    out += response.name(i);
    out += "\"";
  }
  out += "]}";

  server.send(200, "application/json", out);
}

// GET /listAtariExt?dir=PATH -> {items:[{name,type,size,modified}],
// freeBytes,totalBytes}. PFTD-only (see PortfolioLink::listExt()); 503 if the
// link is down, 404 if PFTD hasn't confirmed presence.
static void handleListAtariExt() {
  if (!server.hasArg("dir")) {
    server.send(400, "text/plain", "BAD ARGS");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "text/plain", "Portfolio not connected");
    return;
  }

  if (!cableLink.helloChecked() || !cableLink.pftdPresent()) {
    server.send(404, "text/plain", "PFTD not present");
    return;
  }

  String path = server.arg("dir");
  logger.infof("listAtariExt: %s", path.c_str());

  PortfolioLinkListExt response;
  const PofoResult result = cableLink.portfolioLink.listExt(path.c_str(), &response);
  if (result != PofoResult::OK) {
    logger.warnf("LIST_EXT failed: %u", static_cast<unsigned>(result));
    server.send(500, "text/plain", "LIST_EXT failed");
    return;
  }

  String out = "{\"items\":[";
  for (size_t i = 0; i < response.count(); i++) {
    if (i > 0) {
      out += ',';
    }
    out += "{\"name\":\"";
    out += response.name(i);
    out += "\",\"type\":\"";
    out += response.isDirectory(i) ? "folder" : "file";
    out += "\",\"size\":"; out += String(response.size(i));
    out += ",\"modified\":\""; out += response.modified(i); out += "\"";
    out += "}";
  }
  out += "],\"freeBytes\":"; out += String(response.freeBytes());
  out += ",\"totalBytes\":"; out += String(response.totalBytes());
  out += "}";

  server.send(200, "application/json", out);
}

// GET /drives -> {drives:["A","B",...]}. PFTD-only (see PortfolioLink::drives());
// 503 if the link is down, 404 if PFTD hasn't confirmed presence.
static void handleDrives() {
  if (!cableLink.online()) {
    server.send(503, "text/plain", "Portfolio not connected");
    return;
  }

  if (!cableLink.helloChecked() || !cableLink.pftdPresent()) {
    server.send(404, "text/plain", "PFTD not present");
    return;
  }

  uint8_t driveCount = 0;
  const PofoResult result = cableLink.portfolioLink.drives(&driveCount);
  if (result != PofoResult::OK) {
    logger.warnf("DRIVES failed: %u", static_cast<unsigned>(result));
    server.send(500, "text/plain", "DRIVES failed");
    return;
  }

  String out = "{\"drives\":[";
  for (uint8_t i = 0; i < driveCount; i++) {
    if (i > 0) {
      out += ',';
    }
    out += "\"";
    out += static_cast<char>('A' + i);
    out += "\"";
  }
  out += "]}";

  server.send(200, "application/json", out);
}

void registerAtariListRoutes() {
  server.on("/listAtari", HTTP_GET, handleListAtari);
  server.on("/listAtariExt", HTTP_GET, handleListAtariExt);
  server.on("/drives", HTTP_GET, handleDrives);
}
