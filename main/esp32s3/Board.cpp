#include "BoardConfig.hpp"
#include "St7789Lcd.hpp"  // demo
#include "TsensSensor.hpp"  // demo
#include "sdkconfig.h"

namespace Arcana {
namespace Board {

// demo:begin
Lcd::Ssd1306& createDisplay() {
    static Lcd::St7789Lcd display;
    return display;
}

Sensor::ObservableSensor& createSensor() {
    // No ambient temp/humi sensor is wirable on this board — use the S3
    // internal die-temperature sensor (see TsensSensor.hpp).
    static Sensor::TsensSensor sensor(
        Sensor::SensorConfig().WithId(1).WithInterval(CONFIG_DHT_SENSOR_READ_INTERVAL_MS)
    );
    return sensor;
}

// demo:end
} // namespace Board
} // namespace Arcana
