#include <gtest/gtest.h>
#include "VfsFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace arcana::ats;

// Tests use /tmp as the "mount point" so VfsFilePort works on plain Linux/Mac
// without needing ESP-IDF VFS or SD card.

class VfsFilePortTest : public ::testing::Test {
protected:
    VfsFilePort* port;
    const char* testFile = "vfs_test_file.bin";

    void SetUp() override {
        port = new VfsFilePort("/tmp");
        // Clean up any leftover from previous run
        char path[64];
        snprintf(path, sizeof(path), "/tmp/%s", testFile);
        unlink(path);
    }

    void TearDown() override {
        if (port) {
            port->close();
            delete port;
        }
        char path[64];
        snprintf(path, sizeof(path), "/tmp/%s", testFile);
        unlink(path);
    }
};

// ── open() ──────────────────────────────────────────────────────────────────

TEST_F(VfsFilePortTest, OpenForCreateSucceeds) {
    EXPECT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    EXPECT_TRUE(port->isOpen());
}

TEST_F(VfsFilePortTest, OpenReadOnlyOnNonExistentFails) {
    EXPECT_FALSE(port->open("nonexistent_xyz_123.bin", ATS_MODE_READ));
}

TEST_F(VfsFilePortTest, OpenWriteCreatesIfNotExist) {
    EXPECT_TRUE(port->open(testFile, ATS_MODE_WRITE));
}

TEST_F(VfsFilePortTest, OpenTwiceClosesPrevious) {
    EXPECT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    // Second open closes first → still succeeds
    EXPECT_TRUE(port->open(testFile, ATS_MODE_RW));
}

// ── close() ─────────────────────────────────────────────────────────────────

TEST_F(VfsFilePortTest, CloseWithoutOpenIsNoOp) {
    EXPECT_TRUE(port->close());
}

TEST_F(VfsFilePortTest, CloseAfterOpen) {
    port->open(testFile, ATS_MODE_CREATE);
    EXPECT_TRUE(port->close());
    EXPECT_FALSE(port->isOpen());
}

// ── write/read round trip ──────────────────────────────────────────────────

TEST_F(VfsFilePortTest, WriteThenReadRoundTrip) {
    ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(port->write(data, sizeof(data)), (int32_t)sizeof(data));

    EXPECT_TRUE(port->seek(0));
    uint8_t buf[8] = {0};
    EXPECT_EQ(port->read(buf, sizeof(buf)), (int32_t)sizeof(buf));
    EXPECT_EQ(memcmp(buf, data, sizeof(data)), 0);
}

TEST_F(VfsFilePortTest, ReadWithoutOpenReturnsError) {
    uint8_t buf[8];
    EXPECT_EQ(port->read(buf, sizeof(buf)), -1);
}

TEST_F(VfsFilePortTest, WriteWithoutOpenReturnsError) {
    uint8_t data[] = {0x01};
    EXPECT_EQ(port->write(data, 1), -1);
}

TEST_F(VfsFilePortTest, ReadPartialAtEof) {
    ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    uint8_t data[4] = {1, 2, 3, 4};
    port->write(data, 4);
    port->seek(0);
    uint8_t buf[10] = {0};
    EXPECT_EQ(port->read(buf, 10), 4);  // only 4 bytes available
}

// ── seek + tell ─────────────────────────────────────────────────────────────

TEST_F(VfsFilePortTest, SeekAndTell) {
    ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    uint8_t data[100];
    memset(data, 0x42, sizeof(data));
    port->write(data, 100);

    EXPECT_TRUE(port->seek(50));
    EXPECT_EQ(port->tell(), 50u);

    EXPECT_TRUE(port->seek(0));
    EXPECT_EQ(port->tell(), 0u);
}

TEST_F(VfsFilePortTest, SeekWithoutOpenFails) {
    EXPECT_FALSE(port->seek(0));
}

TEST_F(VfsFilePortTest, TellWithoutOpenIsZero) {
    EXPECT_EQ(port->tell(), 0u);
}

// ── size ────────────────────────────────────────────────────────────────────

TEST_F(VfsFilePortTest, SizeReportsActualBytes) {
    ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    uint8_t data[256];
    memset(data, 0xAA, sizeof(data));
    port->write(data, 256);
    EXPECT_EQ(port->size(), 256u);
}

TEST_F(VfsFilePortTest, SizeAfterPartialWrite) {
    ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    uint8_t a[10] = {0};
    port->write(a, 10);
    EXPECT_EQ(port->size(), 10u);

    uint8_t b[20] = {0};
    port->write(b, 20);
    EXPECT_EQ(port->size(), 30u);
}

TEST_F(VfsFilePortTest, SizeWithoutOpenIsZero) {
    EXPECT_EQ(port->size(), 0u);
}

// ── sync ────────────────────────────────────────────────────────────────────

TEST_F(VfsFilePortTest, SyncReturnsTrueWhenOpen) {
    ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    uint8_t data[10] = {0};
    port->write(data, 10);
    EXPECT_TRUE(port->sync());
}

TEST_F(VfsFilePortTest, SyncWithoutOpenFails) {
    EXPECT_FALSE(port->sync());
}

// ── truncate ────────────────────────────────────────────────────────────────

TEST_F(VfsFilePortTest, TruncateAtCurrentPos) {
    ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
    uint8_t data[100];
    memset(data, 0xFF, 100);
    port->write(data, 100);

    port->seek(50);
    EXPECT_TRUE(port->truncate());

    EXPECT_EQ(port->size(), 50u);
}

TEST_F(VfsFilePortTest, TruncateWithoutOpenFails) {
    EXPECT_FALSE(port->truncate());
}

// ── reopen + persistence ────────────────────────────────────────────────────

TEST_F(VfsFilePortTest, DataPersistsAfterClose) {
    {
        ASSERT_TRUE(port->open(testFile, ATS_MODE_CREATE));
        const uint8_t data[] = {1, 2, 3, 4, 5};
        port->write(data, 5);
        port->close();
    }
    {
        ASSERT_TRUE(port->open(testFile, ATS_MODE_READ));
        uint8_t buf[5] = {0};
        EXPECT_EQ(port->read(buf, 5), 5);
        EXPECT_EQ(buf[0], 1);
        EXPECT_EQ(buf[4], 5);
    }
}
