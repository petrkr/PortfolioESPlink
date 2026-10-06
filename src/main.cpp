/*
  PortfolioESPlink is a file transfer utility that connects to the Atari Portfolio
  pocket computer over WiFi. It communicates with the built-in file transfer software
  of the Portfolio using the PofoSmartCable library.

  Version history:
  0.1 First release, only list folder and cat(type) files to Serial, protocol based on Transfolio v1.0.1 (2019-05-26)
  0.2 Format SPIFF if it does not work, fix setupPort
  0.3 Rewritten on top of the PofoSmartCable library
  0.4 Split into CableLink/WebApi/FsUtil modules, REST API for data/web

  See WebApi.h for the REST API served to data/web.
*/

#include <board_config.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <log4mcu.h>
#include <secrets.h>

#include "CableLink.h"
#include "WebApi.h"
#include "OtaControl.h"

static log4mcu::Logger& logger = log4mcu::Logger::get("PortfolioESPlink");
static log4mcu::SerialLogAppender serialAppender(Serial);

void setup() {
  // USB-CDC write() blocks (host-backpressure path) up to tx_timeout_ms *
  // max_consec_timeouts (~2s per call by default) whenever the port is
  // enumerated but nothing is reading it. Disable that wait so logging never
  // stalls the main loop (and therefore the web server) while nobody has the
  // port open.
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);
  delay(250);

  log4mcu::Logger::setAppender(&serialAppender);
  log4mcu::Logger::setGlobalMinLevel(log4mcu::LogLevel::Debug);

  logger.info("PortfolioESPLink 0.4 - (c) 2023 by Petr Kracik");
  logger.info("Using PofoSmartCable library");

  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  logger.info("Opening filesystem");
  if (!LittleFS.begin(true)) {
    logger.error("Filesystem mount failed");
  }

  if (!LittleFS.exists("/data")) {
    LittleFS.mkdir("/data");
  }

  logger.infof("Connecting to %s", ssid);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  logger.infof("Connected! IP address: %s", WiFi.localIP().toString().c_str());

  webApiBegin();

  if (!cableLink.begin(CABLE_CLK_IN, CABLE_DATA_IN, CABLE_CLK_OUT, CABLE_DATA_OUT)) {
    logger.error("PofoSmartCable begin failed");
    return;
  }

  logger.info("Waiting for Smart Cable synchronization");
}

void loop() {
  webApiLoop();
  cableLink.loop();
  otaControlLoop();
}
