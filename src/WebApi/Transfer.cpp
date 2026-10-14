#include "WebApiInternal.h"

#include <LittleFS.h>
#include <log4mcu.h>

#include "../CableLink.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("WebApi");

static const uint32_t kTransferWaitTimeoutMs = 60000;

static bool waitForTransferDone() {
  const uint32_t startedAt = millis();
  while (cableLink.busy()) {
    if (millis() - startedAt > kTransferWaitTimeoutMs) {
      logger.error("Transfer wait timed out");
      return false;
    }
    delay(1);
  }
  return true;
}

static void sendTransferResult(AsyncWebServerRequest* request, bool ok, const String& errorMessage) {
  if (ok) {
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  String out = "{\"ok\":false,\"message\":\"";
  out += errorMessage;
  out += "\"}";
  request->send(500, "application/json", out);
}

struct UploadTransferTaskArgs {
  AsyncWebServerRequestPtr request;
};

static void uploadTransferTask(void* pvParameters) {
  UploadTransferTaskArgs* args = static_cast<UploadTransferTaskArgs*>(pvParameters);
  AsyncWebServerRequestPtr requestPtr = args->request;
  delete args;

  const bool done = waitForTransferDone();

  if (auto request = requestPtr.lock()) {
    sendTransferResult(request.get(), done && cableLink.lastError().isEmpty(), cableLink.lastError());
  }
  vTaskDelete(NULL);
}

static void startUploadTransferTask(AsyncWebServerRequest* request) {
  UploadTransferTaskArgs* args = new UploadTransferTaskArgs{request->pause()};
  xTaskCreate(uploadTransferTask, "uploadTx", 4096, args, 1, NULL);
}

struct DownloadTransferTaskArgs {
  AsyncWebServerRequestPtr request;
  String pofoPath;
  String localPath;
};

static void downloadTransferTask(void* pvParameters) {
  DownloadTransferTaskArgs* args = static_cast<DownloadTransferTaskArgs*>(pvParameters);
  AsyncWebServerRequestPtr requestPtr = args->request;
  const String pofoPath = args->pofoPath;
  const String localPath = args->localPath;
  delete args;

  const bool done = waitForTransferDone();

  auto request = requestPtr.lock();
  if (!request) {
    vTaskDelete(NULL);
    return;
  }

  if (!done || !cableLink.lastError().isEmpty()) {
    if (cableLink.lastResult() == PofoResult::REMOTE_ERROR) {
      request->send(404, "application/json", "{\"ok\":false,\"message\":\"File not found\"}");
    } else {
      sendTransferResult(request.get(), false, cableLink.lastError());
    }
    vTaskDelete(NULL);
    return;
  }

  File file = LittleFS.open(localPath, "r");
  if (!file) {
    sendTransferResult(request.get(), false, "Downloaded file missing on ESP32");
    vTaskDelete(NULL);
    return;
  }
  file.close();

  AsyncWebServerResponse* response =
      request->beginResponse(LittleFS, localPath, "application/octet-stream", /*download=*/true);
  response->addHeader("Content-Disposition", "attachment; filename=\"" + pofoBasename(pofoPath) + "\"");
  request->send(response);
  vTaskDelete(NULL);
}

static void startDownloadTransferTask(AsyncWebServerRequest* request, const String& pofoPath, const String& localPath) {
  DownloadTransferTaskArgs* args = new DownloadTransferTaskArgs{request->pause(), pofoPath, localPath};
  xTaskCreate(downloadTransferTask, "downloadTx", 4096, args, 1, NULL);
}

static bool uploadToAtariRequested = false;
static String uploadLocalPath;
static String uploadPofoPath;
static bool uploadOverwrite = false;

static void handleUpload(AsyncWebServerRequest* request) {
  if (!uploadToAtariRequested) {
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  uploadToAtariRequested = false;

  if (!cableLink.online()) {
    request->send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  if (!cableLink.requestTransmit(uploadLocalPath, uploadPofoPath, uploadOverwrite)) {
    request->send(409, "application/json", "{\"ok\":false,\"message\":\"Transfer already in progress\"}");
    return;
  }

  startUploadTransferTask(request);
}

static void handleUploadData(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data,
                              size_t len, bool final) {
  if (index == 0) {
    String path = String(DATA_DIR) + filename;

    logger.infof("Upload start: %s", path.c_str());
    fsUploadFile = LittleFS.open(path, "w");
  }

  if (fsUploadFile) {
    fsUploadFile.write(data, len);
  }

  if (final) {
    if (fsUploadFile) {
      fsUploadFile.close();

      if (request->hasArg("toAtari")) {
        uploadLocalPath = String(DATA_DIR) + filename;
        uploadOverwrite = request->hasArg("overwrite");
        String destDir = request->hasArg("destDir") ? request->arg("destDir") : "C:\\";
        uploadPofoPath = destDir + filename;
        uploadToAtariRequested = true;
      }
    }
  }
}

static void handleSendToAtari(AsyncWebServerRequest* request) {
  if (!request->hasArg("path")) {
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  if (!cableLink.online()) {
    request->send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  String localPath = String(DATA_DIR) + request->arg("path");
  localPath.replace("//", "/");

  const bool overwrite = request->hasArg("overwrite");
  const String destDir = request->hasArg("destDir") ? request->arg("destDir") : "C:\\";

  int lastSlash = localPath.lastIndexOf('/');
  const String basename = lastSlash >= 0 ? localPath.substring(lastSlash + 1) : localPath;
  const String pofoPath = destDir + basename;

  if (!cableLink.requestTransmit(localPath, pofoPath, overwrite)) {
    request->send(409, "application/json", "{\"ok\":false,\"message\":\"Transfer already in progress\"}");
    return;
  }

  startUploadTransferTask(request);
}

static void handleDownloadFile(AsyncWebServerRequest* request) {
  if (!request->hasArg("path")) {
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"BAD ARGS\"}");
    return;
  }

  if (!cableLink.online()) {
    request->send(503, "application/json", "{\"ok\":false,\"message\":\"Portfolio not connected\"}");
    return;
  }

  const String pofoPath = request->arg("path");
  const String localPath = String(DATA_DIR) + pofoBasename(pofoPath);

  if (!cableLink.requestReceive(pofoPath, localPath, /*overwrite=*/true)) {
    request->send(409, "application/json", "{\"ok\":false,\"message\":\"Transfer already in progress\"}");
    return;
  }

  startDownloadTransferTask(request, pofoPath, localPath);
}

void registerTransferRoutes() {
  server.on("/uploadFile", HTTP_POST, handleUpload, handleUploadData);
  server.on("/sendToAtari", HTTP_POST, handleSendToAtari);
  server.on("/downloadFile", HTTP_POST, handleDownloadFile);
}
