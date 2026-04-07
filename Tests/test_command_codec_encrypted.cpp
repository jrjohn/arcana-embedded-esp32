#include <gtest/gtest.h>
#include "CommandCodec.hpp"
#include "FrameCodec.hpp"
#include "CommandTypes.hpp"
#include "KeyExchangeManager.hpp"
#include "arcana_cmd.pb.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
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

// ── Helper: generate a real client keypair so we can run a session install ─

static bool generateClientKeypair(uint8_t pubOut[64]) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr;
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point Q;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr);
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);

    const char* pers = "codec_encrypted_test";
    bool ok = false;
    do {
        if (mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &entropy,
                                   reinterpret_cast<const unsigned char*>(pers),
                                   strlen(pers)) != 0) break;
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) break;
        if (mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                     mbedtls_ctr_drbg_random, &ctr) != 0) break;
        if (mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(X), pubOut, 32) != 0) break;
        if (mbedtls_mpi_write_binary(&Q.MBEDTLS_PRIVATE(Y), pubOut + 32, 32) != 0) break;
        ok = true;
    } while (false);

    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    mbedtls_ctr_drbg_free(&ctr);
    mbedtls_entropy_free(&entropy);
    return ok;
}

// ── Session-key encrypt path: install a real session, then encode a non-
//    KeyExchange response so it goes through EncryptWithSession (L160-161)
//    and the post-encrypt `if (!encrypted)` PSK fallback is skipped (L166-171
//    bypassed for the success case).

TEST(CommandCodecEncryptedTest, EncodeWithInstalledSessionEncryptsViaSession) {
    // Build a real KeyExchangeManager + install a session for (BLE, 1)
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    for (size_t i = 0; i < sizeof(psk); i++) psk[i] = static_cast<uint8_t>(i);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));
    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 1,
                                          clientPub, serverPub, authTag));
    ASSERT_TRUE(kex.InstallPendingSession(CommandSource::BLE, 1));

    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);
    codec.SetKeyExchangeManager(&kex);

    // Non-KeyExchange response → goes through session encrypt path
    CommandResponse rsp{};
    rsp.Source = CommandSource::BLE;
    rsp.ConnectionId = 1;
    rsp.ClusterId = Cluster::System;
    rsp.Command = SystemCmd::Ping;
    rsp.Status = kStatusOk;
    rsp.PayloadLen = 4;
    rsp.Payload[0] = 0x10; rsp.Payload[1] = 0x20;
    rsp.Payload[2] = 0x30; rsp.Payload[3] = 0x40;
    rsp.StreamId = 9;
    rsp.Fin = true;

    uint8_t buf[512];
    size_t outLen = 0;
    EXPECT_TRUE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));
    EXPECT_GT(outLen, FrameCodec::kOverhead);
}

// ── Session-key decrypt path: encode then decode using the same session ────

TEST(CommandCodecEncryptedTest, RoundTripViaSession) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    for (size_t i = 0; i < sizeof(psk); i++) psk[i] = static_cast<uint8_t>(0xA0 + i);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));
    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 2,
                                          clientPub, serverPub, authTag));
    ASSERT_TRUE(kex.InstallPendingSession(CommandSource::BLE, 2));

    CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);
    codec.SetKeyExchangeManager(&kex);

    // Use EncryptWithSession directly to build a wire request, then deframe
    // by hand to get an encrypted protobuf payload. We send that through
    // DecodeRequest which will call DecryptWithSession (L67) and decode.
    arcana_CmdRequest msg = arcana_CmdRequest_init_zero;
    msg.cluster = static_cast<uint32_t>(Cluster::System);
    msg.command = SystemCmd::Ping;
    msg.payload.size = 0;
    uint8_t pbBuf[arcana_CmdRequest_size];
    pb_ostream_t stream = pb_ostream_from_buffer(pbBuf, sizeof(pbBuf));
    ASSERT_TRUE(pb_encode(&stream, arcana_CmdRequest_fields, &msg));
    size_t pbLen = stream.bytes_written;

    uint8_t encBuf[256];
    size_t encLen = 0;
    ASSERT_TRUE(kex.EncryptWithSession(CommandSource::BLE, 2,
                                         pbBuf, pbLen,
                                         encBuf, sizeof(encBuf), encLen));

    uint8_t frame[300];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(encBuf, encLen, frame, sizeof(frame), frameLen,
                                    FrameCodec::kFlagFin, 1));

    CommandRequest req;
    EXPECT_TRUE(codec.DecodeRequest(CommandSource::BLE, 2, frame, frameLen, req));
    EXPECT_EQ(req.ClusterId, Cluster::System);
    EXPECT_EQ(req.Command, SystemCmd::Ping);
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
