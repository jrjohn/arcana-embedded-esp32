#pragma once

/*
 * Board: ALIENTEK DNESP32S3 (ESP32-S3-WROOM-1 N16R8).
 * One BoardConfig.hpp exists per board directory (esp32/, esp32s3/) —
 * CMake puts exactly one on the include path per IDF_TARGET.
 * Full pin map: docs/DNESP32S3-pinmap.md
 */

// Buttons (active-LOW). No spare directly-wired key: KEY0-3 sit behind the
// XL9555 expander and GPIO5 is the camera's D1 line (pull-up noise there
// fired phantom events) — button A is disabled; BOOT (IO0) is button B.
#define BOARD_BUTTON_A_GPIO     -1
#define BOARD_BUTTON_B_GPIO     0
#define BOARD_BUTTON_B_PULLUP   1    // BOOT key needs the internal pull-up

namespace Arcana {
namespace Lcd { class Ssd1306; }
namespace Sensor { class ObservableSensor; }
namespace Board {

/// Returns the board's display (ST7789 SPI LCD, 320x240, SPILCD socket).
Lcd::Ssd1306& createDisplay();

/// Returns the board's environment sensor.
Sensor::ObservableSensor& createSensor();

} // namespace Board
} // namespace Arcana
