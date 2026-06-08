#include "VfsFilePort.hpp"
#include <unistd.h>    // ftruncate, fsync, fileno

namespace arcana {
namespace ats {

bool VfsFilePort::open(const char* path, uint8_t mode) {
    if (mFp) close();

    char fullPath[128];
    buildPath(path, fullPath, sizeof(fullPath));

    const char* fopenMode;
    if (mode & ATS_MODE_CREATE) {
        fopenMode = "w+b";     // create + read/write
    } else if (mode == ATS_MODE_RW) {
        fopenMode = "r+b";     // existing read/write
    } else if (mode & ATS_MODE_WRITE) {
        fopenMode = "r+b";     // write to existing (don't truncate)
    } else {
        fopenMode = "rb";      // read-only
    }

    mFp = fopen(fullPath, fopenMode);

    // If r+b failed (file doesn't exist) and write was requested, create it
    if (!mFp && (mode & ATS_MODE_WRITE)) {
        mFp = fopen(fullPath, "w+b");
    }

    return mFp != nullptr;
}

bool VfsFilePort::close() {
    if (!mFp) return true;
    int ret = fclose(mFp);
    mFp = nullptr;
    return ret == 0;
}

int32_t VfsFilePort::read(uint8_t* buf, uint32_t size) {
    if (!mFp) return -1;
    size_t n = fread(buf, 1, size, mFp);
    if (n == 0 && ferror(mFp)) return -1;
    return (int32_t)n;
}

int32_t VfsFilePort::write(const uint8_t* buf, uint32_t size) {
    if (!mFp) return -1;
    size_t n = fwrite(buf, 1, size, mFp);
    if (n == 0 && ferror(mFp)) return -1;
    return (int32_t)n;
}

bool VfsFilePort::seek(uint64_t offset) {
    if (!mFp) return false;
    return fseek(mFp, (long)offset, SEEK_SET) == 0;
}

bool VfsFilePort::sync() {
    if (!mFp) return false;
    fflush(mFp);
    return fsync(fileno(mFp)) == 0;
}

uint64_t VfsFilePort::tell() {
    if (!mFp) return 0;
    long pos = ftell(mFp);
    return (pos < 0) ? 0 : (uint64_t)pos;
}

uint64_t VfsFilePort::size() {
    if (!mFp) return 0;
    long cur = ftell(mFp);
    fseek(mFp, 0, SEEK_END);
    long end = ftell(mFp);
    fseek(mFp, cur, SEEK_SET);
    return (end < 0) ? 0 : (uint64_t)end;
}

bool VfsFilePort::truncate() {
    if (!mFp) return false;
    fflush(mFp);
    long pos = ftell(mFp);
    return ftruncate(fileno(mFp), pos) == 0;
}

} // namespace ats
} // namespace arcana
