#!/usr/bin/env bash
# Print the arch-qube image to use: arcana.boo/arcana/arch-qube:<tools/arch-qube.version>.
# Resolves by `docker pull` of the pinned tag (works wherever the daemon can reach the registry,
# including inside the Jenkins container where an HTTPS tags/list call from the shell cannot).
# Only if the pull fails: print :latest and warn on stderr — never silently.
# Used by tools/arch-check.sh AND the Jenkinsfile, so local and CI scan with the same image.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="${ARCH_QUBE_REPO:-arcana.boo/arcana/arch-qube}"
VERSION="$(tr -d '[:space:]' < "$ROOT/tools/arch-qube.version")"

for t in "$VERSION" "${VERSION#v}"; do
    if docker image inspect "${REPO}:${t}" >/dev/null 2>&1 || docker pull -q "${REPO}:${t}" >/dev/null 2>&1; then
        echo "${REPO}:${t}"
        exit 0
    fi
done
echo "arch-qube: WARNING — could not pull ${REPO}:${VERSION}; falling back to :latest, which may not be ${VERSION}" >&2
echo "${REPO}:latest"
