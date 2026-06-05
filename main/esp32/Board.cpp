#include "BoardConfig.hpp"
#include "Ssd1306.hpp"
#include "DhtSensor.hpp"
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

Sensor::ObservableSensor& createSensor() {
    static Sensor::DhtSensor sensor(
        static_cast<gpio_num_t>(CONFIG_DHT_SENSOR_GPIO),
#ifdef CONFIG_DHT_SENSOR_TYPE_DHT22
        Sensor::DhtType::DHT22,
#else
        Sensor::DhtType::DHT11,
#endif
        Sensor::SensorConfig().WithId(1).WithInterval(CONFIG_DHT_SENSOR_READ_INTERVAL_MS)
    );
    return sensor;
}

} // namespace Board
} // namespace Arcana
