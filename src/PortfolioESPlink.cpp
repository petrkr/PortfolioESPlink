#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "PortfolioLink.h"

#define LED2 4
#define LED3 10

#define DBG_OUTPUT_PORT Serial
#define FILESYSTEM LittleFS

const char* ssid = "IoT";
const char* password = "octopus19";
const char* host = "portfolioesplink";

AsyncWebServer server(80);
File fsUploadFile;

PortfolioLink portfolio(DBG_OUTPUT_PORT);

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

String espFilesToJson(const String& dir) {
  File root = FILESYSTEM.open(dir);
  String output = "{ \"items\" : [";
  bool first = true;

  if (!root || !root.isDirectory()) {
    return "{ \"items\" : [] }";
  }

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (name.startsWith(dir) && dir != "/") {
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
  String localPath = filename;
  if (!localPath.startsWith("/")) {
    localPath = "/" + localPath;
  }

  if (index == 0) {
    DBG_OUTPUT_PORT.print("handleFileUpload Name: ");
    DBG_OUTPUT_PORT.println(localPath);
    fsUploadFile = FILESYSTEM.open(localPath, FILE_WRITE);
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

  DBG_OUTPUT_PORT.print("File ");
  DBG_OUTPUT_PORT.print(localPath);
  DBG_OUTPUT_PORT.println(" uploaded");

  if (!request->hasParam("toAtari")) {
    request->send(200, "text/plain", "Uploaded to ESP32");
    return;
  }

  bool overwrite = request->hasParam("overwrite");
  String destDir = request->hasParam("destDir") ? request->getParam("destDir")->value() : "C:\\";
  if (queueSendToAtari(fsPath, destDir, overwrite)) {
    request->send(202, "text/plain", "Upload queued");
  } else {
    DBG_OUTPUT_PORT.println("Upload job rejected");
    request->send(409, "text/plain", "Portfolio busy");
  }
}

void handleFileListAtari(AsyncWebServerRequest* request) {
  if (!request->hasParam("dir")) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = request->getParam("dir")->value();
  DBG_OUTPUT_PORT.println("handleFileList: " + path);

  String files;
  if (portfolio.listFiles(path.c_str(), files) != PortfolioResult::Ok) {
    request->send(500, "text/plain", "Portfolio list failed");
    return;
  }

  request->send(200, "application/json", filesToJson(files));
}

void handleStatus(AsyncWebServerRequest* request) {
  String output = "{ \"status\" : \"";
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
  output += "\" }";
  request->send(200, "application/json", output);
}

void handleDeleteESP32(AsyncWebServerRequest* request) {
  if (!request->hasParam("path")) {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = request->getParam("path")->value();
  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  if (path.startsWith("/web/") || path == "/web") {
    request->send(403, "text/plain", "Refusing to delete UI files");
    return;
  }

  if (!FILESYSTEM.remove(path)) {
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
  String localPath = "/" + basename;

  bool overwrite = request->hasParam("overwrite");
  if (portfolio.startDownload(FILESYSTEM, pofoPath.c_str(), localPath.c_str(), overwrite)) {
    request->send(202, "text/plain", "Download queued");
  } else {
    request->send(409, "text/plain", "Portfolio busy");
  }
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

  DBG_OUTPUT_PORT.printf("PortfolioESPLink 0.3 - (c) 2026 by Petr Kracik\n");
  DBG_OUTPUT_PORT.printf("based on Transfolio 1.0.1 - (c) 2018 by Klaus Peichl\n");

  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  DBG_OUTPUT_PORT.println("Setting up Portfolio link");
  if (!portfolio.begin({7, 5, 8, 6})) {
    DBG_OUTPUT_PORT.println("Portfolio link init failed");
    return;
  }

  DBG_OUTPUT_PORT.println("Opening filesystem");
  if (!FILESYSTEM.begin()) {
    DBG_OUTPUT_PORT.println("Corrupted or empty filesystem, formatting");
    FILESYSTEM.format();
    FILESYSTEM.begin();
  }

  File root = FILESYSTEM.open("/");
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    size_t fileSize = file.size();
    DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    file = root.openNextFile();
  }
  DBG_OUTPUT_PORT.println();

  DBG_OUTPUT_PORT.print("Connecting to ");
  DBG_OUTPUT_PORT.println(ssid);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DBG_OUTPUT_PORT.print(".");
  }

  DBG_OUTPUT_PORT.println();
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest* request) {}, handleFileUpload);

  server.on("/listAtari", HTTP_GET, handleFileListAtari);
  server.on("/listESP32", HTTP_GET, handleFileListESP32);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/deleteESP32", HTTP_POST, handleDeleteESP32);
  server.on("/sendToAtari", HTTP_POST, handleSendToAtari);
  server.on("/downloadFromAtari", HTTP_POST, handleDownloadFromAtari);
  server.serveStatic("/files/", FILESYSTEM, "/").setCacheControl("no-store");
  server.serveStatic("/", FILESYSTEM, "/web/").setDefaultFile("index.htm");

  server.begin();
}

void loop() {
  digitalWrite(LED2, portfolio.isBusy() ? 1 : 0);
  digitalWrite(LED3, portfolio.isConnected() ? 1 : 0);
  delay(1);
}
