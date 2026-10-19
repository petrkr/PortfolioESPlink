#include "ServerMode.h"

namespace {

// Wire format matches the PofoSmartCable library's HelloServer example.
const uint8_t kHelloResponse[] = {
    0x20,                    // status: success
    0x00,                    // error
    'P', 'F', 'C', '1',      // magic
    0x00, 0x00, 0xFF, 0xFF,  // build_id: 0xFFFF0000, little-endian
    0x00, 0x00, 0x00, 0x00,  // version 0.0.0, reserved
};

}  // namespace

void ServerMode::handleHello() {
  serverModeLogger().info("Sending HELLO response");
  const PofoResult result = cable_.sendBlock(kHelloResponse, sizeof(kHelloResponse));
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("sendBlock failed: %u", static_cast<unsigned>(result));
  }
}
