#pragma once

/*
 * Board: classic ESP32 DevKit + EZ Start Kit (breadboard wiring).
 * One BoardConfig.hpp exists per board directory (esp32/, esp32s3/) —
 * CMake puts exactly one on the include path per IDF_TARGET.
 */

// Buttons (active-LOW)
#define BOARD_BUTTON_A_GPIO     5    // momentary key, internal pull-up
#define BOARD_BUTTON_B_GPIO     36   // input-only pin, external pull-up only
#define BOARD_BUTTON_B_PULLUP   0    // GPIO36 has no internal pull-up hardware

namespace Arcana {
namespace Lcd { class Ssd1306; }
namespace Sensor { class ObservableSensor; }
namespace Board {

/// Returns the board's display (SSD1306 OLED on I2C, 128x64).
Lcd::Ssd1306& createDisplay();

/// Returns the board's environment sensor.
Sensor::ObservableSensor& createSensor();

} // namespace Board
} // namespace Arcana
