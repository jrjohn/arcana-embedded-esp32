#!/usr/bin/env bash
# Internal-RAM budget gate (AGENTS.md §6, rule R10). Run after a firmware build.
#
#   tools/ram-budget.sh [build-dir]          # default: build
#   RAM_BUDGET_MAX=90000 tools/ram-budget.sh  # one-off override (control runs only)
#
# Fails (exit 1) when the static internal RAM of the built image exceeds the budget for
# its target in tools/ram-budget.conf, or when the built sdkconfig has the task-watchdog
# panic switched off (a stale sdkconfig silently drops sdkconfig.defaults).
# Exit 2 = could not measure (no map file, no esp_idf_size) — that is NOT a pass.
#
# What is measured: .data + .bss in DRAM (classic ESP32) / DIRAM (ESP32-S3), i.e. the internal
# RAM taken at link time. Host tests and arch-check cannot see memory; whatever this takes is
# gone from the heap the BLE controller, Wi-Fi, lwIP and TLS allocate from at runtime. A product
# from this template passed every other gate at 116,681 B of .data+.bss on an S3 and booted
# with 6.9 KB free; BLE and Wi-Fi failed to allocate.
#
# Needs python3 with esp_idf_size (inside `. $IDF_PATH/export.sh` or the espressif/idf image).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-build}"
[ -d "$BUILD" ] || BUILD="$ROOT/$BUILD"
CONF="${RAM_BUDGET_CONF:-$ROOT/tools/ram-budget.conf}"

die() { echo "ram-budget: $* — NOT measured (this is NOT a pass)" >&2; exit 2; }

[ -f "$BUILD/project_description.json" ] || die "$BUILD/project_description.json missing; build first"
PY="${PYTHON:-python3}"
command -v "$PY" >/dev/null || die "python3 not found"

read -r TARGET MAP < <("$PY" - "$BUILD/project_description.json" <<'EOF'
import json, os, sys
d = json.load(open(sys.argv[1]))
build = os.path.dirname(os.path.abspath(sys.argv[1]))
print(d["target"], os.path.join(build, d["app_elf"].replace(".elf", ".map")))
EOF
)
[ -f "$MAP" ] || die "map file $MAP missing"

MAX="${RAM_BUDGET_MAX:-$(awk -v t="$TARGET" '$1==t {print $2}' "$CONF")}"
[ -n "$MAX" ] || die "no budget for target '$TARGET' in $CONF"

SIZE_JSON="$(mktemp)"
trap 'rm -f "$SIZE_JSON"' EXIT
"$PY" -m esp_idf_size --format json2 "$MAP" >"$SIZE_JSON" 2>/dev/null || die "esp_idf_size failed (run inside the IDF environment)"

read -r USED REGION_USED REGION_TOTAL < <("$PY" - "$SIZE_JSON" <<'EOF'
import json, sys
layout = {l["name"]: l for l in json.load(open(sys.argv[1]))["layout"]}
# Classic ESP32: DRAM. ESP32-S3 / C-series: DIRAM (shared with IRAM code).
region = layout.get("DIRAM") or layout.get("DRAM")
if region:
    parts = region["parts"]
    static = sum(parts.get(k, {}).get("size", 0) for k in (".data", ".bss"))
    print(static, region["used"], region["total"])
EOF
)
[ -n "$USED" ] || die "no DIRAM/DRAM region in esp_idf_size output"

SDKCONFIG="$BUILD/sdkconfig"
[ -f "$SDKCONFIG" ] || SDKCONFIG="$ROOT/sdkconfig"
WDT_OK=1
if [ -f "$SDKCONFIG" ] && ! grep -q '^CONFIG_ESP_TASK_WDT_PANIC=y' "$SDKCONFIG"; then
    WDT_OK=0
fi

echo "ram-budget: target=$TARGET static .data+.bss=${USED} B budget=${MAX} B ($(( USED * 100 / MAX ))% of budget); region used ${REGION_USED}/${REGION_TOTAL} B incl. code"
RC=0
if [ "$USED" -gt "$MAX" ]; then
    echo "ram-budget: FAIL — static .data+.bss ${USED} B > ${MAX} B. Move CPU-only buffers and task stacks to PSRAM (core/memory/MemoryPlacement.hpp); do not raise the budget to get a pass (AGENTS.md R10)" >&2
    RC=1
fi
if [ "$WDT_OK" -eq 0 ]; then
    echo "ram-budget: FAIL — $SDKCONFIG has no CONFIG_ESP_TASK_WDT_PANIC=y (stale sdkconfig? delete it and rebuild; AGENTS.md R11)" >&2
    RC=1
fi
[ "$RC" -eq 0 ] && echo "ram-budget: PASS"
exit "$RC"
