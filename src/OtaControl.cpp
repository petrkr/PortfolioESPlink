#include "OtaControl.h"

#include <ArduinoOTA.h>
#include <log4mcu.h>

static log4mcu::Logger& logger = log4mcu::Logger::get("OtaControl");
static bool enabled = false;

static const char* otaErrorName(ota_error_t error) {
  switch (error) {
    case OTA_AUTH_ERROR: return "AUTH_ERROR";
    case OTA_BEGIN_ERROR: return "BEGIN_ERROR";
    case OTA_CONNECT_ERROR: return "CONNECT_ERROR";
    case OTA_RECEIVE_ERROR: return "RECEIVE_ERROR";
    case OTA_END_ERROR: return "END_ERROR";
  }
  return "UNKNOWN";
}

void otaControlEnable() {
  if (enabled) {
    return;
  }

  ArduinoOTA.onStart([]() {
    logger.infof("Update start (%s)",
                 ArduinoOTA.getCommand() == U_FLASH ? "flash" : "filesystem");
  });
  ArduinoOTA.onEnd([]() {
    logger.info("Update complete, rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 0xff;
    const unsigned int percent = total ? (progress * 100U / total) : 100U;
    if (percent != lastPercent) {
      logger.infof("Update progress: %u%% (%u/%u bytes)", percent, progress, total);
      lastPercent = percent;
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    logger.errorf("Update error: %s", otaErrorName(error));
  });

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
