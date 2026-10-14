#include "WebApiInternal.h"

#include <LittleFS.h>
#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

static const uint32_t kTransferWaitTimeoutMs = 60000;

static bool waitForTransferDone() {
  const uint32_t startedAt = millis();
  do {
    if (millis() - startedAt > kTransferWaitTimeoutMs) {
      logger.error("Transfer wait timed out");
      return false;
    }
    cableLink.loop();
    delay(1);
  } while (cableLink.busy());
  return true;
}

static void sendTransferResult(bool ok, const String& errorMessage) {
  if (ok) {
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }

  String out = "{\"ok\":false,\"message\":\"";
  out += errorMessage;
  out += "\"}";
  server.send(500, "application/json", out);
}

static bool uploadToAtariRequested = false;
static String uploadLocalPath;
static String uploadPofoPath;
static bool uploadOverwrite = false;

static void handleUpload() {
  if (!uploadToAtariRequested) {
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }
  uploadToAtariRequested = false;

  if (!cableLink.online()) {
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  if (!cableLink.requestTransmit(uploadLocalPath, uploadPofoPath, uploadOverwrite)) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Transfer already in progress\"}");
    return;
  }

  const bool done = waitForTransferDone();
  sendTransferResult(done && cableLink.lastError().isEmpty(), cableLink.lastError());
}

static void handleUploadData() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = String(DATA_DIR) + upload.filename;

    logger.infof("Upload start: %s", filename.c_str());
    fsUploadFile = LittleFS.open(filename, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();

      if (server.hasArg("toAtari")) {
        uploadLocalPath = String(DATA_DIR) + upload.filename;
        uploadOverwrite = server.hasArg("overwrite");
        String destDir = server.hasArg("destDir") ? server.arg("destDir") : "C:\\";
        uploadPofoPath = destDir + upload.filename;
        uploadToAtariRequested = true;
      }
    }
  }
}

static void handleSendToAtari() {
  if (!server.hasArg("path")) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  String localPath = String(DATA_DIR) + server.arg("path");
  localPath.replace("//", "/");

  const bool overwrite = server.hasArg("overwrite");
  const String destDir = server.hasArg("destDir") ? server.arg("destDir") : "C:\\";

  int lastSlash = localPath.lastIndexOf('/');
  const String basename = lastSlash >= 0 ? localPath.substring(lastSlash + 1) : localPath;
  const String pofoPath = destDir + basename;

  if (!cableLink.requestTransmit(localPath, pofoPath, overwrite)) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Transfer already in progress\"}");
    return;
  }

  const bool done = waitForTransferDone();
  sendTransferResult(done && cableLink.lastError().isEmpty(), cableLink.lastError());
}

static void handleDownloadFile() {
  if (!server.hasArg("path")) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  const String pofoPath = server.arg("path");
  const String localPath = String(DATA_DIR) + pofoBasename(pofoPath);

  if (!cableLink.requestReceive(pofoPath, localPath, /*overwrite=*/true)) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Transfer already in progress\"}");
    return;
  }

  const bool done = waitForTransferDone();
  if (!done || !cableLink.lastError().isEmpty()) {
    if (cableLink.lastResult() == PofoResult::REMOTE_ERROR) {
      String out = "{\"ok\":false,\"message\":\"File not found\"}";
      server.send(404, "application/json", out);
    } else {
      sendTransferResult(false, cableLink.lastError());
    }
    return;
  }

  File file = LittleFS.open(localPath, "r");
  if (!file) {
    sendTransferResult(false, "Downloaded file missing on ESP32");
    return;
  }

  server.sendHeader("Content-Disposition",
                     "attachment; filename=\"" + pofoBasename(pofoPath) + "\"");
  server.streamFile(file, "application/octet-stream");
  file.close();
}

void registerTransferRoutes() {
  server.on("/uploadFile", HTTP_POST, handleUpload, handleUploadData);
  server.on("/sendToAtari", HTTP_POST, handleSendToAtari);
  server.on("/downloadFile", HTTP_POST, handleDownloadFile);
}
