// Xl9555 expander driver — register-shadow logic against a fake I2C bus.
//
// The driver caches OUT/CFG shadows so pin ops are single transactions and
// unchanged ports are never rewritten. That logic (not the I2C transport)
// is what these tests pin down: a stale shadow silently breaks LCD power /
// camera reset sequencing on the DNESP32S3.

#include <gtest/gtest.h>
#include <cstring>
#include "Xl9555.hpp"

// ── Fake I2C register file ──────────────────────────────────────────────────
namespace {
uint8_t  gRegs[8];          // 0/1 in, 2/3 out, 6/7 cfg
int      gWriteCount[8];
int      gReadCount = 0;
bool     gFailTransmit = false;
}

extern "C" {
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t* bus) {
    *bus = (void*)0x1; return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t*,
                                    i2c_master_dev_handle_t* dev) {
    *dev = (void*)0x2; return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t* data, size_t len, int) {
    if (gFailTransmit) return ESP_FAIL;
    if (len == 2 && data[0] < 8) {
        gRegs[data[0]] = data[1];
        gWriteCount[data[0]]++;
        return ESP_OK;
    }
    return ESP_FAIL;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t* tx, size_t,
                                      uint8_t* rx, size_t rx_len, int) {
    gReadCount++;
    for (size_t i = 0; i < rx_len; i++) rx[i] = gRegs[(tx[0] + i) % 8];
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t) { return ESP_OK; }
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t) { return ESP_OK; }
}

using Arcana::Io::Xl9555;

// NOTE: Xl9555 is a singleton — tests run in declaration order and share
// driver state, mirroring how the device accumulates state on real hardware.

TEST(Xl9555Test, InitReadsInputsAndBecomesReady) {
    memset(gRegs, 0xFF, sizeof(gRegs));
    gRegs[0] = 0xFD;  // QMA_INT low — realistic power-on snapshot
    auto& xl = Xl9555::getInstance();

    EXPECT_EQ(xl.init(), ESP_OK);
    EXPECT_TRUE(xl.isReady());
    EXPECT_GE(gReadCount, 1);  // input read = INT release

    EXPECT_EQ(xl.init(), ESP_OK);  // idempotent
}

TEST(Xl9555Test, PinModeWritesOnlyTheChangedPort) {
    auto& xl = Xl9555::getInstance();
    memset(gWriteCount, 0, sizeof(gWriteCount));

    // SLCD_PWR|SLCD_RST are port-1 bits — config reg 0x07 only
    EXPECT_EQ(xl.pinMode(Xl9555::kSlcdPwr | Xl9555::kSlcdRst, false), ESP_OK);
    EXPECT_EQ(gWriteCount[0x06], 0);             // port-0 cfg untouched
    EXPECT_EQ(gWriteCount[0x07], 1);
    EXPECT_EQ(gRegs[0x07], 0xFF & ~0x0C);        // bits 2,3 now outputs
}

TEST(Xl9555Test, PinWriteSkipsWhenShadowUnchanged) {
    auto& xl = Xl9555::getInstance();
    memset(gWriteCount, 0, sizeof(gWriteCount));

    // Power-on default is already high — no I2C traffic expected
    EXPECT_EQ(xl.pinWrite(Xl9555::kSlcdPwr, true), ESP_OK);
    EXPECT_EQ(gWriteCount[0x02], 0);
    EXPECT_EQ(gWriteCount[0x03], 0);
}

TEST(Xl9555Test, PinWriteDrivesLowThenHigh) {
    auto& xl = Xl9555::getInstance();
    memset(gWriteCount, 0, sizeof(gWriteCount));

    EXPECT_EQ(xl.pinWrite(Xl9555::kSlcdRst, false), ESP_OK);  // reset low
    EXPECT_EQ(gWriteCount[0x03], 1);
    EXPECT_EQ(gRegs[0x03], 0xFF & ~0x04);

    EXPECT_EQ(xl.pinWrite(Xl9555::kSlcdRst, true), ESP_OK);   // release
    EXPECT_EQ(gWriteCount[0x03], 2);
    EXPECT_EQ(gRegs[0x03], 0xFF);

    EXPECT_EQ(gWriteCount[0x02], 0);  // port-0 out never touched
}

TEST(Xl9555Test, ReadInputsCombinesPortsLittleEndian) {
    auto& xl = Xl9555::getInstance();
    gRegs[0] = 0x12;
    gRegs[1] = 0x34;

    uint16_t v = 0;
    EXPECT_EQ(xl.readInputs(v), ESP_OK);
    EXPECT_EQ(v, 0x3412);  // in0 = low byte, in1 = high byte
}

TEST(Xl9555Test, TransmitFailureSurfacesAndKeepsShadow) {
    auto& xl = Xl9555::getInstance();
    gFailTransmit = true;

    EXPECT_NE(xl.pinWrite(Xl9555::kBeep, false), ESP_OK);  // port-0 write fails

    gFailTransmit = false;
    memset(gWriteCount, 0, sizeof(gWriteCount));
    // Shadow must NOT have absorbed the failed write — retry really writes
    EXPECT_EQ(xl.pinWrite(Xl9555::kBeep, false), ESP_OK);
    EXPECT_EQ(gWriteCount[0x02], 1);
}
