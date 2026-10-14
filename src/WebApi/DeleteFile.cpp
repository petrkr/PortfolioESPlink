#include "WebApiInternal.h"

#include <LittleFS.h>
#include <log4mcu.h>

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

// POST /deleteFile?path=ESP:\... -> {ok,message} on success,
// {ok:false,message} on failure. File only, same as /deleteAtari. ESP:\ is
// the only prefix handled for now; other drive letters (A:, C:, ...) will
// route to the Atari side (same as /deleteAtari) once this becomes the
// single delete endpoint for both.
static void handleDeleteFile(AsyncWebServerRequest* request) {
  if (!request->hasArg("path")) {
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  String path = request->arg("path");
  if (!path.startsWith("ESP:\\")) {
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"Only ESP:\\\\ paths are supported\"}");
    return;
  }

  String localPath = String(DATA_DIR) + path.substring(5);
  localPath.replace("\\", "/");
  localPath.replace("//", "/");

  logger.infof("deleteFile: %s", localPath.c_str());

  if (!LittleFS.exists(localPath)) {
    request->send(404, "application/json", "{\"ok\":false,\"message\":\"File not found\"}");
    return;
  }

  if (!LittleFS.remove(localPath)) {
    logger.warnf("deleteFile failed: %s", localPath.c_str());
    request->send(500, "application/json", "{\"ok\":false,\"message\":\"Delete failed\"}");
    return;
  }

  request->send(200, "application/json", "{\"ok\":true,\"message\":\"File deleted\"}");
}

void registerDeleteFileRoutes() {
  server.on("/deleteFile", HTTP_POST, handleDeleteFile);
}
