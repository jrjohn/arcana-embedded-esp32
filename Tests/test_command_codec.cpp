#include <gtest/gtest.h>
#include "CommandCodec.hpp"
#include "FrameCodec.hpp"
#include "CommandTypes.hpp"
#include "arcana_cmd.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstring>

using namespace Arcana::Command;

// Tests run with CONFIG_CMD_ENCRYPTION_ENABLED *not* defined, so the codec
// operates in plaintext mode. The encryption path is exercised separately by
// test_key_exchange_manager + test_crypto_engine.

// ── Init ───────────────────────────────────────────────────────────────────

TEST(CommandCodecTest, InitSucceedsInPlaintextMode) {
    CommandCodec codec;
    EXPECT_EQ(codec.Init(), ESP_OK);
}

// ── EncodeResponse ─────────────────────────────────────────────────────────

TEST(CommandCodecTest, EncodeResponseProducesValidFrame) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    CommandResponse rsp{};
    rsp.Source = CommandSource::BLE;
    rsp.ConnectionId = 1;
    rsp.ClusterId = Cluster::System;
    rsp.Command = SystemCmd::Ping;
    rsp.Status = kStatusOk;
    rsp.PayloadLen = 4;
    rsp.Payload[0] = 0xDE;
    rsp.Payload[1] = 0xAD;
    rsp.Payload[2] = 0xBE;
    rsp.Payload[3] = 0xEF;
    rsp.StreamId = 7;
    rsp.Fin = true;

    uint8_t buf[512];
    size_t outLen = 0;
    ASSERT_TRUE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));
    EXPECT_GT(outLen, FrameCodec::kOverhead);

    // Verify frame magic + version
    EXPECT_EQ(buf[0], FrameCodec::kMagic[0]);
    EXPECT_EQ(buf[1], FrameCodec::kMagic[1]);
    EXPECT_EQ(buf[2], FrameCodec::kVersion);
    EXPECT_EQ(buf[3] & FrameCodec::kFlagFin, FrameCodec::kFlagFin);
    EXPECT_EQ(buf[4], 7);  // streamId
}

TEST(CommandCodecTest, EncodeResponseWithSmallBufferFails) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    CommandResponse rsp{};
    rsp.ClusterId = Cluster::System;
    rsp.Command = SystemCmd::Ping;
    rsp.Status = kStatusOk;

    uint8_t tinyBuf[5];
    size_t outLen = 0;
    EXPECT_FALSE(codec.EncodeResponse(rsp, tinyBuf, sizeof(tinyBuf), outLen));
}

TEST(CommandCodecTest, EncodeResponseEmptyPayload) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    CommandResponse rsp{};
    rsp.ClusterId = Cluster::System;
    rsp.Command = SystemCmd::GetDeviceInfo;
    rsp.Status = kStatusOk;
    rsp.PayloadLen = 0;
    rsp.StreamId = 0;
    rsp.Fin = true;

    uint8_t buf[512];
    size_t outLen = 0;
    EXPECT_TRUE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));
}

// ── DecodeRequest round-trip with hand-built frame ─────────────────────────

static size_t buildPlaintextRequestFrame(uint32_t cluster, uint32_t cmd,
                                          const uint8_t* payload, size_t payloadLen,
                                          uint8_t* outFrame, size_t outFrameSize,
                                          uint8_t streamId = 3, uint8_t flags = FrameCodec::kFlagFin) {
    // Encode protobuf
    arcana_CmdRequest msg = arcana_CmdRequest_init_zero;
    msg.cluster = cluster;
    msg.command = cmd;
    if (payloadLen > 0) {
        msg.payload.size = static_cast<pb_size_t>(payloadLen);
        memcpy(msg.payload.bytes, payload, payloadLen);
    }
    uint8_t pbBuf[arcana_CmdRequest_size];
    pb_ostream_t stream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
    if (!pb_encode(&stream, arcana_CmdRequest_fields, &msg)) return 0;
    size_t pbLen = stream.bytes_written;

    // Frame
    size_t outLen = 0;
    if (!FrameCodec::Frame(pbBuf, pbLen, outFrame, outFrameSize, outLen, flags, streamId)) {
        return 0;
    }
    return outLen;
}

TEST(CommandCodecTest, DecodeValidRequestFrame) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    uint8_t frame[512];
    uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t frameLen = buildPlaintextRequestFrame(
        static_cast<uint32_t>(Cluster::System), SystemCmd::Ping,
        payload, sizeof(payload), frame, sizeof(frame), /*sid=*/9);
    ASSERT_GT(frameLen, 0u);

    CommandRequest req;
    EXPECT_TRUE(codec.DecodeRequest(CommandSource::BLE, 11, frame, frameLen, req));
    EXPECT_EQ(req.Source, CommandSource::BLE);
    EXPECT_EQ(req.ConnectionId, 11);
    EXPECT_EQ(req.ClusterId, Cluster::System);
    EXPECT_EQ(req.Command, SystemCmd::Ping);
    EXPECT_EQ(req.PayloadLen, 8);
    EXPECT_EQ(req.StreamId, 9);
    EXPECT_TRUE(req.Fin);
    EXPECT_EQ(memcmp(req.Payload, payload, 8), 0);
}

TEST(CommandCodecTest, DecodeRequestWithEmptyPayload) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    uint8_t frame[256];
    size_t frameLen = buildPlaintextRequestFrame(
        static_cast<uint32_t>(Cluster::System), SystemCmd::GetDeviceInfo,
        nullptr, 0, frame, sizeof(frame));
    ASSERT_GT(frameLen, 0u);

    CommandRequest req;
    EXPECT_TRUE(codec.DecodeRequest(CommandSource::MQTT, 0, frame, frameLen, req));
    EXPECT_EQ(req.PayloadLen, 0);
    EXPECT_EQ(req.ClusterId, Cluster::System);
}

TEST(CommandCodecTest, DecodeRejectsBadMagic) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);
    uint8_t bogus[] = {0x00, 0x00, 0x01, 0x01, 0, 5, 0, 1, 2, 3, 4, 5, 0, 0};
    CommandRequest req;
    EXPECT_FALSE(codec.DecodeRequest(CommandSource::BLE, 0, bogus, sizeof(bogus), req));
}

TEST(CommandCodecTest, DecodeRejectsTooShort) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);
    uint8_t tiny[3] = {0xAC, 0xDA, 0x01};
    CommandRequest req;
    EXPECT_FALSE(codec.DecodeRequest(CommandSource::BLE, 0, tiny, sizeof(tiny), req));
}

TEST(CommandCodecTest, DecodeRejectsBadCrc) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    uint8_t frame[256];
    uint8_t payload[4] = {1, 2, 3, 4};
    size_t frameLen = buildPlaintextRequestFrame(
        static_cast<uint32_t>(Cluster::System), SystemCmd::Ping,
        payload, sizeof(payload), frame, sizeof(frame));
    ASSERT_GT(frameLen, 0u);

    // Corrupt the CRC bytes (last 2 bytes of frame)
    frame[frameLen - 1] ^= 0xFF;
    CommandRequest req;
    EXPECT_FALSE(codec.DecodeRequest(CommandSource::BLE, 0, frame, frameLen, req));
}

TEST(CommandCodecTest, DecodeRejectsCorruptProtobuf) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    // Build a valid frame but with protobuf garbage as payload
    uint8_t garbage[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t frame[256];
    size_t outLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(garbage, sizeof(garbage), frame, sizeof(frame), outLen));

    CommandRequest req;
    EXPECT_FALSE(codec.DecodeRequest(CommandSource::BLE, 0, frame, outLen, req));
}

// ── Round-trip via the public API ──────────────────────────────────────────

TEST(CommandCodecTest, EncodeResponseDecodableAsRequestStructure) {
    // Verify the wire frame produced by EncodeResponse can at least be
    // deframed (CRC + length checks pass). The protobuf payload is a
    // CmdResponse not CmdRequest so we can't decode it as a request.
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    CommandResponse rsp{};
    rsp.ClusterId = Cluster::System;
    rsp.Command = SystemCmd::Ping;
    rsp.Status = kStatusOk;
    rsp.PayloadLen = 0;
    rsp.StreamId = 5;
    rsp.Fin = true;

    uint8_t buf[512];
    size_t outLen = 0;
    ASSERT_TRUE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));

    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    uint8_t flags = 0;
    uint8_t sid = 0;
    EXPECT_TRUE(FrameCodec::Deframe(buf, outLen, payload, payloadLen, flags, sid));
    EXPECT_EQ(sid, 5);
    EXPECT_NE(flags & FrameCodec::kFlagFin, 0);
}

// ── SetKeyExchangeManager (just a setter, smoke test) ──────────────────────

TEST(CommandCodecTest, SetKeyExchangeManagerAcceptsNullptr) {
    CommandCodec codec;
    codec.SetKeyExchangeManager(nullptr);
    EXPECT_EQ(codec.Init(), ESP_OK);
}
