#include "ServerMode.h"

#include <string.h>

#include <WiFi.h>

namespace {

const uint8_t kStatusOk = 0x20;
const uint8_t kStatusError = 0x10;
const uint8_t kErrorInterfaceNotSupported = 0x04;

uint8_t subnetPrefixLength(IPAddress mask) {
  uint32_t bits = static_cast<uint32_t>(mask);
  uint8_t prefix = 0;
  while (bits != 0) {
    prefix += bits & 1;
    bits >>= 1;
  }
  return prefix;
}

}  // namespace

void ServerMode::handleGetNetif(const uint8_t* payload, size_t length) {
  const uint8_t interface = payload[1];
  if (interface != PftcNetif::kInterface) {
    serverModeLogger().warnf("GET_NETIF: unsupported interface %u", interface);
    const uint8_t response[] = {kStatusError, kErrorInterfaceNotSupported};
    cable_.sendBlock(response, sizeof(response));
    return;
  }

  serverModeLogger().info("Sending GET_NETIF response");

  const bool connected = WiFi.status() == WL_CONNECTED;
  const IPAddress ip = WiFi.localIP();
  const IPAddress gateway = WiFi.gatewayIP();
  const IPAddress dns = WiFi.dnsIP();
  const uint8_t prefix = subnetPrefixLength(WiFi.subnetMask());
  const uint8_t channel = connected ? static_cast<uint8_t>(WiFi.channel()) : 0;
  const int8_t rssi = connected ? WiFi.RSSI() : static_cast<int8_t>(0x80);
  const char* ssid = WiFi.SSID().c_str();
  const uint8_t ssidLength = static_cast<uint8_t>(strlen(ssid));

  uint8_t response[24 + 32];
  response[0] = kStatusOk;
  response[1] = 0x00;
  response[2] = PftcNetif::kInterface;
  response[3] = PftcNetif::kTypeWifiClient;
  response[4] = 0x01;  // enabled
  response[5] = connected ? 0x02 : 0x00;
  response[6] = ip[0];
  response[7] = ip[1];
  response[8] = ip[2];
  response[9] = ip[3];
  response[10] = prefix;
  response[11] = gateway[0];
  response[12] = gateway[1];
  response[13] = gateway[2];
  response[14] = gateway[3];
  response[15] = dns[0];
  response[16] = dns[1];
  response[17] = dns[2];
  response[18] = dns[3];
  response[19] = 0x00;  // IP mode: DHCP
  response[20] = 0x00;  // IPv6: disabled
  response[21] = channel;
  response[22] = static_cast<uint8_t>(rssi);
  response[23] = ssidLength;
  memcpy(response + 24, ssid, ssidLength);

  const PofoResult result = cable_.sendBlock(response, 24 + ssidLength);
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("sendBlock failed: %u", static_cast<unsigned>(result));
  }
}
