#include "WebApi.h"

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <log4mcu.h>

#include "CableLink.h"
#include "FsUtil.h"

#define DATA_DIR "/data/"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");
static WebServer server(80);
static File fsUploadFile;

static void handleStatus() {
  const bool connected = cableLink.online();
  const bool busy = cableLink.busy();

  String out = "{";
  out += "\"status\":\""; out += (busy ? "busy" : "idle"); out += "\",";
  out += "\"connected\":"; out += (connected ? "true" : "false"); out += ",";
  out += "\"phase\":\""; out += (busy ? "pofo_upload" : "idle"); out += "\",";
  out += "\"done\":"; out += String(cableLink.transferredBytes()); out += ",";
  out += "\"total\":"; out += String(cableLink.totalBytes());
  out += "}";

  server.send(200, "application/json", out);
}

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
  const PofoResult result = cableLink.list(path.c_str(), &response);
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

      String filename = String(DATA_DIR) + upload.filename;
      const bool overwrite = server.hasArg("overwrite");
      const String pofoPath = "C:\\" + upload.filename;

      cableLink.queueTransmit(filename, pofoPath, overwrite);
      logger.infof("File %s queued for transfer to %s", filename.c_str(), pofoPath.c_str());
    }
  }
}

void webApiBegin() {
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/listESP32", HTTP_GET, handleListEsp32);
  server.on("/listAtari", HTTP_GET, handleListAtari);
  server.on("/upload", HTTP_POST, handleUpload, handleUploadData);

  server.serveStatic("/files/", LittleFS, DATA_DIR);
  server.serveStatic("/", LittleFS, "/web/");

  server.begin();
}

void webApiLoop() {
  server.handleClient();
}
