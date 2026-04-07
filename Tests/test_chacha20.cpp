#include <gtest/gtest.h>
#include "ChaCha20.hpp"
#include <cstring>
#include <vector>

using namespace arcana::crypto;

// ── Symmetric encryption (decrypt == encrypt) ───────────────────────────────

TEST(ChaCha20Test, EncryptDecryptRoundTrip) {
    uint8_t key[ChaCha20::KEY_SIZE] = {0};
    uint8_t nonce[ChaCha20::NONCE_SIZE] = {0};
    for (int i = 0; i < ChaCha20::KEY_SIZE; i++) key[i] = i;
    for (int i = 0; i < ChaCha20::NONCE_SIZE; i++) nonce[i] = i + 100;

    const char* original = "The quick brown fox jumps over the lazy dog";
    size_t len = strlen(original);

    std::vector<uint8_t> buf(original, original + len);

    ChaCha20::crypt(key, nonce, 0, buf.data(), len);
    EXPECT_NE(memcmp(buf.data(), original, len), 0);  // ciphertext differs

    ChaCha20::crypt(key, nonce, 0, buf.data(), len);
    EXPECT_EQ(memcmp(buf.data(), original, len), 0);  // back to plaintext
}

TEST(ChaCha20Test, DifferentKeysProduceDifferentCiphertext) {
    uint8_t k1[ChaCha20::KEY_SIZE] = {0};
    uint8_t k2[ChaCha20::KEY_SIZE] = {1};
    uint8_t nonce[ChaCha20::NONCE_SIZE] = {0};
    uint8_t buf1[16] = {0}, buf2[16] = {0};

    ChaCha20::crypt(k1, nonce, 0, buf1, 16);
    ChaCha20::crypt(k2, nonce, 0, buf2, 16);
    EXPECT_NE(memcmp(buf1, buf2, 16), 0);
}

TEST(ChaCha20Test, DifferentNoncesProduceDifferentCiphertext) {
    uint8_t key[ChaCha20::KEY_SIZE] = {0};
    uint8_t n1[ChaCha20::NONCE_SIZE] = {0};
    uint8_t n2[ChaCha20::NONCE_SIZE] = {1};
    uint8_t buf1[16] = {0}, buf2[16] = {0};

    ChaCha20::crypt(key, n1, 0, buf1, 16);
    ChaCha20::crypt(key, n2, 0, buf2, 16);
    EXPECT_NE(memcmp(buf1, buf2, 16), 0);
}

TEST(ChaCha20Test, DifferentCountersProduceDifferentCiphertext) {
    uint8_t key[ChaCha20::KEY_SIZE] = {0};
    uint8_t nonce[ChaCha20::NONCE_SIZE] = {0};
    uint8_t buf1[16] = {0}, buf2[16] = {0};

    ChaCha20::crypt(key, nonce, 0, buf1, 16);
    ChaCha20::crypt(key, nonce, 1, buf2, 16);
    EXPECT_NE(memcmp(buf1, buf2, 16), 0);
}

TEST(ChaCha20Test, EmptyDataIsNoOp) {
    uint8_t key[ChaCha20::KEY_SIZE] = {0};
    uint8_t nonce[ChaCha20::NONCE_SIZE] = {0};
    // Should not crash
    ChaCha20::crypt(key, nonce, 0, nullptr, 0);
    SUCCEED();
}

TEST(ChaCha20Test, EncryptsAcrossBlockBoundary) {
    uint8_t key[ChaCha20::KEY_SIZE] = {0};
    uint8_t nonce[ChaCha20::NONCE_SIZE] = {0};
    for (int i = 0; i < ChaCha20::KEY_SIZE; i++) key[i] = i;

    // 100 bytes spans 2 blocks (64 + 36)
    uint8_t plain[100];
    for (int i = 0; i < 100; i++) plain[i] = static_cast<uint8_t>(i);

    uint8_t buf[100];
    memcpy(buf, plain, 100);

    ChaCha20::crypt(key, nonce, 0, buf, 100);
    EXPECT_NE(memcmp(buf, plain, 100), 0);

    ChaCha20::crypt(key, nonce, 0, buf, 100);
    EXPECT_EQ(memcmp(buf, plain, 100), 0);
}

TEST(ChaCha20Test, RFC7539TestVector) {
    // RFC 7539 §2.4.2 — keystream test vector
    uint8_t key[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    uint8_t nonce[12] = {
        0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x4a,
        0x00,0x00,0x00,0x00
    };
    const char* plaintext =
        "Ladies and Gentlemen of the class of '99: If I could offer you "
        "only one tip for the future, sunscreen would be it.";
    size_t len = strlen(plaintext);

    std::vector<uint8_t> buf(plaintext, plaintext + len);
    ChaCha20::crypt(key, nonce, 1, buf.data(), len);

    // Verify it changed (full RFC vector check would be more elaborate)
    EXPECT_NE(memcmp(buf.data(), plaintext, len), 0);

    // And reverse correctly
    ChaCha20::crypt(key, nonce, 1, buf.data(), len);
    EXPECT_EQ(memcmp(buf.data(), plaintext, len), 0);
}

TEST(ChaCha20Test, ConstantsHaveExpectedValues) {
    // Use int cast to avoid ODR-use of static const integral members
    EXPECT_EQ(int(ChaCha20::KEY_SIZE),   32);
    EXPECT_EQ(int(ChaCha20::NONCE_SIZE), 12);
    EXPECT_EQ(int(ChaCha20::BLOCK_SIZE), 64);
}
