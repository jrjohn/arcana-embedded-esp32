#include "BoardConfig.hpp"
#include "St7789Lcd.hpp"

namespace Arcana {
namespace Board {

Lcd::Ssd1306& createDisplay() {
    static Lcd::St7789Lcd display;
    return display;
}

} // namespace Board
} // namespace Arcana
