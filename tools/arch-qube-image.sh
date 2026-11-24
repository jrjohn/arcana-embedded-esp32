#!/usr/bin/env bash
# Print the arch-qube image to use: arcana.boo/arcana/arch-qube:<tools/arch-qube.version>.
# If that tag is not in the registry yet, print :latest and warn on stderr — never silently.
# Used by tools/arch-check.sh AND the Jenkinsfile, so local and CI scan with the same image.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="${ARCH_QUBE_REPO:-arcana.boo/arcana/arch-qube}"
VERSION="$(tr -d '[:space:]' < "$ROOT/tools/arch-qube.version")"
REGISTRY="${REPO%%/*}"
NAME="${REPO#*/}"

TAGS="$(curl -fsS --max-time 10 "https://${REGISTRY}/v2/${NAME}/tags/list" 2>/dev/null || true)"
for t in "$VERSION" "${VERSION#v}"; do
    if printf '%s' "$TAGS" | grep -q "\"$t\""; then
        echo "${REPO}:${t}"
        exit 0
    fi
done
echo "arch-qube: WARNING — image tag ${VERSION} not in ${REPO} (tags: ${TAGS:-unreachable}); falling back to :latest, which may not be ${VERSION}" >&2
echo "${REPO}:latest"
