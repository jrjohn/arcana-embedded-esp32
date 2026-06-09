#pragma once
/*
 * Host-test stub for ESP-IDF FatFs (ff.h).
 *
 * Production code (HttpUploadServiceImpl, FatFsFilePort) goes straight to the
 * raw FatFs API for 64-bit-safe file offsets. ESP-IDF ships the real ff.h with
 * the fatfs component on-device; the host unit-test build has no FatFs, so this
 * stub backs the subset of the API we use onto plain host stdio.
 *
 * Volume mapping: on the device esp_vfs_fat mounts the SD card so that the
 * FatFs default volume root ("/foo", "0:/foo") and the "/sdcard/foo" VFS path
 * refer to the SAME file. The upload tests write fixtures via stdio under
 * "/sdcard"; production opens them via raw FatFs as "/foo". To keep that
 * equivalence on the host, f_open() rewrites any FatFs path to live under
 * "/sdcard" (strip an optional "N:" drive prefix, then prepend "/sdcard").
 *
 * Pattern mirrors the other ESP-IDF host stubs in Tests/mocks/ (esp_random.h,
 * esp_http_client_stub.cpp, mbedtls_wrap.cpp). Mocks are first on the test
 * include path so this shadows the real ff.h.
 */

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

typedef unsigned char      BYTE;
typedef unsigned int       UINT;
typedef unsigned long long FSIZE_t;   /* 64-bit, matches FF_FS_EXFAT offsets */

/* f_open() access-mode flags (real FatFs values). */
#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW    0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS   0x10
#define FA_OPEN_APPEND   0x30

/* FatFs result codes (subset actually compared against in our code). */
typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT
} FRESULT;

/* Minimal filesystem object — only n_fats is read (FatFsFilePort::truncate). */
typedef struct {
    BYTE n_fats;
} FATFS;

typedef struct {
    FATFS*  fs;
    FSIZE_t objsize;
} FFOBJID;

/* File object. Real FatFs exposes obj/err/fptr; we add a host FILE* backing. */
typedef struct {
    FFOBJID obj;
    BYTE    err;
    FSIZE_t fptr;
    FILE*   _host_fp;
} FIL;

/* f_size()/f_tell() are macros in real FatFs — keep them as macros. */
#define f_size(fp) ((fp)->obj.objsize)
#define f_tell(fp) ((fp)->fptr)

/* Map a FatFs path ("/foo", "0:/foo") onto the host "/sdcard" mount. */
static inline void _ff_host_path(const char* path, char* out, size_t outSize) {
    const char* p = path;
    if (p[0] && p[1] == ':') p += 2;   /* strip optional "N:" drive prefix */
    if (*p == '/') p += 1;             /* drop the leading slash (we add /sdcard/) */
    snprintf(out, outSize, "/sdcard/%s", p);
}

static inline FRESULT f_open(FIL* fp, const char* path, BYTE mode) {
    static FATFS s_fs = { /*n_fats=*/2 };
    if (!fp) return FR_INVALID_OBJECT;
    memset(fp, 0, sizeof(*fp));

    char host[128];
    _ff_host_path(path, host, sizeof(host));

    const char* fmode;
    if (mode & FA_CREATE_ALWAYS)      fmode = "wb+";
    else if (mode & FA_WRITE)         fmode = "rb+";
    else                              fmode = "rb";

    FILE* h = fopen(host, fmode);
    if (!h) return FR_NO_FILE;

    fseek(h, 0, SEEK_END);
    long sz = ftell(h);
    fseek(h, 0, SEEK_SET);

    fp->_host_fp     = h;
    fp->obj.fs       = &s_fs;
    fp->obj.objsize  = (sz < 0) ? 0 : (FSIZE_t)sz;
    fp->fptr         = 0;
    fp->err          = 0;
    return FR_OK;
}

static inline FRESULT f_close(FIL* fp) {
    if (!fp || !fp->_host_fp) return FR_INVALID_OBJECT;
    fclose(fp->_host_fp);
    fp->_host_fp = nullptr;
    return FR_OK;
}

static inline FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br) {
    if (!fp || !fp->_host_fp) return FR_INVALID_OBJECT;
    size_t n = fread(buff, 1, btr, fp->_host_fp);
    if (ferror(fp->_host_fp)) { if (br) *br = 0; return FR_DISK_ERR; }
    if (br) *br = (UINT)n;
    fp->fptr += n;
    return FR_OK;
}

static inline FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw) {
    if (!fp || !fp->_host_fp) return FR_INVALID_OBJECT;
    size_t n = fwrite(buff, 1, btw, fp->_host_fp);
    if (bw) *bw = (UINT)n;
    fp->fptr += n;
    if (fp->fptr > fp->obj.objsize) fp->obj.objsize = fp->fptr;
    return (n == btw) ? FR_OK : FR_DISK_ERR;
}

static inline FRESULT f_lseek(FIL* fp, FSIZE_t ofs) {
    if (!fp || !fp->_host_fp) return FR_INVALID_OBJECT;
    if (fseek(fp->_host_fp, (long)ofs, SEEK_SET) != 0) return FR_DISK_ERR;
    fp->fptr = ofs;
    return FR_OK;
}

static inline FRESULT f_sync(FIL* fp) {
    if (!fp || !fp->_host_fp) return FR_INVALID_OBJECT;
    return (fflush(fp->_host_fp) == 0) ? FR_OK : FR_DISK_ERR;
}

static inline FRESULT f_truncate(FIL* fp) {
    if (!fp || !fp->_host_fp) return FR_INVALID_OBJECT;
    fflush(fp->_host_fp);
    if (ftruncate(fileno(fp->_host_fp), (off_t)fp->fptr) != 0) return FR_DISK_ERR;
    fp->obj.objsize = fp->fptr;
    return FR_OK;
}

static inline FRESULT f_mount(FATFS* fs, const char* path, BYTE opt) {
    (void)fs; (void)path; (void)opt;
    return FR_OK;
}
