#include "impl/RegistrationServiceImpl.hpp"
#include "impl/AtsStorageServiceImpl.hpp"
#include "FrameCodec.hpp"
#include "Esp32AesCtrCipher.hpp"   // ESP32 HW AES-256-CTR for the registration response
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "EspRng.hpp"

// mbedtls 4.0 (IDF 6.0+) gates legacy mbedtls_* low-level identifiers behind
// this macro. Define BEFORE pulling in the private headers.
// Note: mbedtls_entropy_* / mbedtls_ctr_drbg_* are GONE in 4.0; we wrap
// esp_fill_random() via EspRng.hpp instead.
#ifndef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#endif
#include "mbedtls/ecp.h"
#include "EcdhShared.hpp"
#include "CryptoMac.hpp"
#include <cstring>
#include <cstdio>

static const char* TAG = "Registration";

// Registration server config
#ifndef CONFIG_REG_SERVER_HOST
#define CONFIG_REG_SERVER_HOST  "arcana.boo"
#endif
#ifndef CONFIG_REG_SERVER_PORT
#define CONFIG_REG_SERVER_PORT  443
#endif

namespace Arcana::Registration {

// ---------------------------------------------------------------------------
// Minimal protobuf encoder/decoder (no .proto compile needed)
// ---------------------------------------------------------------------------

static void pbWriteVarint(uint8_t*& p, uint32_t v) {
    while (v > 0x7F) { *p++ = (v & 0x7F) | 0x80; v >>= 7; }
    *p++ = v & 0x7F;
}

static void pbWriteField(uint8_t*& p, uint8_t fieldNum, uint8_t wireType,
                          const void* data, size_t len) {
    pbWriteVarint(p, (fieldNum << 3) | wireType);
    if (wireType == 0) {  // varint
        pbWriteVarint(p, *(const uint32_t*)data);
    } else if (wireType == 2) {  // length-delimited
        pbWriteVarint(p, (uint32_t)len);
        memcpy(p, data, len);
        p += len;
    }
}

struct PbField { uint8_t num; const uint8_t* data; size_t len; bool isVarint; uint32_t varintVal; };

static int pbDecode(const uint8_t* buf, size_t bufLen, PbField* fields, int maxFields) {
    int count = 0;
    size_t off = 0;
    while (off < bufLen && count < maxFields) {
        uint8_t tag = buf[off++];
        uint8_t fn = tag >> 3;
        uint8_t wt = tag & 7;
        PbField& f = fields[count];
        f.num = fn;
        if (wt == 0) {  // varint
            uint32_t v = 0; int shift = 0;
            while (off < bufLen) {
                uint8_t b = buf[off++];
                v |= (uint32_t)(b & 0x7F) << shift;
                shift += 7;
                if (!(b & 0x80)) break;
            }
            f.isVarint = true; f.varintVal = v;
            f.data = nullptr; f.len = 0;
        } else if (wt == 2) {  // length-delimited
            uint32_t l = 0; int shift = 0;
            while (off < bufLen) {
                uint8_t b = buf[off++];
                l |= (uint32_t)(b & 0x7F) << shift;
                shift += 7;
                if (!(b & 0x80)) break;
            }
            f.isVarint = false; f.varintVal = 0;
            f.data = buf + off; f.len = l;
            off += l;
        } else {
            break;
        }
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Credential pack/unpack (same format as STM32)
// ---------------------------------------------------------------------------

static const uint16_t CRED_PLAIN_SIZE = 272;   // >= CREDS data field (264) + slack
static const uint8_t CRED_VALID_MAGIC[2] = {0xCE, 0xED};

static void packCreds(uint8_t* plain, const RegistrationService::Credentials& c) {
    memset(plain, 0, CRED_PLAIN_SIZE);
    uint16_t off = 0;
    memcpy(plain + off, c.mqttUser,    36); off += 36;
    memcpy(plain + off, c.mqttPass,    36); off += 36;
    memcpy(plain + off, c.mqttBroker,  36); off += 36;
    memcpy(plain + off, &c.mqttPort,    2); off += 2;
    memcpy(plain + off, c.uploadToken, 72); off += 72;
    memcpy(plain + off, c.topicPrefix, 36); off += 36;
    // Validation magic (offset 218-219)
    plain[off] = CRED_VALID_MAGIC[0]; off += 1;  // off=218→219
    plain[off] = CRED_VALID_MAGIC[1]; off += 1;  // off=219→220
    plain[off] = c.hasCommKey ? 1 : 0; off += 1;  // off=220→221
    // Per-device ECDH command key (offset 221-252). The CREDS record was
    // enlarged (232→264 data) specifically to persist this — it survives
    // reboots so the command channel re-keys without a fresh registration.
    memcpy(plain + off, c.commKey, 32); off += 32;  // off=221→253
}

static bool unpackCreds(const uint8_t* plain, RegistrationService::Credentials& c) {
    // Layout: [user:36][pass:36][broker:36][port:2][token:72][prefix:36]=218
    //         [magic:2][hasCommKey:1][commKey:32] = 253 total
    // Old 232-byte records have a different layout → the magic check below fails
    // on them, so we never read a stale commKey (device re-registers instead).

    if (plain[218] != CRED_VALID_MAGIC[0] || plain[219] != CRED_VALID_MAGIC[1])
        return false;

    uint16_t off = 0;
    memcpy(c.mqttUser,    plain + off, 36); off += 36;
    memcpy(c.mqttPass,    plain + off, 36); off += 36;
    memcpy(c.mqttBroker,  plain + off, 36); off += 36;
    memcpy(&c.mqttPort,   plain + off,  2); off += 2;
    memcpy(c.uploadToken, plain + off, 72); off += 72;
    memcpy(c.topicPrefix, plain + off, 36); off += 36;
    // off=218, skip magic (2 bytes)
    off += 2;
    c.hasCommKey = (plain[off] == 1); off += 1;   // off=220→221
    memcpy(c.commKey, plain + off, 32);            // off=221→253

    return c.mqttUser[0] != '\0' && c.mqttBroker[0] != '\0';
}

// ---------------------------------------------------------------------------
// Singleton + init
// ---------------------------------------------------------------------------

RegistrationServiceImpl::RegistrationServiceImpl() {
    // Device ID from MAC (12-char hex)
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        mDeviceId[i * 2]     = hex[mac[i] >> 4];
        mDeviceId[i * 2 + 1] = hex[mac[i] & 0x0F];
    }
    mDeviceId[12] = '\0';

    // Derive device key from MAC
    for (int i = 0; i < 32; i++) {
        mDeviceKey[i] = mac[i % 6] ^ (uint8_t)(0xA5 + i);
    }
}

RegistrationServiceImpl& RegistrationServiceImpl::getInstance() {
    static RegistrationServiceImpl sInstance;
    return sInstance;
}

// ---------------------------------------------------------------------------
// Credential persistence via device.ats ch2
// ---------------------------------------------------------------------------

bool RegistrationServiceImpl::loadCredentials() {
    auto& storage = static_cast<Storage::AtsStorageServiceImpl&>(
        Storage::AtsStorageServiceImpl::getInstance());
    if (!storage.isReady()) return false;

    uint8_t data[CRED_PLAIN_SIZE];
    memset(data, 0, sizeof(data));
    uint16_t dataLen = 0;
    bool loadOk = storage.loadCredentials(data, sizeof(data), dataLen);
    ESP_LOGI(TAG, "loadCredentials: ok=%d len=%u magic=[%02X,%02X]",
             loadOk, dataLen, data[218], data[219]);
    if (!loadOk || dataLen < 220)
        return false;

    if (unpackCreds(data, mCreds)) {
        if (strncmp(mCreds.mqttUser, mDeviceId, 12) == 0) {
            mCreds.valid = true;
            ESP_LOGI(TAG, "Credentials loaded from device.ats (user=%s broker=%s:%u)",
                     mCreds.mqttUser, mCreds.mqttBroker, mCreds.mqttPort);
            ESP_LOGI(TAG, "  uploadToken=%.60s...", mCreds.uploadToken);
            ESP_LOGI(TAG, "  topicPrefix=%s", mCreds.topicPrefix);
            return true;
        }
        ESP_LOGW(TAG, "Stored credentials device mismatch");
    }
    mCreds.valid = false;
    return false;
}

bool RegistrationServiceImpl::saveCredentials() {
    auto& storage = static_cast<Storage::AtsStorageServiceImpl&>(
        Storage::AtsStorageServiceImpl::getInstance());
    if (!storage.isReady()) return false;

    uint8_t data[CRED_PLAIN_SIZE];
    packCreds(data, mCreds);
    // ATS credentials record = [ts:4][data:264] = 268 bytes (ATS adds the ts)
    bool ok = storage.saveCredentials(data, 264);
    ESP_LOGI(TAG, "Credentials %s to device.ats", ok ? "saved" : "SAVE FAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// HTTP Registration
// ---------------------------------------------------------------------------

bool RegistrationServiceImpl::doRegistration() {
    // Use cached creds, else load from storage.
    bool haveCreds = mCreds.valid || (loadCredentials() && mCreds.valid);

#ifdef CONFIG_CMD_ENCRYPTION_ENABLED
    // Command encryption needs a per-device ECDH commKey. If we have creds but
    // no commKey (e.g. the device registered before the server supported ECDH),
    // re-register once to obtain one — otherwise the command channel would stay
    // on the compile-time bootstrap PSK forever.
    if (haveCreds && mCreds.hasCommKey) return true;
    if (haveCreds)
        ESP_LOGI(TAG, "Creds present but no commKey — re-registering for per-device key");
#else
    if (haveCreds) return true;
#endif

    // POST to server
    if (!httpRegister()) return false;

    // Save credentials (retry — device.ats ch2 may need live upgrade)
    if (!saveCredentials()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        saveCredentials();
    }
    return true;
}

bool RegistrationServiceImpl::refreshToken() {
    ESP_LOGI(TAG, "Refreshing upload token (re-register)...");
    mCreds.valid = false;  // bypass doRegistration() early return

    if (!httpRegister()) {
        ESP_LOGE(TAG, "Token refresh failed");
        return false;
    }

    // LCOV_EXCL_START — IEC 62304 §5.5.3. saveCredentials retry needs
    // an NVS-write fail-after-N mock to drive; covered via HIL.
    if (!saveCredentials()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        saveCredentials();
    }
    // LCOV_EXCL_STOP

    ESP_LOGI(TAG, "Token refreshed: %.60s...", mCreds.uploadToken);
    return mCreds.valid;
}

// HTTP response buffer (shared static to avoid stack pressure)
static uint8_t sRespBuf[512];
static int sRespLen = 0;

static esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int remaining = (int)sizeof(sRespBuf) - sRespLen;
        int copyLen = (evt->data_len < remaining) ? evt->data_len : remaining;
        if (copyLen > 0) {
            memcpy(sRespBuf + sRespLen, evt->data, copyLen);
            sRespLen += copyLen;
        }
    }
    return ESP_OK;
}

bool RegistrationServiceImpl::httpRegister() {
    ESP_LOGI(TAG, "Registering device %s at %s:%d",
             mDeviceId, CONFIG_REG_SERVER_HOST, CONFIG_REG_SERVER_PORT);

    // --- Generate ephemeral ECDH keypair (mbedtls ecp_keypair) ---
    // mbedtls 4.0 removed ctr_drbg/entropy modules; use HW TRNG via EspRng.
    mbedtls_ecp_keypair kp;
    mbedtls_ecp_keypair_init(&kp);

    mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &kp,
                         Crypto::EspRngCallback, nullptr);

    // Export public key (64 bytes: x || y)
    uint8_t devPub[64];
    size_t olen = 0;
    uint8_t pubUncompressed[65];
    mbedtls_ecp_point_write_binary(&kp.MBEDTLS_PRIVATE(grp),
                                    &kp.MBEDTLS_PRIVATE(Q),
                                    MBEDTLS_ECP_PF_UNCOMPRESSED,
                                    &olen, pubUncompressed, sizeof(pubUncompressed));
    memcpy(devPub, pubUncompressed + 1, 64);  // skip 0x04 prefix

    // --- Encode RegisterRequest protobuf ---
    uint8_t pbBuf[256];
    uint8_t* p = pbBuf;

    // field 1: device_id (string)
    pbWriteField(p, 1, 2, mDeviceId, strlen(mDeviceId));
    // field 2: public_key (bytes, 64)
    pbWriteField(p, 2, 2, mDeviceKey, 64);  // pad to 64 (first 32 = key, rest = 0)
    // field 3: firmware_ver (varint)
    uint32_t fwVer = 0x0100;
    pbWriteField(p, 3, 0, &fwVer, 0);
    // field 4: ecdh_pub (bytes, 64)
    pbWriteField(p, 4, 2, devPub, 64);
    // field 5: cipher (varint) — 2 = AES-256-CTR (ESP32 has HW AES). The server
    // encrypts the response with this cipher; if absent it defaults to 1=ChaCha20
    // (STM32, which has no AES hardware), so STM32 stays on ChaCha20 unchanged.
    uint32_t cipher = 2;
    pbWriteField(p, 5, 0, &cipher, 0);

    uint16_t pbLen = (uint16_t)(p - pbBuf);

    // --- Wrap in FrameCodec ---
    uint8_t frame[300];
    size_t frameLen = 0;
    // LCOV_EXCL_START — IEC 62304 §5.5.3. FrameCodec::Frame fails only
    // if the destination buffer (300 bytes) is too small for the encoded
    // payload, but pbBuf is statically sized to fit within 300 bytes.
    // Defensive cleanup for a future schema bump.
    if (!Arcana::Command::FrameCodec::Frame(pbBuf, pbLen,
                                             frame, sizeof(frame), frameLen,
                                             Arcana::Command::FrameCodec::kFlagFin, 0x10)) {
        ESP_LOGE(TAG, "FrameCodec encode failed");
        mbedtls_ecp_keypair_free(&kp);
        return false;
    }
    // LCOV_EXCL_STOP

    // --- HTTPS POST via the reverse proxy (nginx 443 → reg-api); the
    // internal :8088 is loopback-only, so the device must go through 443. ---
    char url[128];
    snprintf(url, sizeof(url), "https://%s:%d/api/register",
             CONFIG_REG_SERVER_HOST, CONFIG_REG_SERVER_PORT);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 15000;
    cfg.event_handler = httpEventHandler;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;   // verify arcana.boo's LE cert

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, (const char*)frame, (int)frameLen);

    sRespLen = 0;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP POST failed: err=%s status=%d", esp_err_to_name(err), status);
        mbedtls_ecp_keypair_free(&kp);
        return false;
    }

    ESP_LOGI(TAG, "Server response: %d bytes, HTTP %d", sRespLen, status);

    // --- Find FrameCodec frame in response ---
    bool found = false;
    for (int i = 0; i + 9 <= sRespLen; i++) {
        if (sRespBuf[i] == 0xAC && sRespBuf[i + 1] == 0xDA && sRespBuf[i + 2] == 0x01) {
            uint16_t pLen = sRespBuf[i + 5] | (sRespBuf[i + 6] << 8);
            uint16_t totalFrame = 7 + pLen + 2;
            if (i + totalFrame <= sRespLen) {
                const uint8_t* framePayload = sRespBuf + i + 7;
                // Try cleartext protobuf first
                found = parseResponse(framePayload, pLen);
                // If encrypted (nonce + ciphertext), decrypt with device key
                // LCOV_EXCL_START — IEC 62304 §5.5.3. The encrypted-response
                // fallback is only triggered when the server returns an
                // encrypted payload (post-TOFU re-registration). Reaching
                // it from a host test requires a full HTTP server mock that
                // can return both cleartext and encrypted variants. Covered
                // via integration tests against the real registration server.
                if (!found && pLen > 12) {
                    uint8_t decBuf[256];
                    memcpy(decBuf, framePayload + 12, pLen - 12);
                    // AES-256-CTR decrypt (we sent cipher=2; server encrypts the
                    // response with HW AES). nonce = framePayload[0:12], key =
                    // device public_key[:32], counter=0 — matches the storage cipher's
                    // IV layout. STM32 (cipher=1) keeps its own ChaCha20 path.
                    arcana::ats::Esp32AesCtrCipher aes;
                    aes.crypt(mDeviceKey, framePayload, 0, decBuf, pLen - 12);
                    found = parseResponse(decBuf, pLen - 12);
                }
                // LCOV_EXCL_STOP
            }
            break;
        }
    }

    if (!found || !mCreds.valid) {
        ESP_LOGE(TAG, "Failed to parse registration response");
        mbedtls_ecp_keypair_free(&kp);
        return false;
    }

    // --- ECDH: derive comm_key ---
    if (mServerPubLen == 64) {
        // Import server public key as ecp_point
        mbedtls_ecp_point serverQ;
        mbedtls_ecp_point_init(&serverQ);

        uint8_t serverPubUncomp[65];
        serverPubUncomp[0] = 0x04;
        memcpy(serverPubUncomp + 1, mServerPub, 64);
        mbedtls_ecp_point_read_binary(&kp.MBEDTLS_PRIVATE(grp),
                                       &serverQ, serverPubUncomp, 65);

        // Compute shared secret: shared = d * serverQ
        mbedtls_mpi shared;
        mbedtls_mpi_init(&shared);
        // 不用 mbedtls_ecdh_compute_shared —— IDF 6.0.2 移除了
        // mbedtls/private/ecdh.h,理由與替代做法見 EcdhShared.hpp。
        int ret = Crypto::EcdhComputeShared(&kp.MBEDTLS_PRIVATE(grp),
                                            &shared, &serverQ,
                                            &kp.MBEDTLS_PRIVATE(d),
                                            Crypto::EspRngCallback, nullptr);
        uint8_t sharedBuf[32];
        size_t sharedLen = mbedtls_mpi_size(&shared);
        if (ret == 0 && sharedLen <= 32) {
            mbedtls_mpi_write_binary(&shared, sharedBuf, 32);

            // comm_key = HKDF-SHA256(ikm=shared, salt=device_id[:8],
            //                        info="ARCANA-COMM"). Built on the raw SHA-256
            // primitive via Crypto:: — the mbedtls_md HMAC layer fails at runtime
            // on IDF 6.0 (PSA dispatch without psa_crypto_init), which would yield
            // a comm_key that does NOT match the Python server's. Must stay
            // byte-identical to ecdh_derive_comm_key() server-side.
            const uint8_t info[] = "ARCANA-COMM";  // 11 chars, no null
            Crypto::HkdfSha256(sharedBuf, 32,
                               (const uint8_t*)mDeviceId, 8,
                               info, sizeof(info) - 1,
                               mCreds.commKey, 32);

            mCreds.hasCommKey = true;
            ESP_LOGI(TAG, "ECDH comm_key derived");
        } else {
            ESP_LOGW(TAG, "ECDH compute_shared failed: %d", ret);
        }
        mbedtls_mpi_free(&shared);
        mbedtls_ecp_point_free(&serverQ);
    }

    mbedtls_ecp_keypair_free(&kp);

    ESP_LOGI(TAG, "Registration successful: user=%s broker=%s:%u",
             mCreds.mqttUser, mCreds.mqttBroker, mCreds.mqttPort);
    ESP_LOGI(TAG, "  uploadToken=%.60s...", mCreds.uploadToken);
    return true;
}

bool RegistrationServiceImpl::parseResponse(const uint8_t* payload, uint16_t len) {
    PbField fields[12];
    int n = pbDecode(payload, len, fields, 12);
    if (n < 1) return false;

    // Check field 1 (success)
    bool success = false;
    for (int i = 0; i < n; i++) {
        if (fields[i].num == 1 && fields[i].isVarint) {
            success = (fields[i].varintVal != 0);
        }
    }
    if (!success) {
        ESP_LOGW(TAG, "Server returned success=false");
        return false;
    }

    // Extract string fields
    for (int i = 0; i < n; i++) {
        PbField& f = fields[i];
        if (f.isVarint) {
            if (f.num == 5) mCreds.mqttPort = (uint16_t)f.varintVal;
            continue;
        }
        if (!f.data || f.len == 0) continue;

        switch (f.num) {
            case 2: memcpy(mCreds.mqttUser,   f.data, f.len < 35 ? f.len : 35); break;
            case 3: memcpy(mCreds.mqttPass,   f.data, f.len < 35 ? f.len : 35); break;
            case 4: memcpy(mCreds.mqttBroker, f.data, f.len < 35 ? f.len : 35); break;
            case 6: memcpy(mCreds.uploadToken,f.data, f.len < 71 ? f.len : 71); break;
            case 7: memcpy(mCreds.topicPrefix,f.data, f.len < 35 ? f.len : 35); break;
            case 9:  // server_pub (64 bytes)
                if (f.len == 64) { memcpy(mServerPub, f.data, 64); mServerPubLen = 64; }
                break;
        }
    }

    mCreds.valid = true;
    return true;
}

} // namespace Arcana::Registration
