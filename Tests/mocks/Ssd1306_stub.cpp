// Stub implementation of Ssd1306 — links into test_main_view to satisfy
// the real Ssd1306.hpp without needing ESP-IDF I2C driver.

#include <cstddef>   // size_t (real Ssd1306.hpp uses but doesn't include it itself)
#include <cstdint>
#include "Ssd1306.hpp"
#include <vector>
#include <string>
#include <utility>

// Public counters (shared with test_main_view.cpp via extern declaration)
struct Ssd1306Counters {
    int clearCount = 0;
    int displayCount = 0;
    int drawCount = 0;
    std::vector<std::pair<int, std::string>> drawnStrings;
};
Ssd1306Counters g_ssdCounters;

namespace Arcana {
namespace Lcd {

Ssd1306::Ssd1306(gpio_num_t sclPin, gpio_num_t sdaPin, uint8_t addr)
    : mSclPin(sclPin), mSdaPin(sdaPin), mAddr(addr) {}

Ssd1306::~Ssd1306() {}

esp_err_t Ssd1306::Init() { return ESP_OK; }
esp_err_t Ssd1306::SendCommand(uint8_t) { return ESP_OK; }
esp_err_t Ssd1306::SendCommands(const uint8_t*, size_t) { return ESP_OK; }

void Ssd1306::Clear() {
    g_ssdCounters.clearCount++;
    g_ssdCounters.drawnStrings.clear();
}

void Ssd1306::SetCursor(uint8_t col, uint8_t page) {
    mCursorCol = col;
    mCursorPage = page;
}

void Ssd1306::DrawChar(char) {}

void Ssd1306::DrawString(const char*) {}

void Ssd1306::DrawStringAt(uint8_t /*col*/, uint8_t page, const char* str) {
    g_ssdCounters.drawCount++;
    g_ssdCounters.drawnStrings.emplace_back(static_cast<int>(page), std::string(str));
}

void Ssd1306::Display() { g_ssdCounters.displayCount++; }

} // namespace Lcd
} // namespace Arcana
