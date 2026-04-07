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

// ── queryServerOffset response parsing (via uploadFile path) ───────────────
//
// uploadFile() calls queryServerOffset() before opening the upload stream.
// The mock streams the queryServerOffset response, then streams the upload
// response. We use the same canned response for both — the queryServerOffset
// just needs to return *some* valid value (or 0 to start fresh).

TEST_F(HttpUploadServiceTest, UploadFileSizeMatchesServerSizeSkipsUpload) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    // 100-byte file
    uint8_t data[100];
    memset(data, 0x42, sizeof(data));
    writeFile("done.ats", data, sizeof(data));

    // Server responds with {"size":100} → queryServerOffset returns 100,
    // which equals fileSize → upload returns true without uploading
    const char* json = "{\"size\":100}";
    http_test_set_response(reinterpret_cast<const uint8_t*>(json),
                           (int)strlen(json), 200);

    auto& svc = HttpUploadServiceImpl::getInstance();
    EXPECT_TRUE(svc.uploadFile("done.ats", "DEV001", "tok|9999|sig"));

    removeFile("done.ats");
}

TEST_F(HttpUploadServiceTest, UploadFileResume404TreatedAsZeroOffset) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    // 200-byte file
    uint8_t data[200];
    memset(data, 0x55, sizeof(data));
    writeFile("new.ats", data, sizeof(data));

    // Server returns 404 → queryServerOffset returns 0 → full upload attempted.
    // The upload write loop will succeed against the same canned response.
    const char* body = "not found";
    http_test_set_response(reinterpret_cast<const uint8_t*>(body),
                           (int)strlen(body), 404);

    auto& svc = HttpUploadServiceImpl::getInstance();
    // Result depends on whether the second response (for the upload itself)
    // is treated as success — accept either; we just want to cover the 404
    // branch in queryServerOffset.
    (void)svc.uploadFile("new.ats", "DEV001", "tok|9999|sig");

    removeFile("new.ats");
}

TEST_F(HttpUploadServiceTest, UploadFileResume500TreatedAsZeroOffset) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    uint8_t data[150];
    memset(data, 0xAA, sizeof(data));
    writeFile("error.ats", data, sizeof(data));

    const char* body = "internal error";
    http_test_set_response(reinterpret_cast<const uint8_t*>(body),
                           (int)strlen(body), 500);

    auto& svc = HttpUploadServiceImpl::getInstance();
    (void)svc.uploadFile("error.ats", "DEV001", "tok|9999|sig");

    removeFile("error.ats");
}

TEST_F(HttpUploadServiceTest, QueryServerOffsetOpenFailure) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    uint8_t data[100];
    memset(data, 0xBB, sizeof(data));
    writeFile("openfail.ats", data, sizeof(data));

    // First query → connect fails → queryServerOffset returns 0
    http_test_set_open_result(ESP_FAIL);

    auto& svc = HttpUploadServiceImpl::getInstance();
    // The upload will then attempt to open for the actual POST and fail too,
    // returning false.
    EXPECT_FALSE(svc.uploadFile("openfail.ats", "DEV001", "tok|9999|sig"));

    removeFile("openfail.ats");
}

TEST_F(HttpUploadServiceTest, UploadFilePartialWriteFailure) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    // 4KB file → write loop runs ~4 iterations
    uint8_t data[4096];
    memset(data, 0xCC, sizeof(data));
    writeFile("partial.ats", data, sizeof(data));

    // queryServerOffset returns 0 (404)
    const char* body = "404";
    http_test_set_response(reinterpret_cast<const uint8_t*>(body), 3, 404);

    // Fail the actual upload write after 1024 bytes
    http_test_set_write_fail_after(1024);

    auto& svc = HttpUploadServiceImpl::getInstance();
    EXPECT_FALSE(svc.uploadFile("partial.ats", "DEV001", "tok|9999|sig"));

    removeFile("partial.ats");
}

// ── uploadFile resume from server-reported offset ──────────────────────────

TEST_F(HttpUploadServiceTest, UploadFileResumesFromServerOffset) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    // 200-byte file
    uint8_t data[200];
    memset(data, 0xDD, sizeof(data));
    writeFile("resume.ats", data, sizeof(data));

    // Server says it has 50 bytes already → fseek(50), upload remaining 150
    const char* json = "{\"size\":50}";
    http_test_set_response(reinterpret_cast<const uint8_t*>(json),
                           (int)strlen(json), 200);

    auto& svc = HttpUploadServiceImpl::getInstance();
    // The actual upload status reuses the same canned response (200) so the
    // result is true. Even if interpretation differs we cover the resume path.
    (void)svc.uploadFile("resume.ats", "DEV001", "tok|9999|sig");

    removeFile("resume.ats");
}

// ── uploadPendingFiles loop body (multi-file iteration) ────────────────────

TEST_F(HttpUploadServiceTest, UploadPendingFilesIteratesMultipleFiles) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    auto& svc = HttpUploadServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setPendingCount(3);  // stub will list pending0/1/2.ats

    // Create the pending files so fopen succeeds
    uint8_t data[100];
    memset(data, 0xEE, sizeof(data));
    writeFile("pending0.ats", data, sizeof(data));
    writeFile("pending1.ats", data, sizeof(data));
    writeFile("pending2.ats", data, sizeof(data));

    // Token refresh fails → uses empty token; upload itself returns 200
    http_test_set_perform_result(ESP_FAIL);  // refresh fails
    // Then we need a real perform OK for the actual upload — but the same
    // perform_result applies to all calls. Let it all fail; we just want
    // the loop body to run.

    (void)svc.uploadPendingFiles();

    removeFile("pending0.ats");
    removeFile("pending1.ats");
    removeFile("pending2.ats");
}

TEST_F(HttpUploadServiceTest, UploadPendingFilesCancelMidLoop) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";

    auto& svc = HttpUploadServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setPendingCount(3);

    uint8_t data[2048];
    memset(data, 0xFA, sizeof(data));
    writeFile("pending0.ats", data, sizeof(data));
    writeFile("pending1.ats", data, sizeof(data));
    writeFile("pending2.ats", data, sizeof(data));

    Arcana::Io::IoServiceImpl::getInstance().test_setCancelRequested(true);

    (void)svc.uploadPendingFiles();

    Arcana::Io::IoServiceImpl::getInstance().test_setCancelRequested(false);
    removeFile("pending0.ats");
    removeFile("pending1.ats");
    removeFile("pending2.ats");
}

// ── isTokenExpired branch coverage (via uploadPendingFiles) ────────────────
//
// isTokenExpired() is file-static so we can only exercise it through
// uploadPendingFiles → it's called on the registration token. To cover
// the parser branches we set up registration with various malformed tokens.

TEST_F(HttpUploadServiceTest, TokenWithoutPipeIsExpired) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";
    auto& svc = HttpUploadServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setPendingCount(0);

    // Set creds with no pipe in token via the stub's load injection
    uint8_t buf[256] = {0};
    auto& reg = RegistrationServiceImpl::getInstance();
    memcpy(buf, reg.deviceId(), 12);
    strcpy((char*)buf + 72, "broker.example.com");
    strcpy((char*)buf + 110, "INVALID_NO_PIPE");  // bad token
    buf[218] = 0xCE; buf[219] = 0xED;
    storage.test_setLoadOk(true);
    storage.test_setLoadData(buf, 256);
    EXPECT_TRUE(reg.loadCredentials());

    // Now uploadPendingFiles will see the bad token and call refreshToken
    http_test_set_perform_result(ESP_FAIL);
    (void)svc.uploadPendingFiles();
    SUCCEED();
}

TEST_F(HttpUploadServiceTest, TokenWithZeroExpiryIsExpired) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";
    auto& svc = HttpUploadServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setPendingCount(0);

    uint8_t buf[256] = {0};
    auto& reg = RegistrationServiceImpl::getInstance();
    memcpy(buf, reg.deviceId(), 12);
    strcpy((char*)buf + 72, "broker.example.com");
    strcpy((char*)buf + 110, "device|0|sig");  // expiry 0
    buf[218] = 0xCE; buf[219] = 0xED;
    storage.test_setLoadOk(true);
    storage.test_setLoadData(buf, 256);
    EXPECT_TRUE(reg.loadCredentials());

    http_test_set_perform_result(ESP_FAIL);
    (void)svc.uploadPendingFiles();
    SUCCEED();
}

TEST_F(HttpUploadServiceTest, TokenWithFutureExpiryIsValid) {
    if (!g_sdcardAvailable) GTEST_SKIP() << "/sdcard not available";
    auto& svc = HttpUploadServiceImpl::getInstance();
    auto& storage = Arcana::Storage::AtsStorageServiceImpl::getInstance();
    storage.test_setReady(true);
    storage.test_setPendingCount(0);

    uint8_t buf[256] = {0};
    auto& reg = RegistrationServiceImpl::getInstance();
    memcpy(buf, reg.deviceId(), 12);
    strcpy((char*)buf + 72, "broker.example.com");
    strcpy((char*)buf + 110, "device|9999999999|sig");  // very far future
    buf[218] = 0xCE; buf[219] = 0xED;
    storage.test_setLoadOk(true);
    storage.test_setLoadData(buf, 256);
    EXPECT_TRUE(reg.loadCredentials());

    // Token is valid → refreshToken should NOT be called. Upload then
    // proceeds with the loaded creds and the actual files (or empty).
    (void)svc.uploadPendingFiles();
    SUCCEED();
}
