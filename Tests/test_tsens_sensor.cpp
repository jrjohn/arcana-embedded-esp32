// TsensSensor — S3 internal temperature sensor as ObservableSensor backend.
// Fakes the esp_driver_tsens API; verifies the SensorData mapping contract
// (scaling, humidity-not-available, quality downgrade) and failure paths.

#include <gtest/gtest.h>
#include "TsensSensor.hpp"

// ── Fake esp_driver_tsens ───────────────────────────────────────────────────
namespace {
float gCelsius = 0.0f;
esp_err_t gInstallResult = ESP_OK;
esp_err_t gReadResult = ESP_OK;
int gInstalls = 0, gUninstalls = 0;
}

extern "C" {
esp_err_t temperature_sensor_install(const temperature_sensor_config_t*,
                                     temperature_sensor_handle_t* h) {
    if (gInstallResult != ESP_OK) return gInstallResult;
    gInstalls++;
    *h = (void*)0x1;
    return ESP_OK;
}
esp_err_t temperature_sensor_enable(temperature_sensor_handle_t)  { return ESP_OK; }
esp_err_t temperature_sensor_disable(temperature_sensor_handle_t) { return ESP_OK; }
esp_err_t temperature_sensor_uninstall(temperature_sensor_handle_t) {
    gUninstalls++; return ESP_OK;
}
esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t, float* out) {
    if (gReadResult != ESP_OK) return gReadResult;
    *out = gCelsius;
    return ESP_OK;
}
}

using namespace Arcana::Sensor;

// Expose the protected hardware hook for direct testing
struct TestableTsens : TsensSensor {
    using TsensSensor::TsensSensor;
    using TsensSensor::ReadHardware;
};

TEST(TsensSensorTest, ReadMapsDataContract) {
    gCelsius = 42.5f;
    TestableTsens sensor(SensorConfig().WithId(7).WithInterval(2000));

    SensorData d;
    ASSERT_EQ(sensor.ReadHardware(d), ESP_OK);
    EXPECT_FLOAT_EQ(d.Temperature, 42.5f);
    EXPECT_FLOAT_EQ(d.Humidity, 0.0f);      // not available on this board
    EXPECT_EQ(d.Value, 4250);               // centi-degrees
    EXPECT_EQ(d.SensorId, 7);
    EXPECT_EQ(d.Quality, 70);               // die temp, not ambient
}

TEST(TsensSensorTest, NegativeTemperatureScales) {
    gCelsius = -3.25f;
    TestableTsens sensor(SensorConfig().WithId(1));

    SensorData d;
    ASSERT_EQ(sensor.ReadHardware(d), ESP_OK);
    EXPECT_EQ(d.Value, -325);
}

TEST(TsensSensorTest, ReadFailurePropagates) {
    gReadResult = ESP_FAIL;
    TestableTsens sensor{SensorConfig()};

    SensorData d;
    EXPECT_EQ(sensor.ReadHardware(d), ESP_FAIL);
    gReadResult = ESP_OK;
}

TEST(TsensSensorTest, InstallFailureLeavesSensorInert) {
    gInstallResult = ESP_FAIL;
    TestableTsens sensor{SensorConfig()};

    SensorData d;
    EXPECT_EQ(sensor.ReadHardware(d), ESP_ERR_INVALID_STATE);
    gInstallResult = ESP_OK;
}

TEST(TsensSensorTest, DestructorUninstallsWhenInstalled) {
    int before = gUninstalls;
    { TestableTsens sensor{SensorConfig()}; }
    EXPECT_EQ(gUninstalls, before + 1);
}
