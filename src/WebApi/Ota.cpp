#include "WebApiInternal.h"

#include "../OtaControl.h"

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

void registerOtaRoutes() {
  server.on("/ota/enable", HTTP_POST, handleOtaEnable);
  server.on("/ota/disable", HTTP_POST, handleOtaDisable);
}
