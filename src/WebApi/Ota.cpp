#include "WebApiInternal.h"

#include "../OtaControl.h"

// POST /ota/enable, POST /ota/disable -> toggles the ArduinoOTA (espota)
// listener, which is otherwise never started.
static void handleOtaEnable(AsyncWebServerRequest* request) {
  otaControlEnable();
  request->send(200, "text/plain", "");
}

static void handleOtaDisable(AsyncWebServerRequest* request) {
  otaControlDisable();
  request->send(200, "text/plain", "");
}

void registerOtaRoutes() {
  server.on("/ota/enable", HTTP_POST, handleOtaEnable);
  server.on("/ota/disable", HTTP_POST, handleOtaDisable);
}
