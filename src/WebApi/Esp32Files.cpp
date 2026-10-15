#include "WebApiInternal.h"

#include "../FsUtil.h"

static void handleListEsp32(AsyncWebServerRequest* request) {
  String dir = request->hasArg("dir") ? request->arg("dir") : "/";

  // Web-facing paths are rooted at DATA_DIR, not the LittleFS root (which also
  // holds /web).
  String fsDir = String(DATA_DIR) + dir;
  fsDir.replace("//", "/");

  String out;
  if (!listEspDirJson(fsDir, out)) {
    request->send(404, "application/json", "{\"ok\":false,\"message\":\"Directory not found\"}");
    return;
  }

  request->send(200, "application/json", out);
}

void registerEsp32FileRoutes() {
  server.on("/listESP32", HTTP_GET, handleListEsp32);
}
