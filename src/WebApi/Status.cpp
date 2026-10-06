#include "WebApiInternal.h"

#include "../CableLink.h"
#include "../OtaControl.h"

static void handleStatus() {
  const bool connected = cableLink.online();
  const bool busy = cableLink.busy();

  String out = "{";
  out += "\"status\":\""; out += (busy ? "busy" : "idle"); out += "\",";
  out += "\"connected\":"; out += (connected ? "true" : "false"); out += ",";
  out += "\"phase\":\""; out += (busy ? "pofo_upload" : "idle"); out += "\",";
  out += "\"done\":"; out += String(cableLink.transferredBytes()); out += ",";
  out += "\"total\":"; out += String(cableLink.totalBytes()); out += ",";
  out += "\"error\":\""; out += cableLink.lastError(); out += "\",";
  out += "\"otaEnabled\":"; out += (otaControlEnabled() ? "true" : "false"); out += ",";

  out += "\"pftd\":";
  if (cableLink.helloChecked() && cableLink.pftdPresent()) {
    const PortfolioLinkHello& info = cableLink.pftdInfo();
    out += "{\"buildId\":\""; out += hex32(info.buildId); out += "\",";
    out += "\"version\":{";
    out += "\"major\":"; out += String(info.versionMajor); out += ",";
    out += "\"minor\":"; out += String(info.versionMinor); out += ",";
    out += "\"patch\":"; out += String(info.versionPatch);
    out += "}}";
  } else {
    out += "null";
  }

  out += "}";

  server.send(200, "application/json", out);
}

void registerStatusRoutes() {
  server.on("/status", HTTP_GET, handleStatus);
}
