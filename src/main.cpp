/*
  PortfolioESPlink is a file transfer utility that connects to the Atari Portfolio
  pocket computer over WiFi. It communicates with the built-in file transfer software
  of the Portfolio using the PofoSmartCable library.

  Version history:
  0.1 First release, only list folder and cat(type) files to Serial, protocol based on Transfolio v1.0.1 (2019-05-26)
  0.2 Format SPIFF if it does not work, fix setupPort
  0.3 Rewritten on top of the PofoSmartCable library

*/

/*
  USAGE:
  List directory:
  GET http://IP/list?dir=A:\*.*
    param dir=PATH/FILTER

  POST http://IP/upload
    param file

  curl -X POST -F "file=@fileondrive" http://IP/upload

*/

#include <board_config.h>

#ifdef ESP32
#include <WiFi.h>
#include <WebServer.h>

WebServer server(80);
#endif

#include <LittleFS.h>
#include <PofoSmartCable.h>
#include <PofoFileTransfer.h>
#include <log4mcu.h>
#include <secrets.h>

#define FILESYSTEM LittleFS
#define DBG_OUTPUT_PORT Serial

log4mcu::Logger& logger = log4mcu::Logger::get("PortfolioESPlink");
log4mcu::SerialLogAppender serialAppender(DBG_OUTPUT_PORT);

PofoSmartCable cable;
PofoFileTransfer fileTransfer(cable);

File fsUploadFile;

const char* host = "portfolioesplink";

bool force = true;

// Path of a file waiting to be sent to the Portfolio once the link comes online.
String pendingLocalPath;
String pendingPofoPath;

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

void onLinkStateChanged(bool isOnline) {
  logger.infof("Link %s", isOnline ? "online" : "offline");
  digitalWrite(LED2, isOnline ? 0 : 1);
  digitalWrite(LED3, isOnline ? 1 : 0);
}

void onTransferProgress(size_t transferred, size_t total) {
  static uint8_t lastPercent = 0xff;
  const uint8_t percent = total ? static_cast<uint8_t>(transferred * 100UL / total) : 100;
  if (percent != lastPercent) {
    logger.infof("Progress: %u%% (%u/%u bytes)", percent,
                 static_cast<unsigned>(transferred),
                 static_cast<unsigned>(total));
    lastPercent = percent;
  }
}

void handleFileUpload() {
  String filename;

  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }

    logger.infof("handleFileUpload Name: %s", filename.c_str());
    fsUploadFile = FILESYSTEM.open(filename, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
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

      pendingLocalPath = filename;
      pendingPofoPath = "C:\\" + upload.filename;

      logger.infof("File %s queued for transfer to %s", filename.c_str(), pendingPofoPath.c_str());
    }
    logger.infof("handleFileUpload Size: %u", static_cast<unsigned>(upload.totalSize));
  }
}

void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  if (!cable.online()) {
    server.send(503, "text/plain", "Portfolio not connected");
    return;
  }

  String path = server.arg("dir");
  logger.infof("handleFileList: %s", path.c_str());

  PofoFileTransferList response;
  const PofoResult result = fileTransfer.list(path.c_str(), &response);
  if (result != PofoResult::OK) {
    logger.warnf("LIST failed: %u", static_cast<unsigned>(result));
    server.send(500, "text/plain", "LIST failed");
    return;
  }

  String output = "[";
  for (size_t i = 0; i < response.count(); i++) {
    if (output != "[") {
      output += ',';
    }
    output += "{\"name\":\"";
    output += response.name(i);
    output += "\"}";
  }
  output += "]";

  server.send(200, "text/json", output);
}

void processPendingTransmit() {
  if (pendingLocalPath.isEmpty() || !cable.online()) {
    return;
  }

  File file = FILESYSTEM.open(pendingLocalPath, "r");
  if (!file) {
    logger.warnf("Cannot open queued file: %s", pendingLocalPath.c_str());
    pendingLocalPath = "";
    pendingPofoPath = "";
    return;
  }

  const size_t length = file.size();
  logger.infof("Transmitting %s to %s (%u bytes)", pendingLocalPath.c_str(),
               pendingPofoPath.c_str(), static_cast<unsigned>(length));

  const PofoResult result = fileTransfer.transmitFile(pendingPofoPath.c_str(), file, length, force);
  file.close();

  if (result != PofoResult::OK) {
    logger.warnf("TransmitFile failed: %u", static_cast<unsigned>(result));
  } else {
    logger.info("Transmit complete");
  }

  pendingLocalPath = "";
  pendingPofoPath = "";
}

void setup() {
  DBG_OUTPUT_PORT.begin(115200);
  delay(250);

  log4mcu::Logger::setAppender(&serialAppender);
  log4mcu::Logger::setGlobalMinLevel(log4mcu::LogLevel::Debug);

  logger.info("PortfolioESPLink 0.3 - (c) 2023 by Petr Kracik");
  logger.info("Using PofoSmartCable library");

  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  logger.info("Opening filesystem");
  if (!FILESYSTEM.begin(true)) {
    logger.error("Filesystem mount failed");
  }

  // Setup WiFi
  logger.infof("Connecting to %s", ssid);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  logger.infof("Connected! IP address: %s", WiFi.localIP().toString().c_str());

  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  server.on("/list", HTTP_GET, handleFileList);

  server.begin();

  if (!cable.begin(CABLE_CLK_IN, CABLE_DATA_IN, CABLE_CLK_OUT, CABLE_DATA_OUT)) {
    logger.error("PofoSmartCable begin failed");
    return;
  }
  cable.setLinkStateCallback(onLinkStateChanged);
  fileTransfer.setProgressCallback(onTransferProgress);

  logger.info("Waiting for Smart Cable synchronization");
}

void loop() {
  server.handleClient();
  cable.loop();

  processPendingTransmit();
}
