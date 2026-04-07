#include <gtest/gtest.h>
#include "CryptoEngine.hpp"
#include "Esp32AesCtrCipher.hpp"
#include <cstring>
#include <vector>

using namespace Arcana::Command;

// ── Esp32AesCtrCipher (mbedtls AES-256-CTR — works on host with libmbedcrypto)

TEST(Esp32AesCtrCipherTest, CipherTypeIsTwo) {
    arcana::ats::Esp32AesCtrCipher c;
    EXPECT_EQ(c.cipherType(), 2);
}

TEST(Esp32AesCtrCipherTest, EncryptDecryptRoundTrip) {
    arcana::ats::Esp32AesCtrCipher c;
    uint8_t key[32]; for (int i = 0; i < 32; i++) key[i] = static_cast<uint8_t>(i);
    uint8_t nonce[12]; for (int i = 0; i < 12; i++) nonce[i] = static_cast<uint8_t>(0xA0 + i);
    uint8_t data[64];
    for (int i = 0; i < 64; i++) data[i] = static_cast<uint8_t>(i * 3);
    uint8_t orig[64];
    memcpy(orig, data, 64);

    c.crypt(key, nonce, 0, data, 64);
    EXPECT_NE(memcmp(data, orig, 64), 0);  // ciphertext differs

    c.crypt(key, nonce, 0, data, 64);  // CTR: same op decrypts
    EXPECT_EQ(memcmp(data, orig, 64), 0);
}

TEST(Esp32AesCtrCipherTest, CounterAffectsKeystream) {
    arcana::ats::Esp32AesCtrCipher c;
    uint8_t key[32]{};
    uint8_t nonce[12]{};
    uint8_t a[16]{}, b[16]{};
    c.crypt(key, nonce, 0, a, 16);
    c.crypt(key, nonce, 1, b, 16);  // different counter
    EXPECT_NE(memcmp(a, b, 16), 0);
}

// Helper: 32-byte test key
static void makeTestKey(uint8_t key[CryptoEngine::kKeyLen]) {
    for (size_t i = 0; i < CryptoEngine::kKeyLen; i++) {
        key[i] = static_cast<uint8_t>(i);
    }
}

// ── Init ────────────────────────────────────────────────────────────────────

TEST(CryptoEngineTest, InitWithValidKeySucceeds) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    EXPECT_EQ(eng.Init(key), 0);  // ESP_OK == 0
}

TEST(CryptoEngineTest, InitTwiceReinitializesCleanly) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    EXPECT_EQ(eng.Init(key), 0);
    EXPECT_EQ(eng.Init(key), 0);  // re-init should not crash
}

// ── Encrypt ─────────────────────────────────────────────────────────────────

TEST(CryptoEngineTest, EncryptUninitializedFails) {
    CryptoEngine eng;
    uint8_t plain[16] = {0};
    uint8_t out[64];
    size_t outLen = 0;
    EXPECT_FALSE(eng.Encrypt(plain, 16, out, sizeof(out), outLen));
}

TEST(CryptoEngineTest, EncryptOutputBufferTooSmallFails) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    eng.Init(key);

    uint8_t plain[100];
    uint8_t small[5];
    size_t outLen = 0;
    EXPECT_FALSE(eng.Encrypt(plain, 100, small, sizeof(small), outLen));
}

TEST(CryptoEngineTest, EncryptProducesCorrectLength) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    eng.Init(key);

    uint8_t plain[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t out[64];
    size_t outLen = 0;
    ASSERT_TRUE(eng.Encrypt(plain, 16, out, sizeof(out), outLen));
    EXPECT_EQ(outLen, CryptoEngine::kOverhead + 16);  // counter + ciphertext + tag
}

// ── Decrypt round-trip ──────────────────────────────────────────────────────

TEST(CryptoEngineTest, EncryptDecryptRoundTrip) {
    CryptoEngine txEng, rxEng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    txEng.Init(key);
    rxEng.Init(key);

    const uint8_t plain[] = "hello arcana defense";
    size_t plainLen = sizeof(plain);

    uint8_t encrypted[128];
    size_t encLen = 0;
    ASSERT_TRUE(txEng.Encrypt(plain, plainLen, encrypted, sizeof(encrypted), encLen));

    uint8_t decrypted[128];
    size_t decLen = 0;
    ASSERT_TRUE(rxEng.Decrypt(encrypted, encLen, decrypted, sizeof(decrypted), decLen));

    EXPECT_EQ(decLen, plainLen);
    EXPECT_EQ(memcmp(decrypted, plain, plainLen), 0);
}

TEST(CryptoEngineTest, EncryptIncrementsCounter) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    eng.Init(key);

    uint8_t plain[8] = {0};
    uint8_t out1[64], out2[64];
    size_t l1 = 0, l2 = 0;

    eng.Encrypt(plain, 8, out1, sizeof(out1), l1);
    eng.Encrypt(plain, 8, out2, sizeof(out2), l2);

    // Counters in first 4 bytes (LE) should differ
    uint32_t c1 = out1[0] | (out1[1] << 8) | (out1[2] << 16) | (out1[3] << 24);
    uint32_t c2 = out2[0] | (out2[1] << 8) | (out2[2] << 16) | (out2[3] << 24);
    EXPECT_NE(c1, c2);
    EXPECT_LT(c1, c2);
}

// ── Replay protection ──────────────────────────────────────────────────────

TEST(CryptoEngineTest, DecryptRejectsReplay) {
    CryptoEngine txEng, rxEng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    txEng.Init(key);
    rxEng.Init(key);

    const uint8_t plain[] = "test";
    uint8_t encrypted[64];
    size_t encLen = 0;
    txEng.Encrypt(plain, sizeof(plain), encrypted, sizeof(encrypted), encLen);

    uint8_t decrypted[64];
    size_t decLen = 0;
    EXPECT_TRUE(rxEng.Decrypt(encrypted, encLen, decrypted, sizeof(decrypted), decLen));
    // Replay same ciphertext → must be rejected
    EXPECT_FALSE(rxEng.Decrypt(encrypted, encLen, decrypted, sizeof(decrypted), decLen));
}

TEST(CryptoEngineTest, DecryptUninitializedFails) {
    CryptoEngine eng;
    uint8_t in[32] = {0};
    uint8_t out[32];
    size_t outLen = 0;
    EXPECT_FALSE(eng.Decrypt(in, sizeof(in), out, sizeof(out), outLen));
}

TEST(CryptoEngineTest, DecryptShortInputFails) {
    CryptoEngine eng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    eng.Init(key);
    uint8_t in[5] = {0};   // less than kOverhead
    uint8_t out[16];
    size_t outLen = 0;
    EXPECT_FALSE(eng.Decrypt(in, sizeof(in), out, sizeof(out), outLen));
}

TEST(CryptoEngineTest, DecryptCorruptedTagFails) {
    CryptoEngine txEng, rxEng;
    uint8_t key[CryptoEngine::kKeyLen];
    makeTestKey(key);
    txEng.Init(key);
    rxEng.Init(key);

    const uint8_t plain[] = "secret";
    uint8_t enc[64];
    size_t encLen = 0;
    txEng.Encrypt(plain, sizeof(plain), enc, sizeof(enc), encLen);

    enc[encLen - 1] ^= 0xFF;  // corrupt last byte (auth tag)

    uint8_t out[64];
    size_t outLen = 0;
    EXPECT_FALSE(rxEng.Decrypt(enc, encLen, out, sizeof(out), outLen));
}

// ── HexToKey ────────────────────────────────────────────────────────────────

TEST(CryptoEngineTest, HexToKeyValidString) {
    const char* hex = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    uint8_t key[CryptoEngine::kKeyLen];
    EXPECT_TRUE(CryptoEngine::HexToKey(hex, key));
    EXPECT_EQ(key[0], 0x00);
    EXPECT_EQ(key[1], 0x11);
    EXPECT_EQ(key[15], 0xFF);
    EXPECT_EQ(key[31], 0xFF);
}

TEST(CryptoEngineTest, HexToKeyTooShortFails) {
    const char* hex = "00112233";  // only 4 bytes
    uint8_t key[CryptoEngine::kKeyLen];
    EXPECT_FALSE(CryptoEngine::HexToKey(hex, key));
}

TEST(CryptoEngineTest, HexToKeyInvalidCharFails) {
    const char* hex = "ZZ112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    uint8_t key[CryptoEngine::kKeyLen];
    EXPECT_FALSE(CryptoEngine::HexToKey(hex, key));
}

TEST(CryptoEngineTest, ConstantsMatchSpec) {
    EXPECT_EQ(CryptoEngine::kKeyLen, 32u);
    EXPECT_EQ(CryptoEngine::kTagLen, 8u);
    EXPECT_EQ(CryptoEngine::kCounterLen, 4u);
    EXPECT_EQ(CryptoEngine::kOverhead, 12u);
}
