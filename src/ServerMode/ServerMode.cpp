#include "ServerMode.h"

log4mcu::Logger& serverModeLogger() {
  static log4mcu::Logger& logger = log4mcu::Logger::get("ServerMode");
  return logger;
}

void ServerMode::loop() {
  if (!cable_.online()) {
    return;
  }

  uint8_t* payload = 0;
  size_t length = 0;
  const PofoResult result = cable_.receiveBlock(&payload, &length);
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("receiveBlock failed: %u", static_cast<unsigned>(result));
    cable_.reset();
    return;
  }

  handleRequest(payload, length);

  PofoSmartCable::releaseBlock(payload);
  cable_.reset();
}

void ServerMode::handleRequest(const uint8_t* payload, size_t length) {
  if (length == 1 && payload[0] == PftcOpcode::kHello) {
    handleHello();
  } else if (length == 1 && payload[0] == PftcOpcode::kGetNetifs) {
    handleGetNetifs();
  } else if (length == 2 && payload[0] == PftcOpcode::kGetDashboard) {
    handleDashboard();
  }
}
