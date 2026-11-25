#!/usr/bin/env bash
# Print the arch-qube image to use, pinned to tools/arch-qube.version.
# Tried in order: ghcr.io (public, multi-arch), then arcana.boo. Resolved by `docker pull` of the
# pinned tag (works wherever the daemon can reach the registry, including inside the Jenkins
# container where an HTTPS tags/list call from the shell cannot).
# There is deliberately NO fallback to :latest: on 2026-11-25 arcana.boo had no 0.5.7 tag and
# :latest there was a 0.1.0 image, so a "PASS" came from the wrong rules. Neither pull works →
# exit 2: that is NOT a pass.
# Used by tools/arch-check.sh AND the Jenkinsfile, so local and CI scan with the same image.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/tools/arch-qube.version")"
VERSION="${VERSION#v}"

for cand in ${ARCH_QUBE_IMAGE:-} "ghcr.io/jrjohn/arch-qube:${VERSION}" "arcana.boo/arcana/arch-qube:${VERSION}"; do
    if docker image inspect "$cand" >/dev/null 2>&1 || docker pull -q "$cand" >/dev/null 2>&1; then
        echo "$cand"
        exit 0
    fi
done
echo "arch-qube: ${VERSION} not available from ghcr.io or arcana.boo — cannot check (this is NOT a pass)" >&2
exit 2
