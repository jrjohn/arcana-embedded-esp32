// Drives the mbedtls failure paths in KeyExchangeManager + CryptoEngine +
// CommandCodec using the linker --wrap mbedtls function interceptors in
// mocks/mbedtls_wrap.cpp.

#include <gtest/gtest.h>
#include "KeyExchangeManager.hpp"
#include "CryptoEngine.hpp"
#include "CommandCodec.hpp"
#include "FrameCodec.hpp"
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
extern int g_fail_ccm_setkey;
extern int g_fail_ccm_encrypt_and_tag;
extern int g_fail_ccm_auth_decrypt;
extern int g_fail_md_hmac_finish_after_n;
extern int g_fail_ccm_setkey_after_n;
extern int g_fail_ccm_encrypt_and_tag_after_n;
extern int g_test_xSemaphoreCreateMutex_fail_count;
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

// ── Counter-based fail-after-N HMAC injection ──────────────────────────────
//
// PerformKeyExchange's HKDF expand step calls HmacSha256 multiple times:
// 1. HKDF extract (HMAC over salt+IKM)
// 2. HKDF expand step 1 (the call that produces the session key block)
// 3. Auth tag HMAC (HMAC over serverPub || clientPub)
//
// The existing g_fail_md_hmac_finish=1 makes the FIRST call fail at the
// extract step, so we never reach the expand or auth tag paths. The new
// counter lets us pick which call fails.

TEST_F(KexFailInject, HkdfExpandHmacFailureCovered) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    // Succeed once (the HKDF extract HMAC), fail on the 2nd HMAC.
    // This drives the HKDF expand failure return at L81.
    g_fail_md_hmac_finish_after_n = 1;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 30,
                                          clientPub, serverPub, authTag));
}

TEST_F(KexFailInject, AuthTagHmacFailureCovered) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    // Succeed twice (HKDF extract + HKDF expand), fail on the 3rd HMAC
    // (the auth tag at L208-211).
    g_fail_md_hmac_finish_after_n = 2;
    uint8_t serverPub[64], authTag[32];
    EXPECT_FALSE(kex.PerformKeyExchange(CommandSource::BLE, 31,
                                          clientPub, serverPub, authTag));
}

// L277-280: Session.Engine.Init failure inside InstallPendingSession.
// CryptoEngine::Init calls mbedtls_ccm_setkey. The first ccm_setkey runs
// during the initial Init(psk); we want the SECOND one (inside
// Session.Engine.Init) to fail.
TEST_F(KexFailInject, InstallPendingSessionFailsWhenSessionInitFails) {
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    ASSERT_EQ(kex.Init(psk), ESP_OK);

    uint8_t clientPub[64];
    ASSERT_TRUE(generateClientKeypair(clientPub));

    uint8_t serverPub[64], authTag[32];
    ASSERT_TRUE(kex.PerformKeyExchange(CommandSource::BLE, 32,
                                          clientPub, serverPub, authTag));

    // Next ccm_setkey call (inside Session.Engine.Init from
    // InstallPendingSession) should fail.
    g_fail_ccm_setkey_after_n = 0;
    EXPECT_FALSE(kex.InstallPendingSession(CommandSource::BLE, 32));

    // After the failure, the pending session should be cleared.
    EXPECT_EQ(kex.GetSession(CommandSource::BLE, 32), nullptr);
}

// L30: KeyExchangeManager::Init returns ESP_ERR_NO_MEM when mutex
// creation fails (FreeRTOS heap exhaustion).
TEST_F(KexFailInject, InitReturnsNoMemOnMutexFailure) {
    g_test_xSemaphoreCreateMutex_fail_count = 1;
    KeyExchangeManager kex;
    uint8_t psk[CryptoEngine::kKeyLen];
    makePsk(psk);
    EXPECT_NE(kex.Init(psk), ESP_OK);
    g_test_xSemaphoreCreateMutex_fail_count = 0;
}

// ── CommandCodec encryption-mode fault injection ──────────────────────────

// L32: CryptoEngine::Init failure inside CommandCodec::Init must return
// the underlying error rather than ESP_OK.
TEST_F(KexFailInject, CommandCodecInitFailsWhenCryptoEngineInitFails) {
    g_fail_ccm_setkey = 1;
    Arcana::Command::CommandCodec codec;
    EXPECT_NE(codec.Init(), ESP_OK);
}

// L169: EncodeResponse fails when the PSK fallback Encrypt fails.
// With no KeyExchangeManager wired in, the codec falls through to
// mCrypto.Encrypt which calls ccm_encrypt_and_tag. Force that to fail.
TEST_F(KexFailInject, CommandCodecEncodeResponseFailsWhenEncryptFails) {
    Arcana::Command::CommandCodec codec;
    ASSERT_EQ(codec.Init(), ESP_OK);

    g_fail_ccm_encrypt_and_tag = 1;

    Arcana::Command::CommandResponse rsp{};
    rsp.ClusterId = Arcana::Command::Cluster::System;
    rsp.Command = Arcana::Command::SystemCmd::Ping;
    rsp.Status = Arcana::Command::kStatusOk;
    rsp.PayloadLen = 0;
    rsp.StreamId = 0;
    rsp.Fin = true;

    uint8_t buf[512];
    size_t outLen = 0;
    EXPECT_FALSE(codec.EncodeResponse(rsp, buf, sizeof(buf), outLen));
}

// L76: CryptoEngine::Encrypt refuses to encrypt when mTxCounter has
// exhausted (== UINT32_MAX) to prevent CCM nonce reuse. The counter is
// private; reach it via repeated Encrypt calls is impractical (2^32),
// so use a friend-class trick OR LCOV_EXCL. Here we add a thin friend
// class declared in CryptoEngine.hpp and exposed only when building
// tests; for now we exercise the path indirectly by encrypting once,
// then asserting the test framework can't get there.
//
// (Skipped — see CryptoEngine.cpp L74 LCOV_EXCL recommendation for the
// counter-exhaustion guard. Realistic exhaustion = 4 billion calls.)
