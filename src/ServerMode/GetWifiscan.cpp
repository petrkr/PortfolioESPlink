#include "ServerMode.h"

#include <string.h>

#include <WiFi.h>

namespace {

const uint8_t kStatusOk = 0x20;
const uint8_t kStatusError = 0x10;
const uint8_t kErrorInterfaceNotSupported = 0x04;

const uint8_t kSecurityOpen = 0x00;
const uint8_t kSecurityWpa2Psk = 0x01;
const uint8_t kSecurityWpa3Psk = 0x02;
const uint8_t kSecurityWep = 0x03;
const uint8_t kSecurityWpaWpa2 = 0x04;
const uint8_t kSecurityUnknown = 0xFF;

const uint8_t kMaxResults = 10;

uint8_t mapSecurity(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return kSecurityOpen;
    case WIFI_AUTH_WEP:
      return kSecurityWep;
    case WIFI_AUTH_WPA_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
      return kSecurityWpaWpa2;
    case WIFI_AUTH_WPA2_PSK:
      return kSecurityWpa2Psk;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return kSecurityWpa3Psk;
    default:
      return kSecurityUnknown;
  }
}

}  // namespace

void ServerMode::handleGetWifiscan(const uint8_t* payload, size_t length) {
  const uint8_t interface = payload[1];
  if (interface != PftcNetif::kInterface) {
    serverModeLogger().warnf("GET_WIFISCAN: unsupported interface %u", interface);
    const uint8_t response[] = {kStatusError, kErrorInterfaceNotSupported};
    cable_.sendBlock(response, sizeof(response));
    return;
  }

  serverModeLogger().info("Scanning WiFi networks");
  const int16_t found = WiFi.scanNetworks();
  const uint8_t count = found > 0
      ? static_cast<uint8_t>(found > kMaxResults ? kMaxResults : found)
      : 0;

  // 3-byte header + up to 10 entries, each up to 1 + 32 + 1 + 1 bytes.
  uint8_t response[3 + kMaxResults * (1 + 32 + 1 + 1)];
  response[0] = kStatusOk;
  response[1] = 0x00;
  response[2] = count;

  size_t offset = 3;
  for (uint8_t index = 0; index < count; ++index) {
    const String ssid = WiFi.SSID(index);
    const uint8_t ssidLength = static_cast<uint8_t>(
        ssid.length() > 32 ? 32 : ssid.length());

    response[offset++] = ssidLength;
    memcpy(response + offset, ssid.c_str(), ssidLength);
    offset += ssidLength;
    response[offset++] = static_cast<uint8_t>(WiFi.RSSI(index));
    response[offset++] = mapSecurity(WiFi.encryptionType(index));
  }

  WiFi.scanDelete();

  const PofoResult result = cable_.sendBlock(response, offset);
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("sendBlock failed: %u", static_cast<unsigned>(result));
  }
}
