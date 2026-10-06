#include "WebApiInternal.h"

#include <LittleFS.h>
#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

static void handleUpload() {
  server.send(202, "text/plain", "");
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
        String filename = String(DATA_DIR) + upload.filename;
        const bool overwrite = server.hasArg("overwrite");
        String destDir = server.hasArg("destDir") ? server.arg("destDir") : "C:\\";
        const String pofoPath = destDir + upload.filename;

        if (cableLink.requestTransmit(filename, pofoPath, overwrite)) {
          logger.infof("File %s queued for transfer to %s", filename.c_str(), pofoPath.c_str());
        } else {
          logger.warn("Transfer already in progress");
        }
      }
    }
  }
}

// POST /sendToAtari?path=<ESP32 /data-relative path>&overwrite=1&destDir=<Portfolio dir prefix>
static void handleSendToAtari() {
  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "BAD ARGS");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "text/plain", "Portfolio not connected");
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
    server.send(409, "text/plain", "Transfer already in progress");
    return;
  }

  server.send(202, "text/plain", "");
}

// POST /downloadFromAtari?path=<Portfolio full path>&overwrite=1
static void handleDownloadFromAtari() {
  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "BAD ARGS");
    return;
  }

  if (!cableLink.online()) {
    server.send(503, "text/plain", "Portfolio not connected");
    return;
  }

  const String pofoPath = server.arg("path");
  const bool overwrite = server.hasArg("overwrite");
  const String localPath = String(DATA_DIR) + pofoBasename(pofoPath);

  if (!cableLink.requestReceive(pofoPath, localPath, overwrite)) {
    server.send(409, "text/plain", "Transfer already in progress, or file exists");
    return;
  }

  server.send(202, "text/plain", "");
}

void registerTransferRoutes() {
  server.on("/upload", HTTP_POST, handleUpload, handleUploadData);
  server.on("/sendToAtari", HTTP_POST, handleSendToAtari);
  server.on("/downloadFromAtari", HTTP_POST, handleDownloadFromAtari);
}
