#include "ServerMode.h"

namespace {

// Single WiFi client slot, per PFTC_PROTOCOL.md's "interface"/"type"
// section - interface=0x00 is the only slot this build has.
const uint8_t kNetifInterface = 0x00;
const uint8_t kNetifTypeWifiClient = 0x01;

}  // namespace

void ServerMode::handleGetNetifs() {
  serverModeLogger().info("Sending GET_NETIFS response");
  // Status, error, count=1, then the one WiFi client slot entry
  // (interface, type, enabled - WiFi is always on while in server mode).
  const uint8_t response[] = {
      0x20, 0x00, 0x01, kNetifInterface, kNetifTypeWifiClient, 0x01,
  };
  const PofoResult result = cable_.sendBlock(response, sizeof(response));
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("sendBlock failed: %u", static_cast<unsigned>(result));
  }
}
