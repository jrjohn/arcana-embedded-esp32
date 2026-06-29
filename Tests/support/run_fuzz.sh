#!/bin/zsh
# Power-loss fuzz: for many "fail after N writes" points, generate a dual-FAT image
# truncated at that write, then cross-check with macOS fsck_exfat.
#
# NOTE: this checks the RAW on-disk state immediately after power loss, with NO
# remount. The ActiveFat flip duplicates only the FAT + bitmap, not the shared-heap
# directory entries, so at a few N a file's committed size can reference a cluster
# whose bitmap bit isn't active yet — fsck flags "bitmap needs repair" (the FILE
# DATA + FAT hierarchy are intact; only free-space accounting is off). That torn
# bitmap is HEALED at the next mount by ExFatVolume::reconcileBitmap() before any
# allocation, so it is not data loss. The authoritative power-fail oracle is
# run_remount_fuzz.sh (tests the actual device path: power loss → remount+heal →
# recovery writes → assert zero free-but-referenced + fsck clean). This script
# remains useful for confirming committed file data + FAT structure survive.
# Usage: run_fuzz.sh <format_fuzz binary> <maxFail> [step]
set -e
BIN="${1:?need format_fuzz binary}"
MAXF="${2:-400}"
STEP="${3:-7}"
IMG=/tmp/fuzz_run.img
clean=0; corrupt=0; total=0
for ((N=10; N<=MAXF; N+=STEP)); do
  "$BIN" "$IMG" "$N" >/dev/null 2>&1 || true
  DEV=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$IMG" 2>/dev/null | head -1 | awk '{print $1}')
  [ -z "$DEV" ] && { echo "N=$N attach-failed"; continue; }
  if fsck_exfat -n "${DEV}s1" >/dev/null 2>&1; then
    clean=$((clean+1))
  else
    corrupt=$((corrupt+1)); echo "N=$N CORRUPT"
  fi
  total=$((total+1))
  hdiutil eject "$DEV" >/dev/null 2>&1 || true
done
echo "=== fuzz result: $clean/$total clean, $corrupt corrupt ==="
