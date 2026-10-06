#include "WebApi.h"

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <log4mcu.h>
#include <ElegantOTA.h>

#include "CableLink.h"
#include "FsUtil.h"
#include "OtaControl.h"

#define DATA_DIR "/data/"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");
static WebServer server(80);
static File fsUploadFile;

static String hex32(uint32_t value) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08lx", static_cast<unsigned long>(value));
  return String(buf);
}

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

// Derives the Portfolio basename from a DOS path (last segment after '\\' or ':').
static String pofoBasename(const String& pofoPath) {
  int pos = pofoPath.lastIndexOf('\\');
  if (pos < 0) {
    pos = pofoPath.lastIndexOf(':');
  }
  return pos >= 0 ? pofoPath.substring(pos + 1) : pofoPath;
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

// POST /ota/enable, POST /ota/disable -> toggles the ArduinoOTA (espota)
// listener, which is otherwise never started.
static void handleOtaEnable() {
  otaControlEnable();
  server.send(200, "text/plain", "");
}

static void handleOtaDisable() {
  otaControlDisable();
  server.send(200, "text/plain", "");
}

void webApiBegin() {
  ElegantOTA.begin(&server);

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/listESP32", HTTP_GET, handleListEsp32);
  server.on("/listAtari", HTTP_GET, handleListAtari);
  server.on("/listAtariExt", HTTP_GET, handleListAtariExt);
  server.on("/drives", HTTP_GET, handleDrives);
  server.on("/upload", HTTP_POST, handleUpload, handleUploadData);
  server.on("/sendToAtari", HTTP_POST, handleSendToAtari);
  server.on("/downloadFromAtari", HTTP_POST, handleDownloadFromAtari);
  server.on("/ota/enable", HTTP_POST, handleOtaEnable);
  server.on("/ota/disable", HTTP_POST, handleOtaDisable);

  server.serveStatic("/files/", LittleFS, DATA_DIR);
  server.serveStatic("/", LittleFS, "/web/");

  server.begin();
}

void webApiLoop() {
  server.handleClient();
  ElegantOTA.loop();
}
