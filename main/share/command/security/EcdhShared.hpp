#pragma once

// ECDH 共享秘密 —— 取代 `mbedtls_ecdh_compute_shared`。
//
// 背景:**ESP-IDF v6.0.2 移除了 `mbedtls/private/ecdh.h`**(v6.0.1 還有),而整個
// classic ECDH 模組在 mbedtls 4.0 已被歸為 PSA 的內部實作細節,不再對外開放。
//
// 這裡不改用 PSA 的 `psa_raw_key_agreement`,因為這個韌體當初正是為了**繞開 PSA**
// 才改用 `mbedtls/private/*` 的原始 primitive:`mbedtls_md` 的 HMAC 在 IDF 6.0 會
// 轉送到 PSA,未呼叫 `psa_crypto_init()` 時直接回錯,HKDF 因此全失敗
// (見 RegistrationServiceImpl.cpp 裡 comm_key 推導處的說明)。走回 PSA 會把那個坑
// 重新踩開。`mbedtls_ecp_mul` 與 aes / sha256 / ccm 一樣是不經 PSA 的直接運算。
//
// 這個函式做的事與原本的封裝逐項相同:算 P = d·Q、拒絕無窮遠點、取 P 的 X 座標。

#ifndef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#endif
#include "mbedtls/ecp.h"
#include "mbedtls/private/ecp.h"

namespace Arcana {
namespace Crypto {

// 回傳 0 表示成功,否則為 mbedtls 錯誤碼。
inline int EcdhComputeShared(mbedtls_ecp_group* grp, mbedtls_mpi* z,
                             const mbedtls_ecp_point* Q, const mbedtls_mpi* d,
                             int (*f_rng)(void*, unsigned char*, size_t),
                             void* p_rng) {
    mbedtls_ecp_point P;
    mbedtls_ecp_point_init(&P);

    int ret = mbedtls_ecp_mul(grp, &P, d, Q, f_rng, p_rng);

    // 無窮遠點代表對方的公鑰落在小子群上 —— 共享秘密不可用,必須當成失敗,
    // 而不是把一個全零的 z 往下傳。原本的封裝也做這個檢查。
    if (ret == 0 && mbedtls_ecp_is_zero(&P)) {
        ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }
    if (ret == 0) {
        ret = mbedtls_mpi_copy(z, &P.MBEDTLS_PRIVATE(X));
    }

    mbedtls_ecp_point_free(&P);
    return ret;
}

} // namespace Crypto
} // namespace Arcana
