#!/usr/bin/env bash
# Start a new firmware product from this repo's full layered skeleton.
#
#   tools/new-app.sh inaplink ~/Documents/projects/inaplink               # infrastructure only (default)
#   tools/new-app.sh --with-demo inaplink ~/Documents/projects/inaplink   # keep the template demos
#
# Default is --no-demo: the template's demo features (DHT/Tsens sensor, RGB LED, SSD1306/ST7789
# + MVVM views, ECG generator / AtsStorageService, Registration, HttpUpload, SdBenchmark, Io
# buttons, BLE GATT server + encrypted command channel) are deleted from sources, CMake,
# AppContainer wiring and Tests. Why default: a product that kept them "unwired" drifted —
# dead services still compiled, were still scanned and still cost RAM, and nobody knew which
# were load-bearing. What stays: board bring-up, Timer, Diagnostic, Wi-Fi, MQTT, OTA,
# FreezeGuard, core/, db/arcanats + SdFat, and every gate. File list: tools/demo-files.txt;
# wiring is fenced in-source with "demo:begin/demo:end" markers.
#
# Copies the committed tree (git archive HEAD), so the new product keeps AGENTS.md,
# CLAUDE.md, the scaffolding scripts, Tests/, the Jenkinsfile and the arch-qube gate.
# Copying only components/ or db/ loses all of that (AGENTS.md §1).
set -euo pipefail

DEMO=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-demo) DEMO=0; shift ;;
        --with-demo) DEMO=1; shift ;;
        -*) echo "unknown option $1" >&2; exit 1 ;;
        *) break ;;
    esac
done
[ $# -eq 2 ] || { echo "usage: $0 [--no-demo|--with-demo] <app-name> <dest-dir>" >&2; exit 1; }
APP="$1"
DEST="$2"
[[ "$APP" =~ ^[a-z][a-z0-9_-]*$ ]] || { echo "app-name: lower-case letters, digits, - or _" >&2; exit 1; }
[ ! -e "$DEST" ] || { echo "$DEST already exists — refusing to overwrite" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
git -C "$ROOT" diff --quiet HEAD -- AGENTS.md tools/ || echo "note: uncommitted changes in AGENTS.md/tools are NOT copied (git archive HEAD)" >&2

mkdir -p "$DEST"
git -C "$ROOT" archive HEAD | tar -x -C "$DEST"
SRC_SHA="$(git -C "$ROOT" rev-parse --short HEAD)"

cd "$DEST"
# Product identity — the only places the template name is load-bearing.
sed -i.bak -E "s/^project\\(mqtt5\\)/project(${APP})/" CMakeLists.txt
sed -i.bak -E "s/^sonar.projectKey=.*/sonar.projectKey=${APP}/; s/^sonar.projectName=.*/sonar.projectName=${APP}/" sonar-project.properties
sed -i.bak -E "s/APP_NAME = \"esp32-app\"/APP_NAME = \"${APP}\"/; s/-Dsonar.projectKey=esp32-app/-Dsonar.projectKey=${APP}/" Jenkinsfile
rm -f CMakeLists.txt.bak sonar-project.properties.bak Jenkinsfile.bak
sed -i.bak -E "s#build/mqtt5\.bin#build/${APP}.bin#" Jenkinsfile && rm -f Jenkinsfile.bak
rm -f CHANGELOG.md .release-please-manifest.json

strip_demo_markers() {   # drop fenced demo blocks + lines tagged "// demo" / "# demo"
    awk '
        /(\/\/|#)[[:space:]]*demo:begin[[:space:]]*$/ { if (skip) { bad=1 } skip=1; next }
        /(\/\/|#)[[:space:]]*demo:end[[:space:]]*$/   { if (!skip) { bad=1 } skip=0; next }
        skip { next }
        /(\/\/|#)[[:space:]]*demo[[:space:]]*$/ { next }
        { print }
        END { if (skip || bad) exit 3 }
    ' "$1" > "$1.nodemo" || { echo "unbalanced demo markers in $1" >&2; exit 1; }
    mv "$1.nodemo" "$1"
}

if [ "$DEMO" -eq 0 ]; then
    while IFS= read -r path; do
        case "$path" in ''|\#*) continue ;; esac
        [ -e "$path" ] || { echo "tools/demo-files.txt lists missing $path (template drift)" >&2; exit 1; }
        rm -rf "$path"
    done < tools/demo-files.txt
    grep -rlE '(//|#)[[:space:]]*demo(:begin|:end)?[[:space:]]*$' main Tests | while IFS= read -r f; do
        strip_demo_markers "$f"
    done
    mkdir -p main/share/driver && touch main/share/driver/.gitkeep   # INCLUDE_DIRS keeps it
fi
printf '{\n  ".": "0.1.0"\n}\n' > .release-please-manifest.json

cat >> AGENTS.md <<EOF

## Origin

Created from arcana-embedded-esp32 @ \`${SRC_SHA}\` by \`tools/new-app.sh\` ($( [ "$DEMO" -eq 1 ] && echo "with the template demos" || echo "--no-demo: infrastructure only" )). The layers, rules
and definition of done above apply unchanged. Remove template services you do not need via
AppContainer, not by flattening.
EOF

git init -q
tools/install-hooks.sh >/dev/null
git add -A
# --no-verify only for this import commit: every file counts as "added" here, including the
# template's own legacy files (core/log/log_dummy.c). The template itself passes arch-check;
# every later commit in the new product goes through the hook.
git -c user.name="${GIT_AUTHOR_NAME:-new-app}" -c user.email="${GIT_AUTHOR_EMAIL:-new-app@localhost}" \
    commit -q --no-verify -m "chore: start ${APP} from arcana-embedded-esp32 ${SRC_SHA}"

echo "created $DEST (${APP}) from arcana-embedded-esp32 ${SRC_SHA}"
echo "next: cd $DEST && tools/new-service.sh <Name>   # add capabilities as services"
