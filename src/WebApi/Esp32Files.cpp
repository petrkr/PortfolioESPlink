#include "WebApiInternal.h"

#include "../FsUtil.h"

static void handleListEsp32() {
  String dir = server.hasArg("dir") ? server.arg("dir") : "/";

  // Web-facing paths are rooted at DATA_DIR, not the LittleFS root (which also
  // holds /web).
  String fsDir = String(DATA_DIR) + dir;
  fsDir.replace("//", "/");

  String out;
  if (!listEspDirJson(fsDir, out)) {
    server.send(404, "text/plain", "Directory not found");
    return;
  }

  server.send(200, "application/json", out);
}

void registerEsp32FileRoutes() {
  server.on("/listESP32", HTTP_GET, handleListEsp32);
}
