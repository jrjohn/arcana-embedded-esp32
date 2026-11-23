#!/usr/bin/env bash
# Scaffold a layered service: interface + impl + host test, wired into both CMakeLists.
#
#   tools/new-service.sh Pairing            # -> PairingService, namespace Arcana::Pairing
#   tools/new-service.sh O2Ring Oximeter    # -> O2RingService, namespace Arcana::Oximeter
#
# After running: wire it in main/share/AppContainer.{hpp,cpp} (wireServices / initServices /
# startServices). That is the only file allowed to include the Impl header (AGENTS.md R2).
set -euo pipefail

[ $# -ge 1 ] || { echo "usage: $0 <Name> [namespace]" >&2; exit 1; }
NAME="$1"
NS="${2:-$1}"
[[ "$NAME" =~ ^[A-Z][A-Za-z0-9]*$ ]] || { echo "Name must be PascalCase (e.g. Pairing)" >&2; exit 1; }
[[ "$NS" =~ ^[A-Z][A-Za-z0-9]*$ ]] || { echo "namespace must be PascalCase" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SVC="$ROOT/main/share/service"
IFACE="$SVC/${NAME}Service.hpp"
IMPL_H="$SVC/impl/${NAME}ServiceImpl.hpp"
IMPL_C="$SVC/impl/${NAME}ServiceImpl.cpp"
LOWER="$(printf '%s' "$NAME" | sed -E 's/([a-z0-9])([A-Z])/\1_\2/g' | tr '[:upper:]' '[:lower:]')"
TEST="$ROOT/Tests/test_${LOWER}_service.cpp"

for f in "$IFACE" "$IMPL_H" "$IMPL_C" "$TEST"; do
    [ ! -e "$f" ] || { echo "exists: ${f#$ROOT/} — refusing to overwrite" >&2; exit 1; }
done

cat > "$IFACE" <<EOF
#pragma once

#include "Observable.hpp"
#include "esp_err.h"

namespace Arcana {
namespace ${NS} {

// TODO: replace with the real value type this service publishes.
struct ${NAME}State {
    int Value = 0;
};

class ${NAME}Service {
public:
    struct Input {
        // Observables this service subscribes to, wired by AppContainer.
    };

    struct Output {
        Observable<${NAME}State>* StateObservable = nullptr;
    };

    Input input;
    Output output;

    virtual ~${NAME}Service() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;
};

} // namespace ${NS}
} // namespace Arcana
EOF

cat > "$IMPL_H" <<EOF
#pragma once

#include "${NAME}Service.hpp"
#include <atomic>

namespace Arcana {
namespace ${NS} {

class ${NAME}ServiceImpl : public ${NAME}Service {
public:
    static ${NAME}Service& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

    ${NAME}ServiceImpl();
    ~${NAME}ServiceImpl() override = default;
    ${NAME}ServiceImpl(const ${NAME}ServiceImpl&) = delete;
    ${NAME}ServiceImpl& operator=(const ${NAME}ServiceImpl&) = delete;

private:
    Observable<${NAME}State> mState;
    std::atomic<bool> mRunning{false};
};

} // namespace ${NS}
} // namespace Arcana
EOF

cat > "$IMPL_C" <<EOF
#include "${NAME}ServiceImpl.hpp"

namespace Arcana {
namespace ${NS} {

${NAME}Service& ${NAME}ServiceImpl::getInstance() {
    static ${NAME}ServiceImpl sInstance;
    return sInstance;
}

${NAME}ServiceImpl::${NAME}ServiceImpl() {
    output.StateObservable = &mState;
}

esp_err_t ${NAME}ServiceImpl::init_HAL() { return ESP_OK; }

esp_err_t ${NAME}ServiceImpl::init() { return ESP_OK; }

esp_err_t ${NAME}ServiceImpl::start() {
    mRunning = true;
    return ESP_OK;
}

void ${NAME}ServiceImpl::stop() { mRunning = false; }

} // namespace ${NS}
} // namespace Arcana
EOF

cat > "$TEST" <<EOF
#include <gtest/gtest.h>
#include "${NAME}ServiceImpl.hpp"

using namespace Arcana::${NS};

TEST(${NAME}ServiceTest, LifecycleReturnsOk) {
    ${NAME}ServiceImpl svc;
    EXPECT_EQ(svc.init_HAL(), ESP_OK);
    EXPECT_EQ(svc.init(), ESP_OK);
    EXPECT_EQ(svc.start(), ESP_OK);
    svc.stop();
}

TEST(${NAME}ServiceTest, ExposesStateObservable) {
    ${NAME}ServiceImpl svc;
    ASSERT_NE(svc.output.StateObservable, nullptr);
}
EOF

# main/CMakeLists.txt: append to the service/impl/ block (first blank line after its header).
MAIN_CM="$ROOT/main/CMakeLists.txt"
grep -q '# service/impl/' "$MAIN_CM" || { echo "main/CMakeLists.txt has no '# service/impl/' section — add the .cpp by hand" >&2; exit 1; }
awk -v line="        \"share/service/impl/${NAME}ServiceImpl.cpp\"" '
    /# service\/impl\// { insec = 1 }
    insec && /^[[:space:]]*$/ { print line; insec = 0 }
    { print }
' "$MAIN_CM" > "$MAIN_CM.tmp" && mv "$MAIN_CM.tmp" "$MAIN_CM"

# Tests/CMakeLists.txt: one executable per service, same shape as the existing ones.
cat >> "$ROOT/Tests/CMakeLists.txt" <<EOF

# ── test_${LOWER}_service (scaffolded by tools/new-service.sh) ─────────────────
add_executable(test_${LOWER}_service
    test_${LOWER}_service.cpp
    \${MAIN_DIR}/share/service/impl/${NAME}ServiceImpl.cpp
)
target_include_directories(test_${LOWER}_service PRIVATE \${COMMON_INCS})
target_link_libraries(test_${LOWER}_service PRIVATE GTest::gtest_main esp_stubs)
add_test(NAME test_${LOWER}_service COMMAND test_${LOWER}_service)
EOF

echo "created:"
for f in "$IFACE" "$IMPL_H" "$IMPL_C" "$TEST"; do echo "  ${f#$ROOT/}"; done
echo "updated: main/CMakeLists.txt, Tests/CMakeLists.txt"
echo "next: wire ${NS}::${NAME}ServiceImpl in main/share/AppContainer.{hpp,cpp}, then run tools/arch-check.sh"
