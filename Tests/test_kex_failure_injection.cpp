// Drives the mbedtls failure paths in KeyExchangeManager + CryptoEngine
// using the linker --wrap mbedtls function interceptors in mocks/mbedtls_wrap.cpp.

#include <gtest/gtest.h>
#include "KeyExchangeManager.hpp"
#include "CryptoEngine.hpp"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <cstring>

using namespace Arcana::Command;

extern "C" {
extern int g_fail_md_setup;
extern int g_fail_md_hmac_starts;
extern int g_fail_md_hmac_update;
extern int g_fail_md_hmac_finish;
extern int g_fail_ecp_group_load;
extern int g_fail_ecp_gen_keypair;
extern int g_fail_ecp_check_pubkey;
extern int g_fail_ecdh_compute_shared;
extern int g_fail_mpi_read_binary;
extern int g_fail_mpi_write_binary;
extern int g_fail_ctr_drbg_seed;
extern int g_fail_ccm_setkey;
extern int g_fail_ccm_encrypt_and_tag;
extern int g_fail_ccm_auth_decrypt;
void mbedtls_test_reset_failures();
}

// Helpers ───────────────────────────────────────────────────────────────────

static void makePsk(uint8_t psk[CryptoEngine::kKeyLen]) {
    for (size_t i = 0; i < CryptoEngine::kKeyLen; i++) psk[i] = static_cast<uint8_t>(0xA0 + i);
}

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

    const char* pers = "kex_fail_test";
    bool ok = false;
    do {
        if (mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &entropy,
                                   (const unsigned char*)pers, strlen(pers)) != 0) break;
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

class KexFailInject : public ::testing::Test {
protected:
    void TearDown() override {
        mbedtls_test_reset_failures();
    }
};

// ── HmacSha256 / HkdfSha256 failure paths ──────────────────────────────────

TEST_F(KexFailInject, MdSetupFailureBreaksHmac) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_md_setup = 1;  // every md_setup call fails
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 11,
                                          clientPub, serverPub, authTag));
}

TEST_F(KexFailInject, MdHmacStartsFailureBreaksHmac) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_md_hmac_starts = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 12,
                                          clientPub, serverPub, authTag));
}

TEST_F(KexFailInject, MdHmacFinishFailurePropagates) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_md_hmac_finish = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 13,
                                          clientPub, serverPub, authTag));
}

// ── ecp_group_load failure ─────────────────────────────────────────────────

TEST_F(KexFailInject, EcpGroupLoadFailureAborts) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_ecp_group_load = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 14,
                                          clientPub, serverPub, authTag));
}

// ── ecp_gen_keypair failure ────────────────────────────────────────────────

TEST_F(KexFailInject, EcpGenKeypairFailureAborts) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_ecp_gen_keypair = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 15,
                                          clientPub, serverPub, authTag));
}

// ── ecdh_compute_shared failure ────────────────────────────────────────────

TEST_F(KexFailInject, EcdhComputeSharedFailureAborts) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_ecdh_compute_shared = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 16,
                                          clientPub, serverPub, authTag));
}

// ── mpi_read_binary / mpi_write_binary failures ────────────────────────────

TEST_F(KexFailInject, MpiReadBinaryFailureAborts) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_mpi_read_binary = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 17,
                                          clientPub, serverPub, authTag));
}

TEST_F(KexFailInject, MpiWriteBinaryFailureAborts) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_mpi_write_binary = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 18,
                                          clientPub, serverPub, authTag));
}

// ── ctr_drbg_seed failure ──────────────────────────────────────────────────

TEST_F(KexFailInject, CtrDrbgSeedFailureAborts) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    g_fail_ctr_drbg_seed = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 19,
                                          clientPub, serverPub, authTag));
}

// ── CryptoEngine: ccm_setkey + encrypt failure paths ─────────────────────

TEST_F(KexFailInject, CryptoEngineInitFailsWhenCcmSetkeyFails) {
    g_fail_ccm_setkey = 1;
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makePsk(key);
    EXPECT_NE(eng.Init(key), ESP_OK);
}

TEST_F(KexFailInject, CryptoEngineEncryptFailsWhenCcmEncryptFails) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makePsk(key);
    ASSERT_EQ(eng.Init(key), ESP_OK);

    g_fail_ccm_encrypt_and_tag = 1;
    uint8_t plain[16] = {0};
    uint8_t out[64];
    size_t outLen = 0;
    EXPECT_FALSE(eng.Encrypt(plain, sizeof(plain), out, sizeof(out), outLen));
}

TEST_F(KexFailInject, CryptoEngineDecryptFailsWhenCcmDecryptFails) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makePsk(key);
    ASSERT_EQ(eng.Init(key), ESP_OK);

    // First encrypt successfully
    uint8_t plain[16];
    for (int i = 0; i < 16; i++) plain[i] = static_cast<uint8_t>(i);
    uint8_t cipher[64];
    size_t cipherLen = 0;
    ASSERT_TRUE(eng.Encrypt(plain, sizeof(plain), cipher, sizeof(cipher), cipherLen));

    // Now poison the decrypt
    g_fail_ccm_auth_decrypt = 1;
    uint8_t decoded[64];
    size_t decodedLen = 0;
    EXPECT_FALSE(eng.Decrypt(cipher, cipherLen, decoded, sizeof(decoded), decodedLen));
}
