#pragma once

#include <Arduino.h>

// Detects a long press (~3s) of the user button to activate server mode,
// and drives LED2/LED3 to indicate "server mode waiting for Atari" once
// active (both LEDs blinking together). See SERVERPFTC.md for the full LED
// state table - the "communicating" state (both LEDs solid ON) is not
// driven from here, it belongs to the server-mode protocol module once it
// exists.
//
// Does not implement any reversal back to client mode - per SERVERPFTC.md,
// for now that only happens via physical reset.
class ServerModeButton {
 public:
  void begin(int buttonPin);
  void loop();

  bool active() const { return active_; }

 private:
  int buttonPin_ = -1;
  bool active_ = false;

  bool pressed_ = false;
  unsigned long pressStartMs_ = 0;

  unsigned long lastBlinkMs_ = 0;
  bool blinkOn_ = false;
};

extern ServerModeButton serverModeButton;
