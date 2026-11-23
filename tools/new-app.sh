#!/usr/bin/env bash
# Start a new firmware product from this repo's full layered skeleton.
#
#   tools/new-app.sh inaplink ~/Documents/projects/inaplink
#
# Copies the committed tree (git archive HEAD), so the new product keeps AGENTS.md,
# CLAUDE.md, the scaffolding scripts, Tests/, the Jenkinsfile and the arch-qube gate.
# Copying only components/ or db/ loses all of that (AGENTS.md §1).
set -euo pipefail

[ $# -eq 2 ] || { echo "usage: $0 <app-name> <dest-dir>" >&2; exit 1; }
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
rm -f CHANGELOG.md .release-please-manifest.json
printf '{\n  ".": "0.1.0"\n}\n' > .release-please-manifest.json

cat >> AGENTS.md <<EOF

## Origin

Created from arcana-embedded-esp32 @ \`${SRC_SHA}\` by \`tools/new-app.sh\`. The layers, rules
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
