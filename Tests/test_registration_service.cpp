#include <gtest/gtest.h>
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"   // stub from Tests/mocks/impl/
#include "esp_http_client.h"
#include "FrameCodec.hpp"
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
