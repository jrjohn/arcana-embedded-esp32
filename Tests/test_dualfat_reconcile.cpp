// Host unit test for the dual-FAT mount-time bitmap reconcile (the power-fail
// safety fix). Links the USE_EXFAT_DUAL_FAT variant of SdFat, so reconcileBitmap()
// / bitmapMarkUsed() / reconcileDir() are compiled in (test_exfat_file_port uses
// the single-FAT variant where they are #if'd out).
//
// Strategy: format dual-FAT, write a file, then SURGICALLY clear a referenced
// cluster's bit in the ACTIVE allocation bitmap on disk — exactly the "free but
// referenced" state a torn dual-FAT commit leaves. Remount: reconcileBitmap()
// must heal it (healedClusters()>0), committed data must survive, and a fresh
// allocation must NOT reuse the healed cluster (no cross-link).
#include <gtest/gtest.h>
#include "ExFatFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

using arcana::ats::ExFatFilePort;

namespace {

class FileBlockDevice : public FsBlockDeviceInterface {
public:
    static const uint32_t kSectors = 0x100000;  // 512 MB (exFAT minimum)
    FileBlockDevice() {
        char tmpl[] = "/tmp/dualfat_blk_XXXXXX";
        mFd = mkstemp(tmpl); unlink(tmpl);
        ftruncate(mFd, (off_t)kSectors * 512);
    }
    ~FileBlockDevice() { if (mFd >= 0) close(mFd); }
    bool isBusy() override { return false; }
    bool readSector(Sector_t s, uint8_t* d) override { return readSectors(s, d, 1); }
    bool readSectors(Sector_t s, uint8_t* d, size_t ns) override {
        return pread(mFd, d, ns * 512, (off_t)s * 512) == (ssize_t)(ns * 512);
    }
    Sector_t sectorCount() override { return kSectors; }
    bool syncDevice() override { return fsync(mFd) == 0; }
    bool writeSector(Sector_t s, const uint8_t* src) override { return writeSectors(s, src, 1); }
    bool writeSectors(Sector_t s, const uint8_t* src, size_t ns) override {
        return pwrite(mFd, src, ns * 512, (off_t)s * 512) == (ssize_t)(ns * 512);
    }
private:
    int mFd = -1;
};

// Minimal on-disk exFAT geometry parsed straight from the block device (MBR part1
// + VBR BPB), independent of SdFat internals — used to poke the active bitmap.
struct Geom {
    uint32_t startLba, heapOffset, secPerClus, activeFat;
    uint64_t clusBytes;
    uint32_t bitmapSec0;  // first sector of the ACTIVE allocation bitmap
};
static Geom parseGeom(FileBlockDevice& dev) {
    uint8_t s[512]; dev.readSector(0, s);
    uint32_t startLba; memcpy(&startLba, s + 0x1BE + 8, 4);
    uint8_t v[512]; dev.readSector(startLba, v);
    uint32_t fatOff, heapOff; uint16_t volFlags;
    memcpy(&heapOff, v + 0x58, 4);
    volFlags = (uint16_t)(v[0x6A] | (v[0x6B] << 8));
    uint8_t secPerClusSh = v[0x6D], numFats = v[0x6E];
    uint32_t spc = 1u << secPerClusSh;
    uint32_t active = (numFats == 2) ? (volFlags & 1) : 0;
    Geom g;
    g.startLba = startLba; g.heapOffset = heapOff; g.secPerClus = spc;
    g.activeFat = active; g.clusBytes = (uint64_t)spc * 512;
    // dual-FAT layout: bitmap copy `active` is cluster (2 + active)
    g.bitmapSec0 = startLba + heapOff + (uint32_t)active * spc;
    return g;
}
static int bitmapBit(FileBlockDevice& dev, const Geom& g, uint32_t cluster) {
    uint32_t bit = cluster - 2;
    uint8_t s[512]; dev.readSector(g.bitmapSec0 + (bit >> 12), s);  // 4096 bits/sector
    return (s[(bit >> 3) & 511] >> (bit & 7)) & 1;
}
// Clear `count` bits starting at `cluster` in the active bitmap (simulate torn commit).
static void bitmapClear(FileBlockDevice& dev, const Geom& g, uint32_t cluster, uint32_t count) {
    for (uint32_t c = cluster; c < cluster + count; c++) {
        uint32_t bit = c - 2; uint32_t sec = g.bitmapSec0 + (bit >> 12);
        uint8_t s[512]; dev.readSector(sec, s);
        s[(bit >> 3) & 511] &= ~(1 << (bit & 7));
        dev.writeSector(sec, s);
    }
}

// Write a contiguous file of `kb` KB; return (firstCluster, clusterCount).
static void writeFile(ExFatVolume& vol, const char* name, int kb,
                      uint32_t& firstCluster, uint32_t& nClus, uint8_t fill = 0x5A) {
    ExFatFilePort fp(&vol);
    ASSERT_TRUE(fp.open(name, arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
    uint8_t blk[1024]; memset(blk, fill, sizeof(blk));
    for (int i = 0; i < kb; i++) ASSERT_EQ(fp.write(blk, sizeof(blk)), (int32_t)sizeof(blk));
    fp.sync(); fp.close();
    char path[40]; snprintf(path, sizeof(path), "/%s", name);
    ExFatFile f; ASSERT_TRUE(f.open(&vol, path, O_RDONLY));
    firstCluster = f.firstCluster();
    uint32_t cb = vol.bytesPerCluster();
    nClus = (uint32_t)((f.fileSize() + cb - 1) / cb);
    f.close();
}

} // namespace

// A torn commit left the file's last cluster referenced (by dir size) but FREE in
// the active bitmap. Reconcile at mount must heal it; data must survive; the
// healed cluster must not be re-allocated.
TEST(DualFatReconcile, HealsFreeButReferenced) {
    FileBlockDevice dev;
    uint8_t secBuf[512];
    { ExFatFormatter fmt; ASSERT_TRUE(fmt.format(&dev, secBuf, nullptr)); }
    uint32_t fc = 0, nclus = 0;
    {
        ExFatVolume vol; ASSERT_TRUE(vol.begin(&dev));
        ASSERT_EQ(vol.numberOfFats(), 2) << "expected dual-FAT volume";
        EXPECT_EQ(vol.healedClusters(), 0u) << "clean format must heal nothing";
        writeFile(vol, "test.ats", 256, fc, nclus);   // 256 KB, contiguous
        ASSERT_GE(nclus, 2u);
    }
    Geom g = parseGeom(dev);
    uint32_t last = fc + nclus - 1;
    ASSERT_EQ(bitmapBit(dev, g, last), 1) << "last cluster should be marked used";
    bitmapClear(dev, g, last, 1);                       // simulate the torn commit
    ASSERT_EQ(bitmapBit(dev, g, last), 0) << "now free-but-referenced";

    // Remount → reconcileBitmap() must heal.
    {
        ExFatVolume vol2; ASSERT_TRUE(vol2.begin(&dev));
        ASSERT_TRUE(vol2.reconcileFile("/test.ats"));
        EXPECT_GE(vol2.healedClusters(), 1u) << "reconcile must heal the torn cluster";
        // committed data intact
        ExFatFilePort fp(&vol2);
        ASSERT_TRUE(fp.open("test.ats", arcana::ats::ATS_MODE_READ));
        EXPECT_EQ(fp.size(), 256u * 1024u);
        uint8_t buf[1024]; EXPECT_EQ(fp.read(buf, sizeof(buf)), (int32_t)sizeof(buf));
        EXPECT_EQ(buf[0], 0x5A); fp.close();
        // a fresh allocation must NOT reuse the healed cluster (no cross-link)
        uint32_t fc2 = 0, nclus2 = 0;
        writeFile(vol2, "other.ats", 64, fc2, nclus2, 0xC3);
        for (uint32_t c = fc2; c < fc2 + nclus2; c++)
            EXPECT_NE(c, last) << "fresh allocation cross-linked the healed cluster";
    }
}

// Multiple torn clusters → heal count matches (covers bitmapMarkUsed multi-bit loop).
TEST(DualFatReconcile, HealsMultipleClusters) {
    FileBlockDevice dev;
    uint8_t secBuf[512];
    { ExFatFormatter fmt; ASSERT_TRUE(fmt.format(&dev, secBuf, nullptr)); }
    uint32_t fc = 0, nclus = 0;
    { ExFatVolume vol; ASSERT_TRUE(vol.begin(&dev)); writeFile(vol, "big.ats", 512, fc, nclus); ASSERT_GE(nclus, 3u); }
    Geom g = parseGeom(dev);
    bitmapClear(dev, g, fc + nclus - 2, 2);             // clear last two referenced clusters
    { ExFatVolume vol2; ASSERT_TRUE(vol2.begin(&dev)); ASSERT_TRUE(vol2.reconcileFile("/big.ats"));
      EXPECT_EQ(vol2.healedClusters(), 2u); }
}

// Clean volume (no tear) → reconcile is a no-op.
TEST(DualFatReconcile, CleanVolumeHealsNothing) {
    FileBlockDevice dev;
    uint8_t secBuf[512];
    { ExFatFormatter fmt; ASSERT_TRUE(fmt.format(&dev, secBuf, nullptr)); }
    uint32_t fc = 0, nclus = 0;
    { ExFatVolume vol; ASSERT_TRUE(vol.begin(&dev)); writeFile(vol, "clean.ats", 128, fc, nclus); }
    { ExFatVolume vol2; ASSERT_TRUE(vol2.begin(&dev)); ASSERT_TRUE(vol2.reconcileFile("/clean.ats"));
      EXPECT_EQ(vol2.healedClusters(), 0u); }
}

// A referenced cluster inside a SUBDIRECTORY's file is healed too (covers the
// reconcileDir recursion branch).
TEST(DualFatReconcile, HealsInsideSubdir) {
    FileBlockDevice dev;
    uint8_t secBuf[512];
    { ExFatFormatter fmt; ASSERT_TRUE(fmt.format(&dev, secBuf, nullptr)); }
    uint32_t fc = 0, nclus = 0;
    {
        ExFatVolume vol; ASSERT_TRUE(vol.begin(&dev));
        ASSERT_TRUE(vol.mkdir("sub"));
        ExFatFilePort fp(&vol);
        ASSERT_TRUE(fp.open("sub/inner.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
        uint8_t blk[1024]; memset(blk, 0x77, sizeof(blk));
        for (int i = 0; i < 192; i++) ASSERT_EQ(fp.write(blk, sizeof(blk)), 1024);
        fp.sync(); fp.close();
        ExFatFile f; ASSERT_TRUE(f.open(&vol, "/sub/inner.ats", O_RDONLY));
        fc = f.firstCluster();
        uint32_t cb = vol.bytesPerCluster();
        nclus = (uint32_t)((f.fileSize() + cb - 1) / cb);
        f.close();
        ASSERT_GE(nclus, 2u);
    }
    Geom g = parseGeom(dev);
    bitmapClear(dev, g, fc + nclus - 1, 1);
    { ExFatVolume vol2; ASSERT_TRUE(vol2.begin(&dev)); ASSERT_TRUE(vol2.reconcileFile("/sub/inner.ats"));
      EXPECT_GE(vol2.healedClusters(), 1u); }
}

// A FRAGMENTED file (non-contiguous → has a real FAT chain) is healed via the
// FAT-walk branch of reconcileDir. Force fragmentation: A, then B right after A,
// then extend A so its growth can't stay contiguous.
TEST(DualFatReconcile, HealsFragmentedFile) {
    FileBlockDevice dev;
    uint8_t secBuf[512];
    { ExFatFormatter fmt; ASSERT_TRUE(fmt.format(&dev, secBuf, nullptr)); }
    uint32_t aFirst = 0;
    {
        ExFatVolume vol; ASSERT_TRUE(vol.begin(&dev));
        uint8_t blk[1024];
        { ExFatFilePort a(&vol); ASSERT_TRUE(a.open("a.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
          memset(blk, 0xA1, sizeof(blk)); for (int i=0;i<8;i++) a.write(blk,sizeof(blk)); a.sync(); a.close(); }
        { ExFatFilePort b(&vol); ASSERT_TRUE(b.open("b.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE));
          memset(blk, 0xB2, sizeof(blk)); for (int i=0;i<8;i++) b.write(blk,sizeof(blk)); b.sync(); b.close(); }
        // extend A past its first cluster; B sits right after it -> A fragments
        { ExFatFilePort a(&vol); ASSERT_TRUE(a.open("a.ats", arcana::ats::ATS_MODE_RW));
          a.seek(a.size()); memset(blk, 0xA1, sizeof(blk));
          for (int i=0;i<256;i++) a.write(blk,sizeof(blk)); a.sync(); a.close(); }
        ExFatFile f; ASSERT_TRUE(f.open(&vol, "/a.ats", O_RDONLY));
        aFirst = f.firstCluster();
        ASSERT_FALSE(f.isContiguous()) << "A should be fragmented for this test";
        f.close();
    }
    Geom g = parseGeom(dev);
    // clear A's first cluster bit -> reconcileFile must walk A's FAT chain to re-mark it
    // (a.ats is < 1 MB so the whole chain is within the tail margin)
    ASSERT_EQ(bitmapBit(dev, g, aFirst), 1);
    bitmapClear(dev, g, aFirst, 1);
    { ExFatVolume vol2; ASSERT_TRUE(vol2.begin(&dev)); ASSERT_TRUE(vol2.reconcileFile("/a.ats"));
      EXPECT_GE(vol2.healedClusters(), 1u); }
}

// reconcileFile on an absent file is a safe no-op (e.g. a freshly-formatted card
// before sensor.ats/device.ats exist).
TEST(DualFatReconcile, ReconcileMissingFileIsNoOp) {
    FileBlockDevice dev;
    uint8_t secBuf[512];
    { ExFatFormatter fmt; ASSERT_TRUE(fmt.format(&dev, secBuf, nullptr)); }
    ExFatVolume vol; ASSERT_TRUE(vol.begin(&dev));
    EXPECT_TRUE(vol.reconcileFile("/sensor.ats"));   // not present yet
    EXPECT_EQ(vol.healedClusters(), 0u);
}
