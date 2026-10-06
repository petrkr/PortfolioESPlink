#pragma once

// Wraps ArduinoOTA so it can be turned on/off at runtime from the web API
// instead of listening on its UDP port all the time.
void otaControlEnable();
void otaControlDisable();
bool otaControlEnabled();

// Must be called from loop(); a no-op while disabled.
void otaControlLoop();
