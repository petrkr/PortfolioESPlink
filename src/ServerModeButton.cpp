#include "ServerModeButton.h"

#include <log4mcu.h>

#include <board_config.h>

namespace {
constexpr unsigned long kLongPressMs = 3000;
constexpr unsigned long kBlinkIntervalMs = 250;
}  // namespace

ServerModeButton serverModeButton;

static log4mcu::Logger& logger = log4mcu::Logger::get("ServerModeButton");

void ServerModeButton::begin(int buttonPin) {
  buttonPin_ = buttonPin;
  pinMode(buttonPin_, INPUT_PULLUP);
}

void ServerModeButton::loop() {
  if (buttonPin_ < 0) {
    return;
  }

  const bool isPressed = digitalRead(buttonPin_) == LOW;

  if (!active_) {
    if (isPressed && !pressed_) {
      pressStartMs_ = millis();
    } else if (isPressed && millis() - pressStartMs_ >= kLongPressMs) {
      active_ = true;
      logger.info("Server mode activated (long press)");
    }
    pressed_ = isPressed;
    return;
  }

  pressed_ = isPressed;

  if (millis() - lastBlinkMs_ >= kBlinkIntervalMs) {
    lastBlinkMs_ = millis();
    blinkOn_ = !blinkOn_;
    digitalWrite(LED2, blinkOn_ ? 1 : 0);
    digitalWrite(LED3, blinkOn_ ? 1 : 0);
  }
}
