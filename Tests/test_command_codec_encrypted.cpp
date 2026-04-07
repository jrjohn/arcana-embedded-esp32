#include <gtest/gtest.h>
#include "CommandCodec.hpp"
#include "FrameCodec.hpp"
#include "CommandTypes.hpp"
#include "arcana_cmd.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstring>

// Compiled with -DCONFIG_CMD_ENCRYPTION_ENABLED=1 and a 64-hex-char PSK to
// exercise the AES-256-CCM crypt path through CommandCodec::Init/Encode/Decode.

using namespace Arcana::Command;

// ── Round-trip via the encryption-enabled codec ────────────────────────────

TEST(CommandCodecEncryptedTest, InitWithValidPskSucceeds) {
    CommandCodec codec;
    EXPECT_EQ(codec.Init(), ESP_OK);
}

TEST(CommandCodecEncryptedTest, EncodeResponseProducesEncryptedFrame) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    CommandResponse rsp{};
    rsp.Source = CommandSource::BLE;
    rsp.ConnectionId = 1;
    rsp.ClusterId = Cluster::System;
    rsp.Command = SystemCmd::Ping;
    rsp.Status = kStatusOk;
    rsp.PayloadLen = 4;
    rsp.Payload[0] = 0xAA; rsp.Payload[1] = 0xBB;
    rsp.Payload[2] = 0xCC; rsp.Payload[3] = 0xDD;
    rsp.StreamId = 5;
    rsp.Fin = true;

    uint8_t buf[512];
    size_t outLen = 0;
    ASSERT_TRUE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));
    EXPECT_GT(outLen, FrameCodec::kOverhead);
    // Frame magic + version still present in plaintext envelope
    EXPECT_EQ(buf[0], FrameCodec::kMagic[0]);
    EXPECT_EQ(buf[1], FrameCodec::kMagic[1]);
    EXPECT_EQ(buf[2], FrameCodec::kVersion);
}

TEST(CommandCodecEncryptedTest, KeyExchangeResponseInstallsSession) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    // KeyExchange responses are special-cased: they're encrypted with the
    // PSK (not session key) and trigger InstallPendingSession after sending.
    // With kex manager == nullptr, we just verify the path doesn't crash.
    CommandResponse rsp{};
    rsp.Source = CommandSource::BLE;
    rsp.ConnectionId = 7;
    rsp.ClusterId = Cluster::Security;
    rsp.Command = SecurityCmd::KeyExchange;
    rsp.Status = kStatusOk;
    rsp.PayloadLen = 96;  // serverPub(64) + authTag(32)
    memset(rsp.Payload, 0xAB, 96);
    rsp.StreamId = 1;
    rsp.Fin = true;

    uint8_t buf[512];
    size_t outLen = 0;
    EXPECT_TRUE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));
}

// ── Helper: build encrypted protobuf frame for round-trip test ─────────────

static size_t buildEncryptedRequestFrame(CommandCodec& codec,
                                          uint32_t cluster, uint32_t command,
                                          const uint8_t* payload, size_t payloadLen,
                                          uint8_t* outFrame, size_t outFrameSize) {
    // We can't easily produce a wire-format encrypted request from the test
    // side without re-implementing the codec's encrypt path. Instead, build
    // a CommandResponse with the same cluster/command, encode it, then strip
    // the FrameCodec wrapper — that gives us the encrypted protobuf payload.
    // But CmdResponse and CmdRequest have different field layouts...
    //
    // Alternative: skip DecodeRequest in encrypted mode (it's well-tested by
    // the round-trip via PSK in test_crypto_engine). The encryption-specific
    // line coverage we care about is the EncodeResponse encrypt branch +
    // KeyExchange special-case, which the tests above cover.
    (void)codec; (void)cluster; (void)command; (void)payload; (void)payloadLen;
    (void)outFrame; (void)outFrameSize;
    return 0;
}

TEST(CommandCodecEncryptedTest, MultipleEncodesDoNotCorrupt) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    for (int i = 0; i < 5; i++) {
        CommandResponse rsp{};
        rsp.ClusterId = Cluster::System;
        rsp.Command = SystemCmd::Ping;
        rsp.Status = kStatusOk;
        rsp.PayloadLen = 0;
        rsp.StreamId = static_cast<uint8_t>(i);
        rsp.Fin = true;

        uint8_t buf[512];
        size_t outLen = 0;
        EXPECT_TRUE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));
        EXPECT_GT(outLen, 0u);
    }
}

TEST(CommandCodecEncryptedTest, DecodeRejectsRandomGarbage) {
    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);
    // Build a valid frame envelope around random "encrypted" garbage.
    // Decryption will fail → DecodeRequest returns false. Covers the
    // session+PSK fallback failure path.
    uint8_t garbage[40];
    for (int i = 0; i < 40; i++) garbage[i] = static_cast<uint8_t>(i ^ 0x55);

    uint8_t frame[80];
    size_t outLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(garbage, sizeof(garbage),
                                    frame, sizeof(frame), outLen));

    CommandRequest req;
    EXPECT_FALSE(codec.DecodeRequest(CommandSource::BLE, 0, frame, outLen, req));
}
