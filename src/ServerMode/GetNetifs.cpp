#include "ServerMode.h"

void ServerMode::handleGetNetifs() {
  serverModeLogger().info("Sending GET_NETIFS response");
  // Status, error, count=1, then the one WiFi client slot entry
  // (interface, type, enabled - WiFi is always on while in server mode).
  const uint8_t response[] = {
      0x20, 0x00, 0x01, PftcNetif::kInterface, PftcNetif::kTypeWifiClient, 0x01,
  };
  const PofoResult result = cable_.sendBlock(response, sizeof(response));
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("sendBlock failed: %u", static_cast<unsigned>(result));
  }
}
