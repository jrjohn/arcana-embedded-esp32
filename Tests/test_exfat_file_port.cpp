// Host unit test for ExFatFilePort — the IFilePort over SdFat's exFAT.
//
// Runs the REAL SdFat ExFatLib (ExFatVolume/ExFatFile/ExFatFormatter) against a
// sparse file-backed block device, so every ExFatFilePort code path is exercised
// on the host: format+mount, create/open, write/read, seek + seek-beyond-EOF
// zero-fill, truncate, size/tell, the open-existing silent fail, and close.
//
// exFAT's formatter requires a >=512 MB volume; the backing file is sparse
// (ftruncate to the logical size, pread/pwrite only touch written sectors), so
// the physical footprint of an empty exFAT volume is a few MB.
#include <gtest/gtest.h>
#include "ExFatFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

using arcana::ats::ExFatFilePort;

namespace {

// Sparse file-backed FsBlockDeviceInterface (512 MB logical = exFAT minimum).
class FileBlockDevice : public FsBlockDeviceInterface {
public:
    static const uint32_t kSectors = 0x100000;  // 1048576 * 512 = 512 MB

    FileBlockDevice() {
        char tmpl[] = "/tmp/exfat_blk_XXXXXX";
        mFd = mkstemp(tmpl);
        unlink(tmpl);  // anonymous: removed on close
        ftruncate(mFd, (off_t)kSectors * 512);
    }
    ~FileBlockDevice() { if (mFd >= 0) close(mFd); }

    // Fault injection: when set, the matching op returns failure so ExFatFilePort's
    // retry/error paths can be exercised on the host.
    bool failReads = false;
    bool failWrites = false;

    bool isBusy() override { return false; }
    bool readSector(Sector_t s, uint8_t* dst) override { return readSectors(s, dst, 1); }
    bool readSectors(Sector_t s, uint8_t* dst, size_t ns) override {
        if (failReads) return false;
        ssize_t n = pread(mFd, dst, ns * 512, (off_t)s * 512);
        return n == (ssize_t)(ns * 512);
    }
    Sector_t sectorCount() override { return kSectors; }
    bool syncDevice() override { return fsync(mFd) == 0; }
    bool writeSector(Sector_t s, const uint8_t* src) override { return writeSectors(s, src, 1); }
    bool writeSectors(Sector_t s, const uint8_t* src, size_t ns) override {
        if (failWrites) return false;
        ssize_t n = pwrite(mFd, src, ns * 512, (off_t)s * 512);
        return n == (ssize_t)(ns * 512);
    }
private:
    int mFd = -1;
};

class ExFatFilePortTest : public ::testing::Test {
protected:
    FileBlockDevice dev;
    ExFatVolume     vol;
    uint8_t         secBuf[512];

    void SetUp() override {
        ExFatFormatter fmt;
        ASSERT_TRUE(fmt.format(&dev, secBuf, nullptr)) << "exFAT format failed";
        ASSERT_TRUE(vol.begin(&dev)) << "exFAT mount failed";
        ASSERT_EQ(vol.fatType(), 64) << "not exFAT (expected fatType 64)";
    }
};

} // namespace

// ── create / write / read round-trip ────────────────────────────────────────
TEST_F(ExFatFilePortTest, WriteThenReadBackMatches) {
    uint8_t wbuf[4096];
    memset(wbuf, 0xA5, sizeof(wbuf));

    ExFatFilePort wp(&vol);
    ASSERT_TRUE(wp.open("sensor.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    EXPECT_TRUE(wp.isOpen());
    EXPECT_EQ(wp.write(wbuf, sizeof(wbuf)), (int32_t)sizeof(wbuf));
    EXPECT_TRUE(wp.sync());
    EXPECT_EQ(wp.size(), sizeof(wbuf));
    EXPECT_TRUE(wp.close());
    EXPECT_FALSE(wp.isOpen());

    uint8_t rbuf[4096] = {0};
    ExFatFilePort rp(&vol);
    ASSERT_TRUE(rp.open("sensor.ats", arcana::ats::ATS_MODE_READ));
    EXPECT_EQ(rp.size(), sizeof(rbuf));
    EXPECT_EQ(rp.read(rbuf, sizeof(rbuf)), (int32_t)sizeof(rbuf));
    EXPECT_EQ(memcmp(wbuf, rbuf, sizeof(wbuf)), 0);
    EXPECT_TRUE(rp.close());
}

// ── seek within file + tell ──────────────────────────────────────────────────
TEST_F(ExFatFilePortTest, SeekAndTell) {
    uint8_t buf[1024];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)i;

    ExFatFilePort p(&vol);
    ASSERT_TRUE(p.open("seek.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    ASSERT_EQ(p.write(buf, sizeof(buf)), (int32_t)sizeof(buf));

    EXPECT_TRUE(p.seek(256));
    EXPECT_EQ(p.tell(), 256u);
    uint8_t one = 0;
    EXPECT_EQ(p.read(&one, 1), 1);
    EXPECT_EQ(one, 256 & 0xFF);   // byte written at offset 256
    p.close();
}

// ── seek beyond EOF zero-fills the gap ───────────────────────────────────────
TEST_F(ExFatFilePortTest, SeekBeyondEofZeroFills) {
    ExFatFilePort p(&vol);
    ASSERT_TRUE(p.open("sparse.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));

    uint8_t hdr[16];
    memset(hdr, 0x11, sizeof(hdr));
    ASSERT_EQ(p.write(hdr, sizeof(hdr)), 16);

    // Seek well past EOF — ExFatFilePort must zero-fill from 16 up to 8192.
    EXPECT_TRUE(p.seek(8192));
    EXPECT_EQ(p.tell(), 8192u);
    EXPECT_EQ(p.size(), 8192u);   // file extended by the zero-fill

    uint8_t marker = 0xEE;
    ASSERT_EQ(p.write(&marker, 1), 1);
    p.close();

    // Read back: [0..15]=0x11, [16..8191]=0x00, [8192]=0xEE.
    ExFatFilePort rp(&vol);
    ASSERT_TRUE(rp.open("sparse.ats", arcana::ats::ATS_MODE_READ));
    EXPECT_EQ(rp.size(), 8193u);
    uint8_t rb[8193];
    ASSERT_EQ(rp.read(rb, sizeof(rb)), (int32_t)sizeof(rb));
    for (int i = 0; i < 16; i++)     EXPECT_EQ(rb[i], 0x11) << "hdr " << i;
    for (int i = 16; i < 8192; i++)  EXPECT_EQ(rb[i], 0x00) << "gap " << i;
    EXPECT_EQ(rb[8192], 0xEE);
    rp.close();
}

// ── truncate at current position ─────────────────────────────────────────────
TEST_F(ExFatFilePortTest, TruncateAtPosition) {
    uint8_t buf[2048];
    memset(buf, 0x7C, sizeof(buf));

    ExFatFilePort p(&vol);
    ASSERT_TRUE(p.open("trunc.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    ASSERT_EQ(p.write(buf, sizeof(buf)), (int32_t)sizeof(buf));
    EXPECT_TRUE(p.seek(500));
    EXPECT_TRUE(p.truncate());
    EXPECT_EQ(p.size(), 500u);
    p.close();
}

// ── open-existing on a missing file fails quietly (no create) ────────────────
TEST_F(ExFatFilePortTest, OpenMissingFileFails) {
    ExFatFilePort p(&vol);
    EXPECT_FALSE(p.open("nope.ats", arcana::ats::ATS_MODE_READ));
    EXPECT_FALSE(p.isOpen());
}

// ── operations on a closed port are safe no-ops ──────────────────────────────
TEST_F(ExFatFilePortTest, ClosedPortGuards) {
    ExFatFilePort p(&vol);
    uint8_t b[8];
    EXPECT_FALSE(p.isOpen());
    EXPECT_EQ(p.read(b, sizeof(b)), -1);
    EXPECT_EQ(p.write(b, sizeof(b)), -1);
    EXPECT_FALSE(p.seek(0));
    EXPECT_FALSE(p.sync());
    EXPECT_FALSE(p.truncate());
    EXPECT_EQ(p.tell(), 0u);
    EXPECT_EQ(p.size(), 0u);
    EXPECT_FALSE(p.close());
}

// ── no volume bound → open fails ─────────────────────────────────────────────
TEST_F(ExFatFilePortTest, NoVolumeOpenFails) {
    ExFatFilePort p(nullptr);
    EXPECT_FALSE(p.open("x.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
}

// ── fault injection: read retries then returns error ─────────────────────────
TEST_F(ExFatFilePortTest, ReadErrorAfterRetries) {
    uint8_t buf[2048];
    memset(buf, 0x33, sizeof(buf));
    ExFatFilePort wp(&vol);
    ASSERT_TRUE(wp.open("rderr.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    ASSERT_EQ(wp.write(buf, sizeof(buf)), (int32_t)sizeof(buf));
    wp.close();

    ExFatFilePort rp(&vol);
    ASSERT_TRUE(rp.open("rderr.ats", arcana::ats::ATS_MODE_READ));
    dev.failReads = true;                       // every sector read now fails
    uint8_t rb[2048];
    EXPECT_EQ(rp.read(rb, sizeof(rb)), -1);     // retries exhausted → error
    dev.failReads = false;
    rp.close();
}

// ── fault injection: write/flush retries then returns error ──────────────────
TEST_F(ExFatFilePortTest, WriteErrorAfterRetries) {
    ExFatFilePort p(&vol);
    ASSERT_TRUE(p.open("wrerr.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    dev.failWrites = true;                      // sector flushes during write fail
    uint8_t buf[8192];
    memset(buf, 0x44, sizeof(buf));
    EXPECT_EQ(p.write(buf, sizeof(buf)), -1);   // partial flush failure → retries → error
    dev.failWrites = false;
    p.close();
}

// ── fault injection: sync retries then returns error ─────────────────────────
TEST_F(ExFatFilePortTest, SyncErrorAfterRetries) {
    ExFatFilePort p(&vol);
    ASSERT_TRUE(p.open("syncerr.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    uint8_t buf[64];
    memset(buf, 0x55, sizeof(buf));
    ASSERT_EQ(p.write(buf, sizeof(buf)), (int32_t)sizeof(buf));   // dirties the cache
    dev.failWrites = true;                      // flush of the dirty cache will fail
    EXPECT_FALSE(p.sync());                     // retries exhausted → false
    dev.failWrites = false;
    p.close();
}

// ── fault injection: zero-fill extend write fails ────────────────────────────
TEST_F(ExFatFilePortTest, ZeroFillExtendWriteError) {
    ExFatFilePort p(&vol);
    ASSERT_TRUE(p.open("zferr.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    uint8_t hdr[16];
    memset(hdr, 0x11, sizeof(hdr));
    ASSERT_EQ(p.write(hdr, sizeof(hdr)), 16);
    dev.failWrites = true;                      // the zero-fill writes will fail
    EXPECT_FALSE(p.seek(65536));                // seek past EOF → zero-fill → write fails
    dev.failWrites = false;
    p.close();
}
