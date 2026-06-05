#include "BoardConfig.hpp"
#include "Ssd1306.hpp"
#include "sdkconfig.h"

namespace Arcana {
namespace Board {

Lcd::Ssd1306& createDisplay() {
    static Lcd::Ssd1306 display(
        static_cast<gpio_num_t>(CONFIG_OLED_SCL_GPIO),
        static_cast<gpio_num_t>(CONFIG_OLED_SDA_GPIO),
        CONFIG_OLED_I2C_ADDR
    );
    return display;
}

} // namespace Board
} // namespace Arcana
