#include "ServerMode.h"

#include <string.h>

namespace {

const char kDashboardText[] = "Hello from PortfolioESPlink";
const uint8_t kDashboardHeader[] = {
    0x20, 0x00,                                   // status, error
    0x00, 0x01, 0x01, sizeof(kDashboardText) - 1,  // id, row, column, text length
};

}  // namespace

void ServerMode::handleDashboard() {
  serverModeLogger().info("Sending dashboard");
  uint8_t response[sizeof(kDashboardHeader) + sizeof(kDashboardText) - 1];
  memcpy(response, kDashboardHeader, sizeof(kDashboardHeader));
  memcpy(response + sizeof(kDashboardHeader), kDashboardText, sizeof(kDashboardText) - 1);

  const PofoResult result = cable_.sendBlock(response, sizeof(response));
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("sendBlock failed: %u", static_cast<unsigned>(result));
  }
}
