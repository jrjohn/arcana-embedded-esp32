// Remount/recovery fuzz: validates the mount-time reseed path that plain
// on-disk fuzzing misses. Sequence per trial:
//   1. format dual-FAT, write a baseline file, sync (commit), close.
//   2. "power loss": append to sensor.ats with the device failing every write
//      after the Nth — leaves the WORKING (inactive) copy dirty/partial.
//   3. "recovery boot": a FRESH ExFatVolume re-mounts the SAME image (init →
//      reseed working from committed), then appends MORE and cleanly syncs.
//   4. Caller fsck_exfats the image — it must be consistent. If the reseed
//      under-copies (leaving the working copy stale), the phase-3 writes build on
//      garbage and fsck reports corruption.
//
// Usage:  remount_fuzz <out.img> <failAfterWrites>
#include "ExFatFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

class FailDev : public FsBlockDeviceInterface {
public:
    static const uint32_t kSectors = 0x100000;
    long failAfter = -1, writeCount = 0;
    explicit FailDev(const char* path, bool create) {
        mFd = open(path, O_RDWR | (create ? (O_CREAT | O_TRUNC) : 0), 0644);
        if (create) ftruncate(mFd, (off_t)kSectors * 512);
    }
    ~FailDev() { if (mFd >= 0) { fsync(mFd); close(mFd); } }
    bool isBusy() override { return false; }
    bool readSector(Sector_t s, uint8_t* d) override { return readSectors(s, d, 1); }
    bool readSectors(Sector_t s, uint8_t* d, size_t n) override {
        return pread(mFd, d, n * 512, (off_t)s * 512) == (ssize_t)(n * 512);
    }
    Sector_t sectorCount() override { return kSectors; }
    bool syncDevice() override { return true; }
    bool writeSector(Sector_t s, const uint8_t* d) override { return writeSectors(s, d, 1); }
    bool writeSectors(Sector_t s, const uint8_t* d, size_t n) override {
        if (failAfter >= 0 && writeCount >= failAfter) return false;
        writeCount++;
        return pwrite(mFd, d, n * 512, (off_t)s * 512) == (ssize_t)(n * 512);
    }
private:
    int mFd = -1;
};

static void appendN(arcana::ats::ExFatFilePort& f, int blocks, int syncEvery) {
    uint8_t blk[4096];
    memset(blk, 0xA5, sizeof(blk));
    for (int i = 0; i < blocks; i++) {
        if (f.write(blk, sizeof(blk)) != (int32_t)sizeof(blk)) break;  // "power died"
        if (syncEvery && (i % syncEvery) == 0) { if (!f.sync()) break; }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s out.img failAfter\n", argv[0]); return 2; }
    const char* path = argv[1];
    long failAfter = atol(argv[2]);

    // Phase 1: format + baseline + interrupted append (power loss).
    {
        FailDev dev(path, true);
        ExFatVolume vol;
        uint8_t secBuf[512];
        ExFatFormatter fmt;
        if (!fmt.format(&dev, secBuf, nullptr) || !vol.begin(&dev)) {
            fprintf(stderr, "setup failed\n"); return 1;
        }
        { arcana::ats::ExFatFilePort base(&vol);
          base.open("baseline.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE);
          appendN(base, 32, 8); base.sync(); base.close(); }
        dev.writeCount = 0; dev.failAfter = failAfter;
        arcana::ats::ExFatFilePort sensor(&vol);
        sensor.open("sensor.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE);
        appendN(sensor, 4096, 8);
        // no close — power cut. vol destructs.
    }

    // Phase 2: recovery mount (fresh objects, no write failures) + more writes.
    {
        FailDev dev(path, false);    // reopen existing image, writes allowed
        ExFatVolume vol;
        if (!vol.begin(&dev)) { fprintf(stderr, "recovery mount failed\n"); return 1; }
        // App-side recovery step: heal the at-risk files' torn tails before writing
        // (mirrors AtsStorageServiceImpl, which calls reconcileFile after mount).
        vol.reconcileFile("/sensor.ats");
        vol.reconcileFile("/baseline.ats");
        arcana::ats::ExFatFilePort sensor(&vol);
        if (sensor.open("sensor.ats", arcana::ats::ATS_MODE_RW)) {
            sensor.seek(sensor.size());           // append at end of committed data
        } else {
            sensor.open("sensor.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE);
        }
        appendN(sensor, 256, 8);                  // 1 MB of recovery writes
        sensor.sync(); sensor.close();
        vol.end();                                 // clean unmount
    }
    printf("remount-recovery ok failAfter=%ld -> %s\n", failAfter, path);
    return 0;
}
