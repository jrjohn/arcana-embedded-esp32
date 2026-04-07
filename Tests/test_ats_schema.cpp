#include <gtest/gtest.h>
#include "ats/ArcanaTsSchema.hpp"
#include <cstring>

using namespace arcana::ats;

// ── FieldDesc layout ────────────────────────────────────────────────────────

TEST(ArcanaTsSchemaTest, FieldDescIs16Bytes) {
    EXPECT_EQ(sizeof(FieldDesc), 16u);
}

// ── fieldSize() (exercised indirectly via addField) ─────────────────────────

TEST(ArcanaTsSchemaTest, AddFieldComputesSizeForAllTypes) {
    ArcanaTsSchema s;
    s.addField("a", FieldType::U8);   // 1
    s.addField("b", FieldType::U16);  // 2
    s.addField("c", FieldType::U32);  // 4
    s.addField("d", FieldType::I16);  // 2
    s.addField("e", FieldType::I32);  // 4
    s.addField("f", FieldType::F32);  // 4
    s.addField("g", FieldType::I24);  // 3
    s.addField("h", FieldType::U64);  // 8
    s.addField("i", FieldType::BYTES, 16);  // 16

    EXPECT_EQ(s.fieldCount, 9);
    EXPECT_EQ(s.recordSize, 1 + 2 + 4 + 2 + 4 + 4 + 3 + 8 + 16);
}

// ── Manual schema construction ──────────────────────────────────────────────

TEST(ArcanaTsSchemaTest, EmptySchemaInitialState) {
    ArcanaTsSchema s;
    EXPECT_EQ(s.fieldCount, 0);
    EXPECT_EQ(s.recordSize, 0);
}

TEST(ArcanaTsSchemaTest, AddFieldComputesOffsetAndRecordSize) {
    ArcanaTsSchema s;
    EXPECT_TRUE(s.addField("ts",   FieldType::U32));
    EXPECT_TRUE(s.addField("temp", FieldType::I16));
    EXPECT_TRUE(s.addField("humi", FieldType::I16));

    EXPECT_EQ(s.fieldCount, 3);
    EXPECT_EQ(s.recordSize, 4 + 2 + 2);
    EXPECT_EQ(s.fields[0].offset, 0);
    EXPECT_EQ(s.fields[1].offset, 4);
    EXPECT_EQ(s.fields[2].offset, 6);
    EXPECT_STREQ(s.fields[0].name, "ts");
    EXPECT_STREQ(s.fields[1].name, "temp");
    EXPECT_STREQ(s.fields[2].name, "humi");
}

TEST(ArcanaTsSchemaTest, AddFieldRespectsMaxFields) {
    ArcanaTsSchema s;
    for (int i = 0; i < ArcanaTsSchema::MAX_FIELDS; i++) {
        EXPECT_TRUE(s.addField("f", FieldType::U8));
    }
    // One more should fail
    EXPECT_FALSE(s.addField("over", FieldType::U8));
}

TEST(ArcanaTsSchemaTest, SetNameTruncates) {
    ArcanaTsSchema s;
    s.setName("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");  // 34 chars
    EXPECT_LE(strlen(s.name), 23u);
}

TEST(ArcanaTsSchemaTest, SchemaIdIsDeterministic) {
    auto a = ArcanaTsSchema::dht11();
    auto b = ArcanaTsSchema::dht11();
    EXPECT_EQ(a.schemaId(), b.schemaId());
}

TEST(ArcanaTsSchemaTest, DifferentSchemasHaveDifferentIds) {
    auto a = ArcanaTsSchema::dht11();
    auto b = ArcanaTsSchema::ads1298_8ch();
    EXPECT_NE(a.schemaId(), b.schemaId());
}

TEST(ArcanaTsSchemaTest, RecordsPerBlockNonZero) {
    auto s = ArcanaTsSchema::dht11();
    EXPECT_GT(s.recordsPerBlock(), 0);
}

TEST(ArcanaTsSchemaTest, RecordsPerBlockZeroForEmptySchema) {
    ArcanaTsSchema s;
    EXPECT_EQ(s.recordsPerBlock(), 0);
}

// ── Predefined sensor schemas ───────────────────────────────────────────────

TEST(ArcanaTsSchemaTest, ADS1298_8ChHas9Fields28Bytes) {
    auto s = ArcanaTsSchema::ads1298_8ch();
    EXPECT_EQ(s.fieldCount, 9);  // ts + 8 channels
    EXPECT_EQ(s.recordSize, 4 + 8*3);  // U32 + 8*I24
    EXPECT_STREQ(s.name, "ADS1298_8CH");
}

TEST(ArcanaTsSchemaTest, MPU6050Schema) {
    auto s = ArcanaTsSchema::mpu6050();
    EXPECT_EQ(s.fieldCount, 5);
    EXPECT_EQ(s.recordSize, 4 + 4 + 2*3);  // U32 + F32 + 3*I16
    EXPECT_STREQ(s.name, "MPU6050");
}

TEST(ArcanaTsSchemaTest, DHT11Schema) {
    auto s = ArcanaTsSchema::dht11();
    EXPECT_EQ(s.fieldCount, 3);
    EXPECT_EQ(s.recordSize, 4 + 2 + 2);
    EXPECT_STREQ(s.name, "DHT11");
}

TEST(ArcanaTsSchemaTest, DeviceStatusSchema) {
    auto s = ArcanaTsSchema::deviceStatus();
    EXPECT_EQ(s.fieldCount, 6);
    EXPECT_STREQ(s.name, "DEVICE_STATUS");
}

TEST(ArcanaTsSchemaTest, GenericAdcSchema) {
    auto s = ArcanaTsSchema::genericAdc();
    EXPECT_EQ(s.fieldCount, 2);
    EXPECT_STREQ(s.name, "GENERIC_ADC");
}

TEST(ArcanaTsSchemaTest, PumpSchema) {
    auto s = ArcanaTsSchema::pump();
    EXPECT_EQ(s.fieldCount, 3);
    EXPECT_STREQ(s.name, "PUMP");
}

// ── Daily operational schemas ───────────────────────────────────────────────

TEST(ArcanaTsSchemaTest, UserActionSchema) {
    auto s = ArcanaTsSchema::userAction();
    EXPECT_EQ(s.fieldCount, 4);
    EXPECT_STREQ(s.name, "USER_ACTION");
}

TEST(ArcanaTsSchemaTest, ErrorLogSchema) {
    auto s = ArcanaTsSchema::errorLog();
    EXPECT_EQ(s.fieldCount, 5);
    EXPECT_STREQ(s.name, "ERROR_LOG");
}

TEST(ArcanaTsSchemaTest, ConfigSnapshotSchema) {
    auto s = ArcanaTsSchema::configSnapshot();
    EXPECT_EQ(s.fieldCount, 8);
    EXPECT_STREQ(s.name, "CONFIG_SNAPSHOT");
}

// ── Device lifecycle schemas ────────────────────────────────────────────────

TEST(ArcanaTsSchemaTest, DeviceInfoSchema) {
    auto s = ArcanaTsSchema::deviceInfo();
    EXPECT_GE(s.fieldCount, 5);
    EXPECT_STREQ(s.name, "DEVICE_INFO");
}

TEST(ArcanaTsSchemaTest, LifecycleEventSchema) {
    auto s = ArcanaTsSchema::lifecycleEvent();
    EXPECT_GE(s.fieldCount, 4);
    EXPECT_STREQ(s.name, "LIFECYCLE");
}

TEST(ArcanaTsSchemaTest, AllPredefinedSchemasAreValid) {
    // Just instantiate all factories — exercise their code paths for coverage
    auto s1  = ArcanaTsSchema::ads1298_8ch();
    auto s2  = ArcanaTsSchema::mpu6050();
    auto s3  = ArcanaTsSchema::dht11();
    auto s4  = ArcanaTsSchema::deviceStatus();
    auto s5  = ArcanaTsSchema::genericAdc();
    auto s6  = ArcanaTsSchema::pump();
    auto s7  = ArcanaTsSchema::userAction();
    auto s8  = ArcanaTsSchema::errorLog();
    auto s9  = ArcanaTsSchema::configSnapshot();
    auto s10 = ArcanaTsSchema::deviceInfo();
    auto s11 = ArcanaTsSchema::lifecycleEvent();

    for (const auto* s : {&s1,&s2,&s3,&s4,&s5,&s6,&s7,&s8,&s9,&s10,&s11}) {
        EXPECT_GT(s->fieldCount, 0);
        EXPECT_GT(s->recordSize, 0);
        EXPECT_GT(s->recordsPerBlock(), 0);
        EXPECT_NE(s->schemaId(), 0u);
    }
}

// ── CRC32 (used by schemaId) ────────────────────────────────────────────────

TEST(Crc32Test, EmptyDataReturnsInit) {
    EXPECT_EQ(arcana::ats::crc32(0xFFFFFFFF, nullptr, 0), 0xFFFFFFFFu);
}

TEST(Crc32Test, KnownValue) {
    // CRC-32 IEEE of "123456789" (raw, not string-terminated) is 0xCBF43926
    // ~0x376E6E7 (init=0xFFFFFFFF) — so check inversion
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    uint32_t result = ~arcana::ats::crc32(0xFFFFFFFF, data, 9);
    EXPECT_EQ(result, 0xCBF43926u);
}

TEST(Crc32Test, DifferentDataDifferentCrc) {
    uint8_t a[] = {1,2,3};
    uint8_t b[] = {1,2,4};
    EXPECT_NE(arcana::ats::crc32(0xFFFFFFFF, a, 3),
              arcana::ats::crc32(0xFFFFFFFF, b, 3));
}
