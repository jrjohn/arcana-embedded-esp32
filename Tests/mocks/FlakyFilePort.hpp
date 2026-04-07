#pragma once
// Test-only IFilePort wrapper that can inject failures into specific
// operations after N successful calls. Used by ArcanaTsDb tests to drive
// the file-I/O failure branches that VfsFilePort + a real /tmp file
// can never reach (which is most of them).

#include "ats/IFilePort.hpp"
#include "VfsFilePort.hpp"
#include <cstdint>

namespace arcana::ats {

class FlakyFilePort : public IFilePort {
public:
    explicit FlakyFilePort(const char* mountPoint = "/tmp")
        : mInner(mountPoint) {}

    // ── Test injection — set to >=0 to enable failure after N successful calls
    void test_failOpenAfter(int n)     { mFailOpenAfter = n; }
    void test_failCloseAfter(int n)    { mFailCloseAfter = n; }
    void test_failReadAfter(int n)     { mFailReadAfter = n; }
    void test_failWriteAfter(int n)    { mFailWriteAfter = n; }
    void test_failSeekAfter(int n)     { mFailSeekAfter = n; }
    void test_failSyncAfter(int n)     { mFailSyncAfter = n; }
    void test_failTruncateAfter(int n) { mFailTruncateAfter = n; }

    void test_reset() {
        mFailOpenAfter = mFailCloseAfter = mFailReadAfter = -1;
        mFailWriteAfter = mFailSeekAfter = mFailSyncAfter = mFailTruncateAfter = -1;
        mOpenCalls = mCloseCalls = mReadCalls = mWriteCalls = 0;
        mSeekCalls = mSyncCalls = mTruncateCalls = 0;
    }

    // ── IFilePort overrides ────────────────────────────────────────────────

    bool open(const char* path, uint8_t mode) override {
        if (mFailOpenAfter >= 0 && mOpenCalls >= mFailOpenAfter) {
            mOpenCalls++;
            return false;
        }
        mOpenCalls++;
        return mInner.open(path, mode);
    }

    bool close() override {
        if (mFailCloseAfter >= 0 && mCloseCalls >= mFailCloseAfter) {
            mCloseCalls++;
            return false;
        }
        mCloseCalls++;
        return mInner.close();
    }

    int32_t read(uint8_t* buf, uint32_t size) override {
        if (mFailReadAfter >= 0 && mReadCalls >= mFailReadAfter) {
            mReadCalls++;
            return -1;
        }
        mReadCalls++;
        return mInner.read(buf, size);
    }

    int32_t write(const uint8_t* buf, uint32_t size) override {
        if (mFailWriteAfter >= 0 && mWriteCalls >= mFailWriteAfter) {
            mWriteCalls++;
            return -1;
        }
        mWriteCalls++;
        return mInner.write(buf, size);
    }

    bool seek(uint64_t offset) override {
        if (mFailSeekAfter >= 0 && mSeekCalls >= mFailSeekAfter) {
            mSeekCalls++;
            return false;
        }
        mSeekCalls++;
        return mInner.seek(offset);
    }

    bool sync() override {
        if (mFailSyncAfter >= 0 && mSyncCalls >= mFailSyncAfter) {
            mSyncCalls++;
            return false;
        }
        mSyncCalls++;
        return mInner.sync();
    }

    uint64_t tell() override { return mInner.tell(); }
    uint64_t size() override { return mInner.size(); }

    bool truncate() override {
        if (mFailTruncateAfter >= 0 && mTruncateCalls >= mFailTruncateAfter) {
            mTruncateCalls++;
            return false;
        }
        mTruncateCalls++;
        return mInner.truncate();
    }

    bool isOpen() const override { return mInner.isOpen(); }

private:
    VfsFilePort mInner;

    int mFailOpenAfter     = -1;
    int mFailCloseAfter    = -1;
    int mFailReadAfter     = -1;
    int mFailWriteAfter    = -1;
    int mFailSeekAfter     = -1;
    int mFailSyncAfter     = -1;
    int mFailTruncateAfter = -1;

    int mOpenCalls     = 0;
    int mCloseCalls    = 0;
    int mReadCalls     = 0;
    int mWriteCalls    = 0;
    int mSeekCalls     = 0;
    int mSyncCalls     = 0;
    int mTruncateCalls = 0;
};

} // namespace arcana::ats
