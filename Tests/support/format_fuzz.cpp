// Power-loss fuzz harness for the dual-FAT transaction engine.
//
// Model: write a baseline file and sync() it (a COMMIT point), then keep writing
// more data while the block device fails EVERY write after the Nth (simulating an
// abrupt power loss at write N). The on-disk image is then handed to the caller to
// cross-check with macOS `fsck_exfat`:
//   - The volume must always be mountable / fsck-clean (last committed state).
//   - The baseline (committed) file must survive intact.
//   - The uncommitted tail may be lost — that is acceptable.
//
// Without the commit engine, a power loss mid-write tears a shared metadata sector
// (FAT/bitmap/root) and corrupts even the committed state — this harness reproduces
// that, and later proves the commit engine eliminates it.
//
// Usage:  format_fuzz <out.img> <failAfterWrites>
#include "ExFatFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

class FailAfterDev : public FsBlockDeviceInterface {
public:
    static const uint32_t kSectors = 0x100000;  // 512 MB
    long failAfter = -1;   // -1 = never; else fail every write once writeCount >= failAfter
    long writeCount = 0;

    explicit FailAfterDev(const char* path) {
        mFd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        ftruncate(mFd, (off_t)kSectors * 512);
    }
    ~FailAfterDev() { if (mFd >= 0) { fsync(mFd); close(mFd); } }
    bool isBusy() override { return false; }
    bool readSector(Sector_t s, uint8_t* d) override { return readSectors(s, d, 1); }
    bool readSectors(Sector_t s, uint8_t* d, size_t n) override {
        return pread(mFd, d, n * 512, (off_t)s * 512) == (ssize_t)(n * 512);
    }
    Sector_t sectorCount() override { return kSectors; }
    bool syncDevice() override { return true; }
    bool writeSector(Sector_t s, const uint8_t* d) override { return writeSectors(s, d, 1); }
    bool writeSectors(Sector_t s, const uint8_t* d, size_t n) override {
        if (failAfter >= 0 && writeCount >= failAfter) return false;  // "power loss"
        writeCount++;
        return pwrite(mFd, d, n * 512, (off_t)s * 512) == (ssize_t)(n * 512);
    }
private:
    int mFd = -1;
};

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s out.img failAfter\n", argv[0]); return 2; }
    const char* path = argv[1];
    long failAfter = atol(argv[2]);

    FailAfterDev dev(path);
    ExFatVolume vol;
    uint8_t secBuf[512];

    // Format + mount with writes ALLOWED (set up a valid committed volume).
    ExFatFormatter fmt;
    if (!fmt.format(&dev, secBuf, nullptr) || !vol.begin(&dev)) {
        fprintf(stderr, "setup format/mount failed\n"); return 1;
    }

    // Baseline file: written + synced = the COMMITTED state that must survive.
    {
        arcana::ats::ExFatFilePort base(&vol);
        base.open("baseline.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE);
        uint8_t blk[4096];
        memset(blk, 0x5A, sizeof(blk));
        for (int i = 0; i < 64; i++) base.write(blk, sizeof(blk));  // 256 KB
        base.sync();
        base.close();
    }

    // Arm the "power loss": every write after this point fails once the counter
    // crosses failAfter. Then keep appending (mimics the firmware mid-write).
    dev.writeCount = 0;
    dev.failAfter = failAfter;
    {
        arcana::ats::ExFatFilePort sensor(&vol);
        sensor.open("sensor.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE);
        uint8_t blk[4096];
        memset(blk, 0xA5, sizeof(blk));
        for (int i = 0; i < 4096; i++) {
            if (sensor.write(blk, sizeof(blk)) != (int32_t)sizeof(blk)) break;  // "power died"
            if ((i % 8) == 0) { if (!sensor.sync()) break; }
        }
        // No clean close — power was cut.
    }
    printf("done failAfter=%ld writes=%ld -> %s\n", failAfter, dev.writeCount, path);
    return 0;
}
