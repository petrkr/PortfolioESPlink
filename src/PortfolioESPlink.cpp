#include <Arduino.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "PortfolioLink.h"

#define LED2 4
#define LED3 10

#define DBG_OUTPUT_PORT Serial
#define FILESYSTEM SPIFFS

const char* ssid = "IoT";
const char* password = "octopus19";
const char* host = "portfolioesplink";

WebServer server(80);
File fsUploadFile;

PortfolioLink portfolio(DBG_OUTPUT_PORT);

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

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".exe")) {
    return "application/x-msdownload";
  } else if (filename.endsWith(".txt")) {
    return "text/plain";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "application/octet-stream";
}

void handleFileUpload() {
  String filename;
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }

    DBG_OUTPUT_PORT.print("handleFileUpload Name: ");
    DBG_OUTPUT_PORT.println(filename);
    fsUploadFile = FILESYSTEM.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    DBG_OUTPUT_PORT.print("handleFileUpload Data: ");
    DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();

      filename = upload.filename;
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }

      DBG_OUTPUT_PORT.print("File ");
      DBG_OUTPUT_PORT.print(filename);
      DBG_OUTPUT_PORT.println(" uploaded");

      if (!portfolio.transmitFile(FILESYSTEM, filename, String("C:\\" + upload.filename).c_str())) {
        DBG_OUTPUT_PORT.println("Upload file to Atari failed");
      }
    }
    DBG_OUTPUT_PORT.print("handleFileUpload Size: ");
    DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void handleFileListAtari() {
  server.enableCORS();

  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = server.arg("dir");
  DBG_OUTPUT_PORT.println("handleFileList: " + path);

  String output;
  if (!portfolio.listFilesJson(path.c_str(), output)) {
    server.send(500, "text/plain", "Portfolio list failed");
    return;
  }

  server.send(200, "application/json", output);
}

void setup() {
  DBG_OUTPUT_PORT.begin(115200);
  delay(250);

  DBG_OUTPUT_PORT.printf("PortfolioESPLink 0.2 - (c) 2023 by Petr Kracik\n");
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

  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  server.on("/listAtari", HTTP_GET, handleFileListAtari);

  server.begin();
}

void loop() {
  server.handleClient();

  DBG_OUTPUT_PORT.print("Waiting for connection");
  while (!portfolio.detect()) {
    delay(1);
    digitalWrite(LED2, 1);
    digitalWrite(LED3, 0);
  }

  DBG_OUTPUT_PORT.println("... Connected");
  digitalWrite(LED2, 0);
  digitalWrite(LED3, 1);

  delay(1);
}
