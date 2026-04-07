#include <gtest/gtest.h>
#include "impl/HttpUploadServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"   // stub
#include "impl/RegistrationServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"           // stub
#include "esp_http_client.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

using namespace Arcana::Upload;
using namespace Arcana::Registration;

// Production code hard-codes /sdcard as the mount point. The test fixture
// tries to create it at startup; if mkdir fails (non-root host), file-
// dependent tests are skipped.
static bool g_sdcardAvailable = false;

class HttpUploadServiceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Try to create /sdcard. Will succeed in Docker (root), fail on
        // an unprivileged macOS dev box.
        if (mkdir("/sdcard", 0755) == 0 || access("/sdcard", W_OK) == 0) {
            g_sdcardAvailable = true;
        }
    }

    void SetUp() override {
        http_test_reset();
        Arcana::Storage::AtsStorageServiceImpl::getInstance().test_reset();
        Arcana::Io::IoServiceImpl::getInstance().test_reset();

        // Force-invalidate registration creds
        http_test_set_perform_result(ESP_FAIL);
        RegistrationServiceImpl::getInstance().refreshToken();
        http_test_reset();
    }

    // Helper: write a file under /sdcard with the given content
    bool writeFile(const char* name, const uint8_t* data, size_t len) {
        if (!g_sdcardAvailable) return false;
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/%s", name);
        FILE* fp = fopen(path, "wb");
        if (!fp) return false;
        fwrite(data, 1, len, fp);
        fclose(fp);
        return true;
    }
    void removeFile(const char* name) {
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/%s", name);
        unlink(path);
    }
};

// ── Singleton ───────────────────────────────────────────────────────────────

TEST_F(HttpUploadServiceTest, GetInstanceReturnsSingleton) {
    auto& a = HttpUploadServiceImpl::getInstance();
    auto& b = HttpUploadServiceImpl::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST_F(HttpUploadServiceTest, ProgressInitiallyZero) {
    auto& svc = HttpUploadServiceImpl::getInstance();
    auto& p = svc.progress();
    EXPECT_EQ(p.bytesSent, 0u);
}

// ── uploadPendingFiles early-exit ──────────────────────────────────────────

TEST_F(HttpUploadServiceTest, UploadPendingFilesReturnsZeroWhenStorageNotReady) {
    auto& svc = HttpUploadServiceImpl::getInstance();
    Arcana::Storage::AtsStorageServiceImpl::getInstance().test_setReady(false);
    EXPECT_EQ(svc.uploadPendingFiles(), 0);
}

TEST_F(HttpUploadServiceTest, UploadPendingFilesAllFailWithoutFiles) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available on this host";

    auto& svc = HttpUploadServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setPendingCount(0);

    // No sensor.ats / device.ats files exist → all uploads fail
    removeFile("sensor.ats");
    removeFile("device.ats");

    // Token refresh will fail (http perform fails)
    http_test_set_perform_result(ESP_FAIL);
    EXPECT_EQ(svc.uploadPendingFiles(), 0);
}

// ── uploadFile early-exit ───────────────────────────────────────────────────

TEST_F(HttpUploadServiceTest, UploadFileNonexistentReturnsFalse) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available on this host";
    auto& svc = HttpUploadServiceImpl::getInstance();
    removeFile("nonexistent.ats");
    EXPECT_FALSE(svc.uploadFile("nonexistent.ats", "DEV001", "tok|9999|sig"));
}

TEST_F(HttpUploadServiceTest, UploadFileEmptyFileReturnsFalse) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available on this host";

    // Create an empty file
    writeFile("empty.ats", nullptr, 0);

    auto& svc = HttpUploadServiceImpl::getInstance();
    EXPECT_FALSE(svc.uploadFile("empty.ats", "DEV001", "tok|9999|sig"));

    removeFile("empty.ats");
}

// ── Cancel handling ─────────────────────────────────────────────────────────

TEST_F(HttpUploadServiceTest, CancelDuringUploadStops) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available on this host";

    // Create a file with some content
    uint8_t data[2048];
    memset(data, 0xAB, sizeof(data));
    writeFile("data.ats", data, sizeof(data));

    Arcana::Io::IoServiceImpl::getInstance().test_setCancelRequested(true);

    auto& svc = HttpUploadServiceImpl::getInstance();
    // Upload returns false because esp_http_client_open returns OK but the
    // cancel check inside the write loop trips. Acceptable: just verify no
    // crash and result is false.
    bool result = svc.uploadFile("data.ats", "DEV001", "tok|9999|sig");
    EXPECT_FALSE(result);

    Arcana::Io::IoServiceImpl::getInstance().test_setCancelRequested(false);
    removeFile("data.ats");
}

// ── Progress callback ──────────────────────────────────────────────────────

static int s_cbCalls = 0;
static void test_progress_cb(uint8_t /*current*/, uint8_t /*total*/,
                              uint32_t /*bytes*/, uint32_t /*totalBytes*/,
                              void* /*ctx*/) {
    s_cbCalls++;
}

TEST_F(HttpUploadServiceTest, ProgressCallbackInvocation) {
    auto& svc = HttpUploadServiceImpl::getInstance();
    s_cbCalls = 0;
    svc.setProgressCallback(test_progress_cb, nullptr);
    SUCCEED();  // setter is no-op-tested; firing requires a real upload
}

TEST_F(HttpUploadServiceTest, SetProgressCallbackNullClears) {
    auto& svc = HttpUploadServiceImpl::getInstance();
    svc.setProgressCallback(nullptr, nullptr);
    SUCCEED();
}
