#include <gtest/gtest.h>
#include "KeyExchangeManager.hpp"
#include "CryptoEngine.hpp"
#include <cstring>

#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

using namespace Arcana::Command;

// ── Helper: generate a valid P-256 keypair, return raw public key (x||y) ───
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

    const char* pers = "test_client";
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

static void makePsk(uint8_t psk[CryptoEngine::kKeyLen]) {
    for (size_t i = 0; i < CryptoEngine::kKeyLen; i++) {
        psk[i] = static_cast<uint8_t>(0xA0 + i);
    }
}

// ── Init ───────────────────────────────────────────────────────────────────

TEST(KeyExchangeManagerTest, InitWithPskSucceeds) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    EXPECT_EQ(kex.Init(psk), ESP_OK);
}

// ── PerformKeyExchange ──────────────────────────────────────────────────────

TEST(KeyExchangeManagerTest, PerformKeyExchangeWithValidPubKey) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    uint8_t serverPub[64] = {0};
    uint8_t authTag[32] = {0};
    EXPECT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 1, clientPub, serverPub, authTag));

    // Server pub key + auth tag should be populated
    bool serverPubNonZero = false;
    for (int i = 0; i < 64; i++) if (serverPub[i] != 0) { serverPubNonZero = true; break; }
    EXPECT_TRUE(serverPubNonZero);

    bool authTagNonZero = false;
    for (int i = 0; i < 32; i++) if (authTag[i] != 0) { authTagNonZero = true; break; }
    EXPECT_TRUE(authTagNonZero);
}

TEST(KeyExchangeManagerTest, PerformKeyExchangeWithBogusPubKeyFails) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    // All zeros is not on the curve
    uint8_t clientPub[64] = {0};
    uint8_t serverPub[64];
    uint8_t authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 1, clientPub, serverPub, authTag));
}

TEST(KeyExchangeManagerTest, DuplicateKeyExchangeForSameConnIsRejected) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    uint8_t serverPub[64];
    uint8_t authTag[32];
    EXPECT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 5, clientPub, serverPub, authTag));
    // Pending without install — duplicate KE for same conn should be rejected
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 5, clientPub, serverPub, authTag));
}

// ── InstallPendingSession ───────────────────────────────────────────────────

TEST(KeyExchangeManagerTest, InstallPendingSessionAfterKeyExchange) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    uint8_t serverPub[64];
    uint8_t authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 7, clientPub, serverPub, authTag));
    EXPECT_TRUE(kex.InstallPendingSession(CommandSource::BLE, 7));

    // After install, GetSession should find an active session
    EXPECT_NE(kex.GetSession(CommandSource::BLE, 7), nullptr);
}

TEST(KeyExchangeManagerTest, InstallPendingForUnknownConnFails) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);
    EXPECT_FALSE(kex.InstallPendingSession(CommandSource::BLE, 99));
}

TEST(KeyExchangeManagerTest, InstallPendingForMismatchedConnFails) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));
    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 1, clientPub, serverPub, authTag));
    // Try to install for a different connId
    EXPECT_FALSE(kex.InstallPendingSession(CommandSource::BLE, 2));
}

// ── GetSession ──────────────────────────────────────────────────────────────

TEST(KeyExchangeManagerTest, GetSessionReturnsNullIfNoSession) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);
    EXPECT_EQ(kex.GetSession(CommandSource::BLE, 0), nullptr);
}

TEST(KeyExchangeManagerTest, DecryptWithoutSessionReturnsFalse) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t in[64] = {0};
    uint8_t plain[64];
    size_t plainLen = 0;
    EXPECT_FALSE(kex.DecryptWithSession(CommandSource::BLE, 0, in, sizeof(in),
                                          plain, sizeof(plain), plainLen));
}

TEST(KeyExchangeManagerTest, EncryptWithoutSessionReturnsFalse) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    const char* msg = "hello";
    uint8_t out[64];
    size_t outLen = 0;
    EXPECT_FALSE(kex.EncryptWithSession(CommandSource::BLE, 0,
                                          reinterpret_cast<const uint8_t*>(msg), 5,
                                          out, sizeof(out), outLen));
}

// ── Encrypt/Decrypt round-trip after session install ───────────────────────

TEST(KeyExchangeManagerTest, EncryptDecryptRoundTripAfterInstall) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));
    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::MQTT, 0, clientPub, serverPub, authTag));
    ASSERT_TRUE(kex.InstallPendingSession(CommandSource::MQTT, 0));

    const char* plaintext = "secret payload";
    size_t plainLen = strlen(plaintext);
    uint8_t cipher[128];
    size_t cipherLen = 0;
    ASSERT_TRUE(kex.EncryptWithSession(CommandSource::MQTT, 0,
                                         reinterpret_cast<const uint8_t*>(plaintext),
                                         plainLen, cipher, sizeof(cipher), cipherLen));
    EXPECT_GT(cipherLen, plainLen);  // includes nonce + tag

    uint8_t decoded[128];
    size_t decodedLen = 0;
    ASSERT_TRUE(kex.DecryptWithSession(CommandSource::MQTT, 0,
                                         cipher, cipherLen, decoded, sizeof(decoded), decodedLen));
    EXPECT_EQ(decodedLen, plainLen);
    EXPECT_EQ(memcmp(decoded, plaintext, plainLen), 0);
}

// ── RemoveSession ───────────────────────────────────────────────────────────

TEST(KeyExchangeManagerTest, RemoveSessionFreesSlot) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));
    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 3, clientPub, serverPub, authTag));
    ASSERT_TRUE(kex.InstallPendingSession(CommandSource::BLE, 3));
    ASSERT_NE(kex.GetSession(CommandSource::BLE, 3), nullptr);

    kex.RemoveSession(CommandSource::BLE, 3);
    EXPECT_EQ(kex.GetSession(CommandSource::BLE, 3), nullptr);
}

TEST(KeyExchangeManagerTest, RemoveNonexistentSessionIsSafe) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);
    kex.RemoveSession(CommandSource::BLE, 99);  // no crash
    SUCCEED();
}

TEST(KeyExchangeManagerTest, RemovePendingClearsItToo) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));
    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 4, clientPub, serverPub, authTag));
    // RemoveSession should also invalidate the pending entry
    kex.RemoveSession(CommandSource::BLE, 4);
    // Now installing should fail since pending was cleared
    EXPECT_FALSE(kex.InstallPendingSession(CommandSource::BLE, 4));
}

// ── Slot exhaustion ─────────────────────────────────────────────────────────

TEST(KeyExchangeManagerTest, SlotExhaustionAfterFourSessions) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    // Install 4 sessions (kMaxSessions = 4)
    for (int i = 0; i < KeyExchangeManager::kMaxSessions; i++) {
        uint8_t clientPub[64];
        ASSERT_TRUE(generateClientKeypair(clientPub));
        uint8_t serverPub[64], authTag[32];
        ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE,
                                             static_cast<uint16_t>(100 + i),
                                             clientPub, serverPub, authTag));
        ASSERT_TRUE(kex.InstallPendingSession(CommandSource::BLE,
                                                static_cast<uint16_t>(100 + i)));
    }

    // 5th session should be rejected (no free slot)
    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));
    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 200,
                                         clientPub, serverPub, authTag));
    EXPECT_FALSE(kex.InstallPendingSession(CommandSource::BLE, 200));
}

// ── Independent BLE / MQTT slots ────────────────────────────────────────────

TEST(KeyExchangeManagerTest, BleAndMqttSessionsAreIndependent) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPubBle[64], clientPubMqtt[64];
    ASSERT_TRUE(generateClientKeypair(clientPubBle));
    ASSERT_TRUE(generateClientKeypair(clientPubMqtt));

    uint8_t serverPub[64], authTag[32];
    // BLE session on conn 1
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 1, clientPubBle,
                                         serverPub, authTag));
    ASSERT_TRUE(kex.InstallPendingSession(CommandSource::BLE, 1));
    // MQTT session on conn 1 (same connId, different source — separate slot)
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::MQTT, 1, clientPubMqtt,
                                         serverPub, authTag));
    ASSERT_TRUE(kex.InstallPendingSession(CommandSource::MQTT, 1));

    EXPECT_NE(kex.GetSession(CommandSource::BLE, 1), nullptr);
    EXPECT_NE(kex.GetSession(CommandSource::MQTT, 1), nullptr);

    // Removing BLE should leave MQTT untouched
    kex.RemoveSession(CommandSource::BLE, 1);
    EXPECT_EQ(kex.GetSession(CommandSource::BLE, 1), nullptr);
    EXPECT_NE(kex.GetSession(CommandSource::MQTT, 1), nullptr);
}
