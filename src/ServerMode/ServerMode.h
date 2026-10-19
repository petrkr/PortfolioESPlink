#pragma once

#include <Arduino.h>
#include <PofoSmartCable.h>
#include <log4mcu.h>

// PFTC opcodes - see PFTC_PROTOCOL.md in the POFOSCAB repo.
namespace PftcOpcode {
constexpr uint8_t kHello = 0x01;
constexpr uint8_t kGetNetifs = 0x02;
constexpr uint8_t kGetNetif = 0x03;
constexpr uint8_t kSetNetif = 0x04;
constexpr uint8_t kGetWifiscan = 0x07;
constexpr uint8_t kGetDashboard = 0x08;
}  // namespace PftcOpcode

// Single WiFi client slot - interface=0x00 is the only slot this build
// has. Shared by GetNetifs.cpp and GetNetif.cpp.
namespace PftcNetif {
constexpr uint8_t kInterface = 0x00;
constexpr uint8_t kTypeWifiClient = 0x01;
}  // namespace PftcNetif

class ServerMode {
 public:
  explicit ServerMode(PofoSmartCable& cable) : cable_(cable) {}

  void loop();

 private:
  void handleRequest(const uint8_t* payload, size_t length);

  void handleHello();
  void handleGetNetifs();
  void handleGetNetif(const uint8_t* payload, size_t length);
  void handleSetNetif(const uint8_t* payload, size_t length);
  void handleGetWifiscan(const uint8_t* payload, size_t length);
  void handleDashboard();

  PofoSmartCable& cable_;
};

log4mcu::Logger& serverModeLogger();

extern ServerMode serverMode;
