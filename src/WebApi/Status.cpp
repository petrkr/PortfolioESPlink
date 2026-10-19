#include "WebApiInternal.h"

#include <LittleFS.h>
#include <Version.h>

#include "../ClientMode.h"
#include "../OtaControl.h"

static void handleStatusAsync(AsyncWebServerRequest* request) {
  const bool connected = clientMode.online();
  const bool busy = clientMode.busy();

  String out = "{";
  out += "\"fwBuildId\":\""; out += hex32(FW_BUILD_ID); out += "\",";
  out += "\"status\":\""; out += (busy ? "busy" : "idle"); out += "\",";
  out += "\"connected\":"; out += (connected ? "true" : "false"); out += ",";
  out += "\"phase\":\""; out += (busy ? "pofo_upload" : "idle"); out += "\",";
  out += "\"done\":"; out += String(clientMode.transferredBytes()); out += ",";
  out += "\"total\":"; out += String(clientMode.totalBytes()); out += ",";
  out += "\"error\":\""; out += clientMode.lastError(); out += "\",";
  out += "\"otaEnabled\":"; out += (otaControlEnabled() ? "true" : "false"); out += ",";
  out += "\"espUsed\":"; out += String(LittleFS.usedBytes()); out += ",";
  out += "\"espTotal\":"; out += String(LittleFS.totalBytes()); out += ",";

  out += "\"pftd\":";
  if (clientMode.helloChecked() && clientMode.pftdPresent()) {
    const PortfolioLinkHello& info = clientMode.pftdInfo();
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
  request->send(200, "application/json", out);
}

void registerStatusRoutes() {
  server.on("/status", HTTP_GET, handleStatusAsync);
}
