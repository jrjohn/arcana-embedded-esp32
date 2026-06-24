// Host reproduction tool: run SdFat's exFAT through the SAME code path the
// firmware uses (ExFatFormatter + two concurrently-open ExFatFilePort files +
// interleaved appends + clean close), writing to a real .img file, so the image
// can be cross-checked with macOS `fsck_exfat`. Pure host, no ESP32 hardware.
//
// Usage:  format_image <out.img> [bytesPerFile]
//   - format only      : bytesPerFile = 0
//   - format + writes  : bytesPerFile > 0 (mimics ECG sensor.ats + device.ats)
#include "ExFatFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

class FileBlockDevice : public FsBlockDeviceInterface {
public:
    static const uint32_t kSectors = 0x100000;  // 512 MB (exFAT minimum)
    explicit FileBlockDevice(const char* path) {
        mFd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        ftruncate(mFd, (off_t)kSectors * 512);
    }
    ~FileBlockDevice() { if (mFd >= 0) { fsync(mFd); close(mFd); } }
    bool isBusy() override { return false; }
    bool readSector(Sector_t s, uint8_t* d) override { return readSectors(s, d, 1); }
    bool readSectors(Sector_t s, uint8_t* d, size_t n) override {
        return pread(mFd, d, n * 512, (off_t)s * 512) == (ssize_t)(n * 512);
    }
    Sector_t sectorCount() override { return kSectors; }
    bool syncDevice() override { return fsync(mFd) == 0; }
    bool writeSector(Sector_t s, const uint8_t* d) override { return writeSectors(s, d, 1); }
    bool writeSectors(Sector_t s, const uint8_t* d, size_t n) override {
        return pwrite(mFd, d, n * 512, (off_t)s * 512) == (ssize_t)(n * 512);
    }
private:
    int mFd = -1;
};

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s out.img [bytesPerFile]\n", argv[0]); return 2; }
    const char* path = argv[1];
    long bytesPerFile = (argc >= 3) ? atol(argv[2]) : 0;

    FileBlockDevice dev(path);
    ExFatVolume vol;
    uint8_t secBuf[512];

    ExFatFormatter fmt;
    if (!fmt.format(&dev, secBuf, nullptr)) { fprintf(stderr, "format failed\n"); return 1; }
    if (!vol.begin(&dev))                   { fprintf(stderr, "mount failed\n");  return 1; }
    printf("formatted: fatType=%d\n", vol.fatType());

    if (bytesPerFile > 0) {
        // Mimic the firmware: sensor.ats + device.ats open at once, interleaved
        // appends through ExFatFilePort (the exact production write path).
        arcana::ats::ExFatFilePort sensor(&vol), device(&vol);
        if (!sensor.open("sensor.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE) ||
            !device.open("device.ats", arcana::ats::ATS_MODE_RW | arcana::ats::ATS_MODE_CREATE)) {
            fprintf(stderr, "open failed\n"); return 1;
        }
        uint8_t blk[4096];
        memset(blk, 0xA5, sizeof(blk));
        long sw = 0, dw = 0;
        long iters = bytesPerFile / (long)sizeof(blk);
        for (long i = 0; i < iters; i++) {
            sw += sensor.write(blk, sizeof(blk));
            if ((i % 16) == 0) dw += device.write(blk, 256);   // device.ats less often
            if ((i % 64) == 0) { sensor.sync(); device.sync(); }
        }
        sensor.sync(); device.sync();
        sensor.close(); device.close();
        printf("wrote sensor=%ld device=%ld bytes\n", sw, dw);
    }

    vol.end();   // clean unmount (flush volume)
    printf("done -> %s\n", path);
    return 0;
}
