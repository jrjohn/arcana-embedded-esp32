#!/usr/bin/env python3
"""Flash a board without losing it: back up, write, then prove it booted healthy.

    tools/flash-safe.py -p /dev/ttyUSB0                  # build/ -> board
    tools/flash-safe.py -p COM5 --build-dir build-esp32s3 --min-internal-free 50000
    tools/flash-safe.py -p /dev/ttyUSB0 --dry-run        # print the plan, touch nothing
    tools/flash-safe.py --check-log boot.log             # judge a captured boot log offline

Steps (AGENTS.md §6, rule R12):
  1. Full-flash backup: esptool read-flash 0..<flash size> to flash-backups/<chip>-<UTC>.bin,
     size checked against the flash size, SHA-256 written next to it.
  2. Write the build exactly as idf.py would: esptool write-flash @flash_args (from build/).
  3. Open the serial port with DTR and RTS held LOW (pyserial's defaults toggle them and can
     drop an ESP32 into download mode or hold it in reset), listen to the boot log and FAIL
     unless the ready marker appears, a "MEM internal_free=<n>" line reports at least
     --min-internal-free bytes, and no panic / watchdog line shows up.
  4. Always print the rollback command for the backup.

Exit codes: 0 healthy, 1 board check failed (rollback printed), 2 usage / tool error.
Needs: esptool (python -m esptool) and pyserial — both ship in the ESP-IDF Python env.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, List, Optional

DEFAULT_READY = "All services running"
DEFAULT_MIN_FREE = 40960          # = CONFIG_ARCANA_INTERNAL_RAM_WARN_BYTES default
DEFAULT_LISTEN_S = 60
MEM_RE = re.compile(r"MEM internal_free=(\d+) largest=(\d+) psram_free=(\d+)")
FATAL_MARKERS = ("Guru Meditation", "LC_FREEZE", "abort() was called", "Task watchdog got triggered",
                 "Rebooting...", "rst:0x8 (TG1WDT", "Brownout detector was triggered")
SIZE_RE = re.compile(r"^(\d+)(MB|KB)$")


# ── pure helpers (unit-tested: tools/tests/test_flash_safe.py) ───────────────────────────
def flash_size_bytes(text: str) -> int:
    """'16MB' -> 16777216. 'detect'/'keep' are refused: a backup must know its size."""
    m = SIZE_RE.match(text.strip())
    if not m:
        raise ValueError(f"flash size '{text}' is not '<n>MB' — set it in sdkconfig, not 'detect'")
    n = int(m.group(1))
    return n * (1024 * 1024 if m.group(2) == "MB" else 1024)


@dataclass
class BuildInfo:
    build_dir: Path
    chip: str
    flash_size: str
    flash_args: Path


def read_build(build_dir: Path) -> BuildInfo:
    fa = build_dir / "flasher_args.json"
    args = build_dir / "flash_args"
    if not fa.is_file() or not args.is_file():
        raise FileNotFoundError(f"{fa} or {args} missing — run idf.py build first")
    d = json.loads(fa.read_text())
    chip = d.get("extra_esptool_args", {}).get("chip")
    size = d.get("flash_settings", {}).get("flash_size")
    if not chip or not size:
        raise ValueError(f"{fa} has no chip / flash_size")
    return BuildInfo(build_dir, chip, size, args)


@dataclass
class BootVerdict:
    ok: bool
    reason: str
    internal_free: Optional[int] = None
    largest: Optional[int] = None
    seen_ready: bool = False
    fatal: List[str] = field(default_factory=list)


def judge_boot_log(lines: Iterable[str], ready: str, min_free: int) -> BootVerdict:
    """Healthy = ready marker seen, MEM line seen with internal_free >= min_free, no fatal line."""
    v = BootVerdict(ok=False, reason="")
    for line in lines:
        if any(f in line for f in FATAL_MARKERS):
            v.fatal.append(line.strip())
        m = MEM_RE.search(line)
        if m:
            v.internal_free, v.largest = int(m.group(1)), int(m.group(2))
        if ready in line:
            v.seen_ready = True
    if v.fatal:
        v.reason = f"fatal line in boot log: {v.fatal[0]}"
    elif not v.seen_ready:
        v.reason = f"ready marker '{ready}' never appeared"
    elif v.internal_free is None:
        v.reason = "no 'MEM internal_free=' line (DiagnosticService::logBootMemory not called?)"
    elif v.internal_free < min_free:
        v.reason = f"internal_free={v.internal_free} < {min_free} — BLE/Wi-Fi/TLS will fail to allocate"
    else:
        v.ok = True
        v.reason = f"ready, internal_free={v.internal_free} >= {min_free}"
    return v


def esptool_v5() -> bool:
    """esptool v5 (ESP-IDF 6.x) renamed read_flash -> read-flash etc.; v4 only knows underscores.
    Unknown (esptool missing) -> assume v5, the version ESP-IDF 6.1 ships."""
    try:
        out = subprocess.run([sys.executable, "-m", "esptool", "version"], capture_output=True,
                             text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return True
    m = re.search(r"v?(\d+)\.\d+", out)
    return not m or int(m.group(1)) >= 5


def _n(name: str, v5: bool) -> str:
    return name.replace("_", "-") if v5 else name


def esptool_base(chip: str, port: str, baud: int) -> List[str]:
    return [sys.executable, "-m", "esptool", "--chip", chip, "-p", port, "-b", str(baud)]


def backup_cmd(info: BuildInfo, port: str, baud: int, out: Path, v5: bool = True) -> List[str]:
    return esptool_base(info.chip, port, baud) + [
        _n("read_flash", v5), "0", hex(flash_size_bytes(info.flash_size)), str(out)]


def write_cmd(info: BuildInfo, port: str, baud: int, v5: bool = True) -> List[str]:
    # @flash_args is resolved by esptool relative to cwd -> run from the build dir.
    return esptool_base(info.chip, port, baud) + [
        "--before", _n("default_reset", v5), "--after", _n("hard_reset", v5),
        _n("write_flash", v5), "@flash_args"]


def rollback_cmd(chip: str, port: str, baud: int, backup: Path, v5: bool = True) -> str:
    return " ".join(shlex.quote(a) for a in esptool_base(chip, port, baud) +
                    [_n("write_flash", v5), "0x0", str(backup)])


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ── side-effecting steps ──────────────────────────────────────────────────────────────────
def run(cmd: List[str], cwd: Optional[Path] = None) -> None:
    print("+", " ".join(shlex.quote(c) for c in cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def do_backup(info: BuildInfo, port: str, baud: int, out: Path, v5: bool) -> str:
    out.parent.mkdir(parents=True, exist_ok=True)
    run(backup_cmd(info, port, baud, out, v5))
    want = flash_size_bytes(info.flash_size)
    got = out.stat().st_size
    if got != want:
        raise RuntimeError(f"backup {out} is {got} B, expected {want} B — refusing to flash")
    digest = sha256_file(out)
    Path(str(out) + ".sha256").write_text(f"{digest}  {out.name}\n")
    print(f"backup OK: {out} ({got} B) sha256={digest}")
    return digest


def open_serial_quiet(port: str, baud: int):
    import serial  # pyserial, from the IDF python env

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.5
    # Set BEFORE open(): pyserial asserts DTR/RTS on open by default, which on the
    # auto-reset circuit means EN low / GPIO0 low = held in reset or download mode.
    s.dtr = False
    s.rts = False
    s.open()
    return s


def listen(port: str, baud: int, seconds: int, ready: str, min_free: int) -> BootVerdict:
    lines: List[str] = []
    ser = open_serial_quiet(port, baud)
    try:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip()
            print(f"  | {line}")
            lines.append(line)
            v = judge_boot_log(lines, ready, min_free)
            if v.fatal or (v.seen_ready and v.internal_free is not None):
                return v
    finally:
        ser.close()
    return judge_boot_log(lines, ready, min_free)


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("-p", "--port", help="serial port (required unless --check-log)")
    p.add_argument("--build-dir", default="build", type=Path)
    p.add_argument("-b", "--baud", type=int, default=460800, help="esptool baud")
    p.add_argument("--monitor-baud", type=int, default=115200)
    p.add_argument("--backup-dir", default="flash-backups", type=Path)
    p.add_argument("--ready-marker", default=DEFAULT_READY)
    p.add_argument("--min-internal-free", type=int, default=DEFAULT_MIN_FREE)
    p.add_argument("--listen", type=int, default=DEFAULT_LISTEN_S, help="seconds to wait for ready")
    p.add_argument("--dry-run", action="store_true", help="print the plan; no serial, no esptool")
    p.add_argument("--check-log", type=Path, help="judge a captured boot log and exit")
    a = p.parse_args(argv)
    if not a.check_log and not a.port:
        p.error("--port is required (or use --check-log)")
    if a.min_internal_free < 0 or a.listen <= 0:
        p.error("--min-internal-free must be >= 0 and --listen > 0")
    return a


def main(argv: Optional[List[str]] = None) -> int:
    a = parse_args(argv)
    if a.check_log:
        v = judge_boot_log(a.check_log.read_text(errors="replace").splitlines(),
                           a.ready_marker, a.min_internal_free)
        print(("flash-safe: PASS — " if v.ok else "flash-safe: FAIL — ") + v.reason)
        return 0 if v.ok else 1
    try:
        info = read_build(a.build_dir)
        flash_size_bytes(info.flash_size)
    except (OSError, ValueError) as e:
        print(f"flash-safe: {e}", file=sys.stderr)
        return 2

    stamp = _dt.datetime.now(_dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = (a.backup_dir / f"{info.chip}-{info.flash_size}-{stamp}.bin").resolve()
    v5 = esptool_v5()
    rollback = rollback_cmd(info.chip, a.port, a.baud, backup, v5)

    if a.dry_run:
        print("flash-safe: DRY RUN — nothing is opened or written")
        print("  serial  : open", a.port, "with DTR=low RTS=low")
        print("  backup  :", " ".join(shlex.quote(c) for c in backup_cmd(info, a.port, a.baud, backup, v5)))
        print("  write   : (cwd", info.build_dir, ")",
              " ".join(shlex.quote(c) for c in write_cmd(info, a.port, a.baud, v5)))
        print(f"  verify  : ready='{a.ready_marker}' internal_free>={a.min_internal_free} within {a.listen}s")
        print("  rollback:", rollback)
        return 0

    try:
        do_backup(info, a.port, a.baud, backup, v5)
        run(write_cmd(info, a.port, a.baud, v5), cwd=info.build_dir)
    except (subprocess.CalledProcessError, RuntimeError, OSError) as e:
        print(f"flash-safe: FAIL before boot check — {e}", file=sys.stderr)
        if backup.exists():
            print(f"rollback: {rollback}")
        return 1

    v = listen(a.port, a.monitor_baud, a.listen, a.ready_marker, a.min_internal_free)
    print(("flash-safe: PASS — " if v.ok else "flash-safe: FAIL — ") + v.reason)
    print(f"rollback: {rollback}")
    return 0 if v.ok else 1


if __name__ == "__main__":
    sys.exit(main())
