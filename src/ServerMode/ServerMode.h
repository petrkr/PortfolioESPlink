#pragma once

#include <Arduino.h>
#include <PofoSmartCable.h>
#include <log4mcu.h>

// PFTC opcodes - see PFTC_PROTOCOL.md in the POFOSCAB repo.
namespace PftcOpcode {
constexpr uint8_t kHello = 0x01;
constexpr uint8_t kGetNetifs = 0x02;
constexpr uint8_t kGetDashboard = 0x08;
}  // namespace PftcOpcode

class ServerMode {
 public:
  explicit ServerMode(PofoSmartCable& cable) : cable_(cable) {}

  void loop();

 private:
  void handleRequest(const uint8_t* payload, size_t length);

  void handleHello();
  void handleGetNetifs();
  void handleDashboard();

  PofoSmartCable& cable_;
};

log4mcu::Logger& serverModeLogger();

extern ServerMode serverMode;
