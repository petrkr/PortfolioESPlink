#include "OtaControl.h"

#include <ArduinoOTA.h>
#include <log4mcu.h>

static log4mcu::Logger& logger = log4mcu::Logger::get("OtaControl");
static bool enabled = false;

void otaControlEnable() {
  if (enabled) {
    return;
  }

  ArduinoOTA.begin();
  enabled = true;
  logger.info("ArduinoOTA enabled");
}

void otaControlDisable() {
  if (!enabled) {
    return;
  }

  ArduinoOTA.end();
  enabled = false;
  logger.info("ArduinoOTA disabled");
}

bool otaControlEnabled() {
  return enabled;
}

void otaControlLoop() {
  if (enabled) {
    ArduinoOTA.handle();
  }
}
