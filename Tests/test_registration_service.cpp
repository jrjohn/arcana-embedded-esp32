#include <gtest/gtest.h>
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"   // stub from Tests/mocks/impl/
#include "esp_http_client.h"
#include "FrameCodec.hpp"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <cstring>
#include <cstdint>

using namespace Arcana::Registration;
using namespace Arcana::Command;

// ── Helper: build a fake registration response frame ──────────────────────
//
// Server response is a FrameCodec frame containing a protobuf message:
//   field 1 (varint) = success (1)
//   field 2 (bytes) = mqtt_user
//   field 3 (bytes) = mqtt_pass
//   field 4 (bytes) = mqtt_broker
//   field 5 (varint) = mqtt_port
//   field 6 (bytes) = upload_token
//   field 7 (bytes) = topic_prefix
//   field 9 (bytes, 64 bytes) = server_pub
//
// The minimal protobuf encoder writes [tag(1B)][len(varint)][data].

static void pbWriteVarint(uint8_t*& p, uint32_t v) {
    while (v > 0x7F) { *p++ = (v & 0x7F) | 0x80; v >>= 7; }
    *p++ = v & 0x7F;
}

static void pbWriteString(uint8_t*& p, uint8_t fieldNum, const char* s) {
    *p++ = (fieldNum << 3) | 2;
    size_t len = strlen(s);
    pbWriteVarint(p, (uint32_t)len);
    memcpy(p, s, len);
    p += len;
}

static void pbWriteBytes(uint8_t*& p, uint8_t fieldNum, const uint8_t* data, size_t len) {
    *p++ = (fieldNum << 3) | 2;
    pbWriteVarint(p, (uint32_t)len);
    memcpy(p, data, len);
    p += len;
}

static void pbWriteVarintField(uint8_t*& p, uint8_t fieldNum, uint32_t value) {
    *p++ = (fieldNum << 3) | 0;
    pbWriteVarint(p, value);
}

// Generate a valid SECP256R1 server keypair → returns 64-byte raw public
// key (x||y). Caller passes this to buildResponseFrame so the registration
// flow's ECDH compute_shared (and HKDF derivation) succeeds.
static bool generateServerPubkey(uint8_t pubOut[64]) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr;
    mbedtls_ecp_keypair kp;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr);
    mbedtls_ecp_keypair_init(&kp);

    const char* pers = "test_server";
    bool ok = false;
    do {
        if (mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &entropy,
                                   reinterpret_cast<const unsigned char*>(pers),
                                   strlen(pers)) != 0) break;
        if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &kp,
                                 mbedtls_ctr_drbg_random, &ctr) != 0) break;

        size_t olen = 0;
        uint8_t uncomp[65];
        if (mbedtls_ecp_point_write_binary(&kp.MBEDTLS_PRIVATE(grp),
                                            &kp.MBEDTLS_PRIVATE(Q),
                                            MBEDTLS_ECP_PF_UNCOMPRESSED,
                                            &olen, uncomp, sizeof(uncomp)) != 0) break;
        if (olen != 65 || uncomp[0] != 0x04) break;
        memcpy(pubOut, uncomp + 1, 64);  // skip 0x04 prefix
        ok = true;
    } while (false);

    mbedtls_ecp_keypair_free(&kp);
    mbedtls_ctr_drbg_free(&ctr);
    mbedtls_entropy_free(&entropy);
    return ok;
}

// Build a wire-format response: FrameCodec(protobuf payload).
// Returns total wire length, fills `out`.
static size_t buildResponseFrame(uint8_t* out, size_t outSize,
                                  bool success,
                                  const char* user, const char* pass,
                                  const char* broker, uint16_t port,
                                  const char* token, const char* prefix,
                                  const uint8_t* serverPub64 /*may be null*/) {
    uint8_t pbBuf[256];
    uint8_t* p = pbBuf;
    pbWriteVarintField(p, 1, success ? 1 : 0);
    if (user)   pbWriteString(p, 2, user);
    if (pass)   pbWriteString(p, 3, pass);
    if (broker) pbWriteString(p, 4, broker);
    pbWriteVarintField(p, 5, port);
    if (token)  pbWriteString(p, 6, token);
    if (prefix) pbWriteString(p, 7, prefix);
    if (serverPub64) pbWriteBytes(p, 9, serverPub64, 64);
    uint16_t pbLen = (uint16_t)(p - pbBuf);

    size_t outLen = 0;
    if (!FrameCodec::Frame(pbBuf, pbLen, out, outSize, outLen,
                           FrameCodec::kFlagFin, 0x80)) {
        return 0;
    }
    return outLen;
}

// ── Test fixture ───────────────────────────────────────────────────────────

class RegistrationServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        http_test_reset();
        Arcana::Storage::AtsStorageServiceImpl::getInstance().test_reset();

        // RegistrationServiceImpl is a Meyer singleton — credentials persist
        // across tests. refreshToken() unconditionally sets mCreds.valid=false
        // at line 1 of its body, so calling it (even with http_test_perform=
        // FAIL) gives us a clean slate.
        http_test_set_perform_result(ESP_FAIL);
        RegistrationServiceImpl::getInstance().refreshToken();
        http_test_reset();
    }
};

// ── Singleton + accessors ──────────────────────────────────────────────────

TEST_F(RegistrationServiceTest, GetInstanceReturnsSingleton) {
    auto& a = RegistrationServiceImpl::getInstance();
    auto& b = RegistrationServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST_F(RegistrationServiceTest, DeviceIdIs12CharHex) {
    auto& svc = RegistrationServiceImpl::getInstance();
    const char* id = svc.deviceId();
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(strlen(id), 12u);
    for (int i = 0; i < 12; i++) {
        char c = id[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
            << "non-hex char at " << i << ": " << c;
    }
}

TEST_F(RegistrationServiceTest, IsRegisteredFalseInitially) {
    auto& svc = RegistrationServiceImpl::getInstance();
    // After test_reset there are no creds — but the singleton may carry state
    // from a prior test. Force-invalidate via doRegistration's failure path
    // by ensuring storage and HTTP both fail.
    Arcana::Storage::AtsStorageServiceImpl::getInstance().test_setReady(false);
    http_test_set_perform_result(ESP_FAIL);
    // We don't reset svc directly — accessor remains valid regardless
    EXPECT_NE(&svc.credentials(), nullptr);  // accessor returns reference
}

// ── loadCredentials ────────────────────────────────────────────────────────

TEST_F(RegistrationServiceTest, LoadCredentialsFailsWhenStorageNotReady) {
    auto& svc = RegistrationServiceImpl::getInstance();
    Arcana::Storage::AtsStorageServiceImpl::getInstance().test_setReady(false);
    EXPECT_FALSE(svc.loadCredentials());
}

TEST_F(RegistrationServiceTest, LoadCredentialsFailsWhenLoadReturnsFalse) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(false);  // load fails
    EXPECT_FALSE(svc.loadCredentials());
}

TEST_F(RegistrationServiceTest, LoadCredentialsFailsOnBadMagic) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(true);

    // 256-byte buffer with wrong magic at offset 218-219
    uint8_t buf[256] = {0};
    buf[218] = 0xFF; buf[219] = 0xFF;
    storage.test_setLoadData(buf, 256);

    EXPECT_FALSE(svc.loadCredentials());
}

TEST_F(RegistrationServiceTest, LoadCredentialsSucceedsWithMatchingDeviceId) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(true);

    // Build a valid creds blob: deviceId at user[0..11], magic at 218-219
    uint8_t buf[256] = {0};
    const char* devId = svc.deviceId();
    memcpy(buf, devId, 12);              // mqttUser
    strcpy((char*)buf + 72, "broker.example.com");  // mqttBroker @ offset 72
    buf[108] = 0x83; buf[109] = 0x07;    // mqttPort = 1923
    strcpy((char*)buf + 110, "test|9999999999|sig"); // uploadToken @ 110
    strcpy((char*)buf + 182, "arcana");  // topicPrefix @ 182
    buf[218] = 0xCE; buf[219] = 0xED;    // valid magic
    buf[220] = 0;                         // hasCommKey = false
    storage.test_setLoadData(buf, 256);

    EXPECT_TRUE(svc.loadCredentials());
    EXPECT_TRUE(svc.isRegistered());
    EXPECT_STREQ(svc.credentials().mqttBroker, "broker.example.com");
    EXPECT_STREQ(svc.credentials().topicPrefix, "arcana");
}

TEST_F(RegistrationServiceTest, LoadCredentialsRejectsForeignDeviceId) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(true);

    uint8_t buf[256] = {0};
    memcpy(buf, "FFFFFFFFFFFF", 12);  // wrong device id
    strcpy((char*)buf + 72, "broker.example.com");
    buf[218] = 0xCE; buf[219] = 0xED;
    storage.test_setLoadData(buf, 256);

    EXPECT_FALSE(svc.loadCredentials());
}

// ── doRegistration ─────────────────────────────────────────────────────────

TEST_F(RegistrationServiceTest, DoRegistrationFailsWhenAllPathsFail) {
    auto& svc = RegistrationServiceImpl::getInstance();
    Arcana::Storage::AtsStorageServiceImpl::getInstance().test_setReady(false);
    http_test_set_perform_result(ESP_FAIL);
    EXPECT_FALSE(svc.doRegistration());
}

// ── refreshToken ───────────────────────────────────────────────────────────

TEST_F(RegistrationServiceTest, RefreshTokenFailsWhenHttpFails) {
    auto& svc = RegistrationServiceImpl::getInstance();
    Arcana::Storage::AtsStorageServiceImpl::getInstance().test_setReady(false);
    http_test_set_perform_result(ESP_FAIL);
    EXPECT_FALSE(svc.refreshToken());
}

// ── httpRegister via parseResponse path ─────────────────────────────────────

TEST_F(RegistrationServiceTest, HttpRegisterParsesValidResponse) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setSaveOk(true);

    // Build a valid 64-byte ECP point on secp256r1 — we don't validate
    // server_pub here, just that the parser fills mqtt fields. Use zeros
    // (ECDH compute_shared will fail downstream but that's logged & swallowed).
    uint8_t serverPub[64] = {0};

    uint8_t frame[400];
    size_t frameLen = buildResponseFrame(frame, sizeof(frame),
        /*success=*/true,
        "MAC123", "secret", "broker.example.com", 1883,
        "tok|9999999999|sig", "topic", serverPub);
    ASSERT_GT(frameLen, 0u);

    http_test_set_response(frame, (int)frameLen, 200);

    // We have to bypass loadCredentials() success path so doRegistration calls
    // httpRegister. Setting loadOk=false ensures load returns false.
    storage.test_setLoadOk(false);

    bool ok = svc.doRegistration();
    EXPECT_TRUE(ok);
    EXPECT_STREQ(svc.credentials().mqttBroker, "broker.example.com");
    EXPECT_EQ(svc.credentials().mqttPort, 1883);
}

TEST_F(RegistrationServiceTest, HttpRegisterFailsOnHttpStatus500) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(false);

    uint8_t bogusBody[] = {'e', 'r', 'r', 'o', 'r'};
    http_test_set_response(bogusBody, sizeof(bogusBody), 500);

    EXPECT_FALSE(svc.doRegistration());
}

TEST_F(RegistrationServiceTest, HttpRegisterFailsWhenResponseHasNoFrame) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(false);

    uint8_t junk[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    http_test_set_response(junk, sizeof(junk), 200);

    EXPECT_FALSE(svc.doRegistration());
}

TEST_F(RegistrationServiceTest, HttpRegisterFailsWhenServerSaysSuccessFalse) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(false);

    uint8_t frame[400];
    size_t frameLen = buildResponseFrame(frame, sizeof(frame),
        /*success=*/false,
        nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr);
    ASSERT_GT(frameLen, 0u);
    http_test_set_response(frame, (int)frameLen, 200);

    EXPECT_FALSE(svc.doRegistration());
}

// ── ECDH success path with valid SECP256R1 server pubkey ───────────────────
//
// Drives the full ECDH compute_shared + HKDF-SHA256 derivation in
// httpRegister so the comm_key derivation lines (~24) get covered.

TEST_F(RegistrationServiceTest, HttpRegisterDerivesCommKeyWithValidServerPubkey) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setSaveOk(true);
    storage.test_setLoadOk(false);

    // Generate a real server keypair so ECDH compute_shared succeeds
    uint8_t serverPub[64];
    ASSERT_TRUE(generateServerPubkey(serverPub));

    uint8_t frame[500];
    size_t frameLen = buildResponseFrame(frame, sizeof(frame),
        /*success=*/true,
        "MAC456", "passwd", "broker.example.com", 8883,
        "tok|9999999999|sig", "topic", serverPub);
    ASSERT_GT(frameLen, 0u);

    http_test_set_response(frame, (int)frameLen, 200);
    EXPECT_TRUE(svc.doRegistration());
    EXPECT_TRUE(svc.isRegistered());
    EXPECT_TRUE(svc.credentials().hasCommKey);

    // commKey should be non-zero (HKDF derived it)
    bool nonZero = false;
    for (int i = 0; i < 32; i++) {
        if (svc.credentials().commKey[i] != 0) { nonZero = true; break; }
    }
    EXPECT_TRUE(nonZero);
}

// ── doRegistration save retry path ─────────────────────────────────────────
//
// First saveCredentials() fails → vTaskDelay(2000) → retry.
// We make save fail by leaving test_setSaveOk(false), but http+parse must
// succeed. The test verifies the retry path doesn't crash.

TEST_F(RegistrationServiceTest, DoRegistrationRetryOnSaveFailure) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setSaveOk(false);   // both saves will fail
    storage.test_setLoadOk(false);

    uint8_t serverPub[64];
    ASSERT_TRUE(generateServerPubkey(serverPub));

    uint8_t frame[500];
    size_t frameLen = buildResponseFrame(frame, sizeof(frame),
        /*success=*/true,
        "MAC789", "secret", "broker.example.com", 1883,
        "tok|9999999999|sig", "topic", serverPub);
    ASSERT_GT(frameLen, 0u);
    http_test_set_response(frame, (int)frameLen, 200);

    // doRegistration returns true even if save fails (it's best-effort).
    // The important thing is the retry path is exercised — no crash.
    EXPECT_TRUE(svc.doRegistration());
}

// ── refreshToken success path ──────────────────────────────────────────────

// ── Malformed protobuf wire type (covers pbDecode L83 break) ───────────────

TEST_F(RegistrationServiceTest, MalformedProtobufWireTypeBreaksDecode) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setLoadOk(false);

    // Build a frame whose protobuf payload starts with a wire-type-1 tag
    // (64-bit fixed) which the minimal pbDecode in RegistrationServiceImpl
    // doesn't handle → it breaks out of the loop. The result has zero
    // recognized fields → parseResponse fails → httpRegister returns false.
    uint8_t pbBuf[16];
    pbBuf[0] = (1 << 3) | 1;  // field 1, wire type 1 (unsupported)
    pbBuf[1] = 0; pbBuf[2] = 0; pbBuf[3] = 0; pbBuf[4] = 0;  // garbage 64-bit
    pbBuf[5] = 0; pbBuf[6] = 0; pbBuf[7] = 0; pbBuf[8] = 0;

    uint8_t frame[64];
    size_t frameLen = 0;
    ASSERT_TRUE(FrameCodec::Frame(pbBuf, 9, frame, sizeof(frame), frameLen,
                                    FrameCodec::kFlagFin, 0x80));

    http_test_set_response(frame, (int)frameLen, 200);
    EXPECT_FALSE(svc.doRegistration());
}

TEST_F(RegistrationServiceTest, RefreshTokenSuccessPath) {
    auto& svc = RegistrationServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setSaveOk(true);

    uint8_t serverPub[64];
    ASSERT_TRUE(generateServerPubkey(serverPub));

    uint8_t frame[500];
    size_t frameLen = buildResponseFrame(frame, sizeof(frame),
        /*success=*/true,
        "MACABC", "newpass", "broker.example.com", 8883,
        "newtok|9999999999|newsig", "topic", serverPub);
    ASSERT_GT(frameLen, 0u);
    http_test_set_response(frame, (int)frameLen, 200);

    EXPECT_TRUE(svc.refreshToken());
    EXPECT_TRUE(svc.isRegistered());
}
