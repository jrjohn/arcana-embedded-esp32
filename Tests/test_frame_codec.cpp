#include <gtest/gtest.h>
#include "FrameCodec.hpp"
#include <cstring>

using namespace Arcana::Command;

// Helper to build a valid frame for test input
static bool makeFrame(const uint8_t* payload, size_t payloadLen,
                      uint8_t* buf, size_t bufSize, size_t& outLen,
                      uint8_t flags = FrameCodec::kFlagFin,
                      uint8_t sid = FrameCodec::kSidNone) {
    return FrameCodec::Frame(payload, payloadLen, buf, bufSize, outLen, flags, sid);
}

// ── Frame() tests ────────────────────────────────────────────────────────────

TEST(FrameCodecTest, FrameSucceedsWithPayload) {
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t buf[64];
    size_t outLen = 0;
    EXPECT_TRUE(FrameCodec::Frame(payload, 3, buf, sizeof(buf), outLen));
    EXPECT_EQ(outLen, 3 + FrameCodec::kOverhead);
}

TEST(FrameCodecTest, FrameFailsIfBufferTooSmall) {
    uint8_t payload[] = {0xAA, 0xBB};
    uint8_t buf[5];  // Too small (need 2 + 9 = 11)
    size_t outLen = 0;
    EXPECT_FALSE(FrameCodec::Frame(payload, 2, buf, sizeof(buf), outLen));
}

TEST(FrameCodecTest, FrameSetsMagicBytes) {
    uint8_t payload[] = {0x42};
    uint8_t buf[32];
    size_t outLen = 0;
    ASSERT_TRUE(makeFrame(payload, 1, buf, sizeof(buf), outLen));
    EXPECT_EQ(buf[0], FrameCodec::kMagic[0]);
    EXPECT_EQ(buf[1], FrameCodec::kMagic[1]);
    EXPECT_EQ(buf[2], FrameCodec::kVersion);
}

TEST(FrameCodecTest, FrameSetsFlagsAndSid) {
    uint8_t payload[] = {0x01};
    uint8_t buf[32];
    size_t outLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(payload, 1, buf, sizeof(buf), outLen,
                                  FrameCodec::kFlagFin, 0x05));
    EXPECT_EQ(buf[3], FrameCodec::kFlagFin);  // flags offset
    EXPECT_EQ(buf[4], 0x05);                  // sid offset
}

TEST(FrameCodecTest, FrameLengthLittleEndian) {
    // 256 bytes payload to test LE encoding (0x0100)
    uint8_t payload[256];
    memset(payload, 0xAB, sizeof(payload));
    uint8_t buf[300];
    size_t outLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(payload, 256, buf, sizeof(buf), outLen));
    EXPECT_EQ(buf[5], 0x00);  // len LSB
    EXPECT_EQ(buf[6], 0x01);  // len MSB
}

// ── Deframe() tests ──────────────────────────────────────────────────────────

TEST(FrameCodecTest, RoundTripPayloadRecovered) {
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t buf[64];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 4, buf, sizeof(buf), frameLen));

    const uint8_t* outPayload = nullptr;
    size_t outLen = 0;
    uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::Deframe(buf, frameLen, outPayload, outLen, flags, sid));
    EXPECT_EQ(outLen, 4u);
    EXPECT_EQ(memcmp(outPayload, payload, 4), 0);
    EXPECT_EQ(flags, FrameCodec::kFlagFin);
}

TEST(FrameCodecTest, DeframeRejectsZeroLengthPayload) {
    // ESP32 FrameCodec rejects len=0 (security hardening)
    uint8_t buf[16];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(nullptr, 0, buf, sizeof(buf), frameLen));

    const uint8_t* p = nullptr;
    size_t len = 99;
    uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, frameLen, p, len, f, s));
}

TEST(FrameCodecTest, DeframeFailsOnCorruptedCRC) {
    uint8_t payload[] = {0x11, 0x22};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 2, buf, sizeof(buf), frameLen));
    buf[frameLen - 1] ^= 0xFF;  // Corrupt last CRC byte

    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, frameLen, p, len, f, s));
}

TEST(FrameCodecTest, DeframeFailsOnWrongMagic) {
    uint8_t payload[] = {0x55};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 1, buf, sizeof(buf), frameLen));
    buf[0] = 0x00;  // Wrong magic0

    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, frameLen, p, len, f, s));
}

TEST(FrameCodecTest, DeframeFailsOnWrongVersion) {
    uint8_t payload[] = {0x55};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 1, buf, sizeof(buf), frameLen));
    buf[2] = 0xFF;  // Wrong version

    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, frameLen, p, len, f, s));
}

TEST(FrameCodecTest, DeframeFailsOnTooShortBuffer) {
    uint8_t buf[4] = {0xAC, 0xDA, 0x01, 0x00};  // Incomplete header
    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, 4, p, len, f, s));
}

TEST(FrameCodecTest, DeframeFailsOnLengthMismatch) {
    uint8_t payload[] = {0x11, 0x22, 0x33};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 3, buf, sizeof(buf), frameLen));
    // Truncate frame to drop CRC
    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, frameLen - 5, p, len, f, s));
}

TEST(FrameCodecTest, DeframeRejectsOversizedPayload) {
    // Manually craft a frame claiming 500-byte payload (> kMaxPayloadLen=300)
    uint8_t buf[16];
    buf[0] = 0xAC; buf[1] = 0xDA; buf[2] = 0x01;  // magic + ver
    buf[3] = 0x01; buf[4] = 0x00;                  // flags + sid
    buf[5] = 0xF4; buf[6] = 0x01;                  // len = 500 LE
    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, sizeof(buf), p, len, f, s));
}

TEST(FrameCodecTest, RoundTripStreamId) {
    uint8_t payload[] = {0xAB};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(payload, 1, buf, sizeof(buf), frameLen,
                                  FrameCodec::kFlagFin, 0x42));

    const uint8_t* p = nullptr; size_t len = 0; uint8_t flags = 0, sid = 0;
    ASSERT_TRUE(FrameCodec::Deframe(buf, frameLen, p, len, flags, sid));
    EXPECT_EQ(sid, 0x42);
}

TEST(FrameCodecTest, PayloadCorruptionDetected) {
    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t buf[32];
    size_t frameLen = 0;
    ASSERT_TRUE(makeFrame(payload, 3, buf, sizeof(buf), frameLen));
    buf[FrameCodec::kHeaderLen + 1] ^= 0x01;  // Flip bit in payload

    const uint8_t* p = nullptr; size_t len = 0; uint8_t f = 0, s = 0;
    EXPECT_FALSE(FrameCodec::Deframe(buf, frameLen, p, len, f, s));
}

// ── CRC16 direct tests ──────────────────────────────────────────────────────

TEST(FrameCodecTest, Crc16OfEmptyIsZero) {
    EXPECT_EQ(FrameCodec::crc16(nullptr, 0), 0u);
}

TEST(FrameCodecTest, Crc16Deterministic) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t a = FrameCodec::crc16(data, 4);
    uint16_t b = FrameCodec::crc16(data, 4);
    EXPECT_EQ(a, b);
}

TEST(FrameCodecTest, Crc16ChangesOnDataChange) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};
    EXPECT_NE(FrameCodec::crc16(data1, 3), FrameCodec::crc16(data2, 3));
}
