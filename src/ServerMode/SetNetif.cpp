#include "ServerMode.h"

namespace {

const uint8_t kStatusOk = 0x20;
const uint8_t kStatusError = 0x10;
const uint8_t kErrorMalformed = 0x02;
const uint8_t kErrorInterfaceNotSupported = 0x04;

// Fixed-size part of the request, before the variable-length SSID/PSK
// tail - see PFTC_PROTOCOL.md's SET_NETIF request body.
constexpr size_t kFixedHeaderLength = 14;

void sendStatus(PofoSmartCable& cable, uint8_t status, uint8_t error) {
  const uint8_t response[] = {status, error};
  const PofoResult result = cable.sendBlock(response, sizeof(response));
  if (result != PofoResult::OK) {
    serverModeLogger().warnf("sendBlock failed: %u", static_cast<unsigned>(result));
  }
}

}  // namespace

void ServerMode::handleSetNetif(const uint8_t* payload, size_t length) {
  // payload[0] is the opcode; the request body starts at payload[1].
  const uint8_t* body = payload + 1;
  const size_t bodyLength = length - 1;

  if (bodyLength < kFixedHeaderLength) {
    serverModeLogger().warn("SET_NETIF: malformed (too short)");
    sendStatus(cable_, kStatusError, kErrorMalformed);
    return;
  }

  const uint8_t interface = body[0];
  const uint8_t type = body[1];
  const uint8_t ipMode = body[3];
  const uint8_t ipv6Enabled = body[13];

  if (interface != PftcNetif::kInterface || type != PftcNetif::kTypeWifiClient) {
    serverModeLogger().warnf("SET_NETIF: unsupported interface=%u type=%u", interface, type);
    sendStatus(cable_, kStatusError, kErrorInterfaceNotSupported);
    return;
  }

  // Static IP and IPv6 are accepted on the wire (per PFTC_PROTOCOL.md's
  // shared addressing-mode fields) but this build only ever runs DHCP,
  // IPv4-only - WiFi.begin() in main.cpp never calls WiFi.config() and
  // IPv6 is never enabled. Report them as unsupported rather than
  // silently ignoring a setting the client thinks it applied.
  if (ipMode != 0x00 || ipv6Enabled != 0x00) {
    serverModeLogger().warn("SET_NETIF: static IP / IPv6 not supported by this build");
    sendStatus(cable_, kStatusError, kErrorInterfaceNotSupported);
    return;
  }

  const uint8_t ssidLength = body[14];
  if (bodyLength < kFixedHeaderLength + 1 + ssidLength + 1) {
    serverModeLogger().warn("SET_NETIF: malformed (SSID/PSK length overruns block)");
    sendStatus(cable_, kStatusError, kErrorMalformed);
    return;
  }
  const uint8_t pskLength = body[kFixedHeaderLength + 1 + ssidLength];
  if (bodyLength < kFixedHeaderLength + 1 + ssidLength + 1 + pskLength) {
    serverModeLogger().warn("SET_NETIF: malformed (PSK length overruns block)");
    sendStatus(cable_, kStatusError, kErrorMalformed);
    return;
  }

  // TODO: apply SSID/PSK via WiFi.begin() once this is wired up for real -
  // parsing/validation only for now, see SERVERPFTC.md.
  serverModeLogger().info("SET_NETIF: accepted (not yet applied)");
  sendStatus(cable_, kStatusOk, 0x00);
}
