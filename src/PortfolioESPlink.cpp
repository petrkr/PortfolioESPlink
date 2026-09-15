#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <log4mcu.h>

#include "PortfolioLink.h"
#include "Version.h"
#include "secrets.h"

#define LED2 4
#define LED3 10

#define DBG_OUTPUT_PORT Serial
#define FILESYSTEM LittleFS
#define DATA_DIR "/data"

const char* host = "portfolioesplink";

AsyncWebServer server(80);
File fsUploadFile;

log4mcu::SerialLogAppender logAppender(DBG_OUTPUT_PORT);
log4mcu::Logger& log_ = log4mcu::Logger::get("main");

PortfolioLink portfolio;

const char* statusText(PortfolioStatus status) {
  switch (status) {
    case PortfolioStatus::Connected:
      return "connected";
    case PortfolioStatus::Busy:
      return "busy";
    case PortfolioStatus::Disconnected:
    default:
      return "disconnected";
  }
}

const char* resultText(PortfolioResult result) {
  switch (result) {
    case PortfolioResult::InvalidPath:
      return "destination rejected (bad path or disk full)";
    case PortfolioResult::Unknown:
      return "error";
    case PortfolioResult::None:
    case PortfolioResult::Ok:
    default:
      return "";
  }
}

const char* phaseText(PortfolioTransferPhase phase) {
  switch (phase) {
    case PortfolioTransferPhase::PofoUpload:
      return "pofo_upload";
    case PortfolioTransferPhase::Idle:
    default:
      return "idle";
  }
}

String filesToJson(const String& files) {
  String output = "{ \"files\" : [";
  int start = 0;
  bool first = true;

  while (start < files.length()) {
    int end = files.indexOf('\n', start);
    if (end < 0) {
      end = files.length();
    }

    if (end > start) {
      if (!first) {
        output += ',';
      }
      output += "\"";
      output += files.substring(start, end);
      output += "\"";
      first = false;
    }

    start = end + 1;
  }

  output += "]}";
  return output;
}

String espFilesToJson(const String& relDir) {
  String dir = relDir == "/" ? String(DATA_DIR) : String(DATA_DIR) + relDir;
  File root = FILESYSTEM.open(dir);
  String output = "{ \"items\" : [";
  bool first = true;

  if (!root || !root.isDirectory()) {
    return "{ \"items\" : [] }";
  }

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (name.startsWith(dir)) {
      name = name.substring(dir.length());
    }
    if (name.startsWith("/")) {
      name = name.substring(1);
    }

    if (!first) {
      output += ',';
    }
    output += "{ \"name\" : \"";
    output += name;
    output += "\", \"type\" : \"";
    output += file.isDirectory() ? "folder" : "file";
    output += "\", \"size\" : ";
    output += file.size();
    output += " }";
    first = false;

    file = root.openNextFile();
  }

  output += "]}";
  return output;
}

String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
  return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
}

String basenameOf(const String& path) {
  int slash = path.lastIndexOf('/');
  return slash < 0 ? path : path.substring(slash + 1);
}

bool queueSendToAtari(const String& localPath, const String& destDir, bool overwrite) {
  String dir = destDir;
  if (!dir.endsWith("\\")) {
    dir += "\\";
  }
  String pofoPath = dir + basenameOf(localPath);
  return portfolio.startUpload(FILESYSTEM, localPath.c_str(), pofoPath.c_str(), overwrite);
}

void handleFileUpload(AsyncWebServerRequest* request,
                      String filename,
                      size_t index,
                      uint8_t* data,
                      size_t len,
                      bool final) {
  String relPath = filename;
  if (!relPath.startsWith("/")) {
    relPath = "/" + relPath;
  }
  String fsPath = String(DATA_DIR) + relPath;

  if (index == 0) {
    log_.infof("handleFileUpload Name: %s", fsPath.c_str());
    fsUploadFile = FILESYSTEM.open(fsPath, FILE_WRITE);
  }

  if (fsUploadFile && len > 0) {
    fsUploadFile.write(data, len);
  }

  if (!final) {
    return;
  }

  if (fsUploadFile) {
    fsUploadFile.close();
  }

  log_.infof("File %s uploaded", fsPath.c_str());

  if (!request->hasParam("toAtari")) {
    request->send(200, "text/plain", "Uploaded to ESP32");
    return;
  }

  bool overwrite = request->hasParam("overwrite");
  String destDir = request->hasParam("destDir") ? request->getParam("destDir")->value() : "C:\\";
  if (queueSendToAtari(fsPath, destDir, overwrite)) {
    request->send(202, "text/plain", "Upload queued");
  } else {
    log_.warn("Upload job rejected");
    request->send(409, "text/plain", "Portfolio busy");
  }
}

void handleFileListAtari(AsyncWebServerRequest* request) {
  if (!request->hasParam("dir")) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = request->getParam("dir")->value();
  log_.infof("handleFileList: %s", path.c_str());

  String files;
  if (portfolio.listFiles(path.c_str(), files) != PortfolioResult::Ok) {
    request->send(500, "text/plain", "Portfolio list failed");
    return;
  }

  request->send(200, "application/json", filesToJson(files));
}

// Extended listing (PFTD 0x86, requires the PFTD driver to be resident on
// the Portfolio - see /hello capabilities). Each entry line from
// PortfolioLink::listFilesExtended is "D|F,size,YYYY-MM-DD HH:MM:SS,name" -
// split into JSON objects, same shape as espFilesToJson's "items" array.
String atariExtFilesToJson(const String& entries, uint32_t freeBytes, uint32_t totalBytes) {
  String output = "{ \"freeBytes\" : ";
  output += freeBytes;
  output += ", \"totalBytes\" : ";
  output += totalBytes;
  output += ", \"items\" : [";
  int start = 0;
  bool first = true;

  while (start < entries.length()) {
    int end = entries.indexOf('\n', start);
    if (end < 0) {
      end = entries.length();
    }

    String line = entries.substring(start, end);
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    if (c1 > 0 && c2 > c1 && c3 > c2) {
      if (!first) {
        output += ',';
      }
      output += "{ \"name\" : \"";
      output += line.substring(c3 + 1);
      output += "\", \"type\" : \"";
      output += line.charAt(0) == 'D' ? "folder" : "file";
      output += "\", \"size\" : ";
      output += line.substring(c1 + 1, c2);
      output += ", \"modified\" : \"";
      output += line.substring(c2 + 1, c3);
      output += "\" }";
      first = false;
    }

    start = end + 1;
  }

  output += "]}";
  return output;
}

void handleFileListAtariExt(AsyncWebServerRequest* request) {
  if (!request->hasParam("dir")) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = request->getParam("dir")->value();
  log_.infof("handleFileListExt: %s", path.c_str());

  String entries;
  uint32_t freeBytes = 0;
  uint32_t totalBytes = 0;
  if (portfolio.listFilesExtended(path.c_str(), entries, freeBytes, totalBytes) != PortfolioResult::Ok) {
    request->send(500, "text/plain", "Portfolio extended list failed");
    return;
  }

  request->send(200, "application/json", atariExtFilesToJson(entries, freeBytes, totalBytes));
}

void handleStatus(AsyncWebServerRequest* request) {
  char fwBuildIdHex[9];
  snprintf(fwBuildIdHex, sizeof(fwBuildIdHex), "%08X", FW_BUILD_ID);

  String output = "{ \"fwBuildId\" : \"";
  output += fwBuildIdHex;
  output += "\", \"status\" : \"";
  output += statusText(portfolio.status());
  output += "\", \"connected\" : ";
  output += portfolio.isConnected() ? "true" : "false";
  output += ", \"phase\" : \"";
  output += phaseText(portfolio.transferPhase());
  output += "\", \"done\" : ";
  output += portfolio.transferDone();
  output += ", \"total\" : ";
  output += portfolio.transferTotal();
  output += ", \"espUsed\" : ";
  output += FILESYSTEM.usedBytes();
  output += ", \"espTotal\" : ";
  output += FILESYSTEM.totalBytes();
  output += ", \"error\" : \"";
  output += resultText(portfolio.lastResult());
  output += "\", \"pftd\" : ";
  if (portfolio.hasPFTD()) {
    char buildIdHex[9];
    snprintf(buildIdHex, sizeof(buildIdHex), "%08X", portfolio.pftdBuildId());
    output += "{ \"buildId\" : \"";
    output += buildIdHex;
    output += "\", \"version\" : ";
    output += portfolio.pftdVersion();
    output += ", \"capabilities\" : ";
    output += portfolio.pftdCapabilities();
    output += " }";
  } else {
    output += "null";
  }
  output += " }";
  request->send(200, "application/json", output);
}

void handleDeleteESP32(AsyncWebServerRequest* request) {
  if (!request->hasParam("path")) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String relPath = request->getParam("path")->value();
  if (!relPath.startsWith("/")) {
    relPath = "/" + relPath;
  }

  if (relPath.indexOf("..") >= 0) {
    request->send(403, "text/plain", "Invalid path");
    return;
  }

  if (!FILESYSTEM.remove(String(DATA_DIR) + relPath)) {
    request->send(404, "text/plain", "Delete failed");
    return;
  }

  request->send(200, "text/plain", "Deleted");
}

void handleSendToAtari(AsyncWebServerRequest* request) {
  if (!request->hasParam("path")) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String relPath = request->getParam("path")->value();
  if (!relPath.startsWith("/")) {
    relPath = "/" + relPath;
  }

  bool overwrite = request->hasParam("overwrite");
  String destDir = request->hasParam("destDir") ? request->getParam("destDir")->value() : "C:\\";
  if (queueSendToAtari(String(DATA_DIR) + relPath, destDir, overwrite)) {
    request->send(202, "text/plain", "Upload queued");
  } else {
    request->send(409, "text/plain", "Portfolio busy");
  }
}

void handleDownloadFromAtari(AsyncWebServerRequest* request) {
  if (!request->hasParam("path")) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String pofoPath = request->getParam("path")->value();
  String basename = pofoPath;
  int backslash = basename.lastIndexOf('\\');
  if (backslash >= 0) {
    basename = basename.substring(backslash + 1);
  }
  String fsPath = String(DATA_DIR) + "/" + basename;

  bool overwrite = request->hasParam("overwrite");
  if (portfolio.startDownload(FILESYSTEM, pofoPath.c_str(), fsPath.c_str(), overwrite)) {
    request->send(202, "text/plain", "Download queued");
  } else {
    request->send(409, "text/plain", "Portfolio busy");
  }
}

// Temporary debug endpoint for testing a new Atari-side TSR/hook: sends an
// arbitrary hex-encoded byte block over the smart-cable link and returns
// whatever comes back, also hex-encoded. POST param "data" = hex string
// (e.g. "0006AA" for bytes 0x00,0x06,0xAA). No interpretation on this side.
void handleSendRaw(AsyncWebServerRequest* request) {
  if (!request->hasParam("data", true)) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String hex = request->getParam("data", true)->value();
  if (hex.length() % 2 != 0) {
    request->send(500, "text/plain", "hex data must have even length");
    return;
  }

  size_t len = hex.length() / 2;
  uint8_t* buf = static_cast<uint8_t*>(malloc(len));
  if (!buf) {
    request->send(500, "text/plain", "OOM");
    return;
  }
  for (size_t i = 0; i < len; i++) {
    buf[i] = strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }

  String response;
  PortfolioResult result = portfolio.sendRaw(buf, len, response);
  free(buf);

  if (result != PortfolioResult::Ok) {
    request->send(500, "text/plain", "sendRaw failed: " + String(resultText(result)));
    return;
  }

  request->send(200, "text/plain", response);
}

void handleHello(AsyncWebServerRequest* request) {
  bool present = false;
  uint32_t buildId = 0;
  uint8_t version = 0;
  uint8_t capabilities = 0;

  PortfolioResult result = portfolio.helloDaemon(present, buildId, version, capabilities);
  if (result != PortfolioResult::Ok) {
    request->send(500, "text/plain", "hello failed: " + String(resultText(result)));
    return;
  }

  char buildIdHex[9];
  snprintf(buildIdHex, sizeof(buildIdHex), "%08X", buildId);

  String output = "{ \"present\" : ";
  output += present ? "true" : "false";
  output += ", \"buildId\" : \"";
  output += buildIdHex;
  output += "\", \"version\" : ";
  output += version;
  output += ", \"capabilities\" : ";
  output += capabilities;
  output += " }";
  request->send(200, "application/json", output);
}

void handleDrives(AsyncWebServerRequest* request) {
  uint8_t driveCount = 0;

  PortfolioResult result = portfolio.listDrives(driveCount);
  if (result != PortfolioResult::Ok) {
    request->send(500, "text/plain", "drives failed: " + String(resultText(result)));
    return;
  }

  String output = "{ \"driveCount\" : ";
  output += driveCount;
  output += ", \"drives\" : [";
  for (uint8_t i = 0; i < driveCount; i++) {
    if (i > 0) {
      output += ',';
    }
    output += "\"";
    output += static_cast<char>('A' + i);
    output += "\"";
  }
  output += "] }";
  request->send(200, "application/json", output);
}

void handleFileListESP32(AsyncWebServerRequest* request) {
  String dir = "/";
  if (request->hasParam("dir")) {
    dir = request->getParam("dir")->value();
  }
  if (!dir.startsWith("/")) {
    dir = "/" + dir;
  }

  request->send(200, "application/json", espFilesToJson(dir));
}

void setup() {
  DBG_OUTPUT_PORT.begin(115200);
  delay(250);

  log4mcu::Logger::setAppender(&logAppender);
  log4mcu::Logger::setGlobalMinLevel(log4mcu::LogLevel::Info);

  log_.info("PortfolioESPLink 0.3 - (c) 2026 by Petr Kracik");
  log_.info("based on Transfolio 1.0.1 - (c) 2018 by Klaus Peichl");

  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  log_.info("Setting up Portfolio link");
  if (!portfolio.begin({7, 5, 8, 6})) {
    log_.error("Portfolio link init failed");
    return;
  }

  log_.info("Opening filesystem");
  if (!FILESYSTEM.begin()) {
    log_.warn("Corrupted or empty filesystem, formatting");
    FILESYSTEM.format();
    FILESYSTEM.begin();
  }

  if (!FILESYSTEM.exists(DATA_DIR)) {
    FILESYSTEM.mkdir(DATA_DIR);
  }

  File root = FILESYSTEM.open("/");
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    size_t fileSize = file.size();
    log_.infof("FS File: %s, size: %s", fileName.c_str(), formatBytes(fileSize).c_str());
    file = root.openNextFile();
  }

  log_.infof("Connecting to %s", ssid);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  log_.infof("Connected! IP address: %s", WiFi.localIP().toString().c_str());

  // UTC, no DST - used to timestamp files uploaded to the Portfolio (see
  // PortfolioLink::runUpload). Uses the ESP32 core's built-in SNTP client
  // (configTime), no extra library.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest* request) {}, handleFileUpload);

  server.on("/listAtari", HTTP_GET, handleFileListAtari);
  server.on("/listAtariExt", HTTP_GET, handleFileListAtariExt);
  server.on("/listESP32", HTTP_GET, handleFileListESP32);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/deleteESP32", HTTP_POST, handleDeleteESP32);
  server.on("/sendToAtari", HTTP_POST, handleSendToAtari);
  server.on("/downloadFromAtari", HTTP_POST, handleDownloadFromAtari);
  server.on("/sendRaw", HTTP_POST, handleSendRaw);
  server.on("/hello", HTTP_GET, handleHello);
  server.on("/drives", HTTP_GET, handleDrives);
  server.serveStatic("/files/", FILESYSTEM, DATA_DIR "/").setCacheControl("no-store");
  server.serveStatic("/", FILESYSTEM, "/web/").setDefaultFile("index.htm");

  server.begin();
}

void loop() {
  digitalWrite(LED2, portfolio.isBusy() ? 1 : 0);
  digitalWrite(LED3, portfolio.isConnected() ? 1 : 0);
  delay(1);
}
