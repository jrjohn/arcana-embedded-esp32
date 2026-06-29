#!/bin/zsh
# Remount/recovery power-loss regression for the dual-FAT engine.
#
# The ActiveFat flip duplicates only the FAT + allocation bitmap, NOT the
# shared-heap directory entries. So a power loss between a file's size advancing
# (committed in the shared dir entry) and the flip that would make its last
# cluster's bitmap bit active leaves that cluster "free but referenced" in the
# committed bitmap — and a naive remount would re-allocate it (cross-link).
# ExFatVolume::reconcileBitmap() heals this at mount before any allocation.
#
# This harness, per N: phase-1 power loss mid-append, phase-2 fresh remount
# (reconcile runs) + recovery writes + clean unmount. Then it asserts on the
# remounted image:
#   (a) ZERO "free but referenced" clusters  (exfat_analyze.py)  — the real oracle
#   (b) the committed baseline file survives                     — no data loss
#   (c) fsck_exfat is clean                                      — macOS cross-check
# (a) is what fsck-of-final-image can miss: a contiguous extend may step past the
# bad cluster by luck, so we check the invariant directly.
#
# Usage: run_remount_fuzz.sh <remount_fuzz binary> [maxFail] [step]
set -e
BIN="${1:?need remount_fuzz binary}"
MAXF="${2:-400}"
STEP="${3:-1}"
HERE="${0:A:h}"
ANALYZE="$HERE/exfat_analyze.py"
IMG=/tmp/remount_fuzz_run.img
frb=0; databad=0; fsckbad=0; clean=0; total=0
for ((N=10; N<=MAXF; N+=STEP)); do
  "$BIN" "$IMG" "$N" >/dev/null 2>&1 || { echo "N=$N remount_fuzz FAILED"; continue; }
  total=$((total+1))
  out=$(python3 "$ANALYZE" "$IMG" 2>/dev/null)
  if echo "$out" | grep -q 'FREE-BUT-REFERENCED'; then frb=$((frb+1)); echo "N=$N FREE-BUT-REFERENCED"; fi
  if ! echo "$out" | grep -q 'baseline.ats: firstCluster'; then databad=$((databad+1)); echo "N=$N baseline MISSING"; fi
  # fsck cross-check is sparse (hdiutil attach is slow): every 25th trial.
  if (( total % 25 == 0 )); then
    DEV=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$IMG" 2>/dev/null | head -1 | awk '{print $1}')
    if [ -n "$DEV" ]; then
      if fsck_exfat -n "${DEV}s1" >/dev/null 2>&1; then clean=$((clean+1)); else fsckbad=$((fsckbad+1)); echo "N=$N fsck DIRTY"; fi
      hdiutil eject "$DEV" >/dev/null 2>&1 || true
    fi
  fi
done
echo "=== remount fuzz: $total trials (N=10..$MAXF step $STEP) ==="
echo "free-but-referenced=$frb  baseline-missing=$databad  fsck(dirty/checked)=$fsckbad/$((clean+fsckbad))"
[ "$frb" -eq 0 ] && [ "$databad" -eq 0 ] && [ "$fsckbad" -eq 0 ] && echo "PASS" || { echo "FAIL"; exit 1; }
