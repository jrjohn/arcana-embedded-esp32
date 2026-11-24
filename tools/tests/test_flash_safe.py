"""Unit tests for tools/flash-safe.py (no board: argument handling, dry run, log verdicts)."""
import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "flash_safe", Path(__file__).resolve().parents[1] / "flash-safe.py")
fs = importlib.util.module_from_spec(_SPEC)
sys.modules["flash_safe"] = fs
_SPEC.loader.exec_module(fs)

GOOD_LOG = """I (512) AppContainer: Services started
I (900) AppContainer: All services running
MEM internal_free=69083 largest=31744 psram_free=8191440
"""


def make_build(tmp: Path, size="16MB", chip="esp32s3") -> Path:
    b = tmp / "build"
    b.mkdir()
    (b / "flash_args").write_text("--flash_mode dio\n0x0 bootloader/bootloader.bin\n")
    (b / "flasher_args.json").write_text(json.dumps(
        {"extra_esptool_args": {"chip": chip}, "flash_settings": {"flash_size": size}}))
    return b


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            rc = fs.main(argv)
        except SystemExit as e:
            rc = e.code
    return rc, out.getvalue(), err.getvalue()


class FlashSize(unittest.TestCase):
    def test_parses(self):
        self.assertEqual(fs.flash_size_bytes("4MB"), 4 * 1024 * 1024)
        self.assertEqual(fs.flash_size_bytes("16MB"), 16 * 1024 * 1024)
        self.assertEqual(fs.flash_size_bytes("512KB"), 512 * 1024)

    def test_refuses_detect(self):
        for bad in ("detect", "keep", "", "16"):
            with self.assertRaises(ValueError):
                fs.flash_size_bytes(bad)


class BootVerdicts(unittest.TestCase):
    def test_healthy(self):
        v = fs.judge_boot_log(GOOD_LOG.splitlines(), fs.DEFAULT_READY, 40960)
        self.assertTrue(v.ok, v.reason)
        self.assertEqual(v.internal_free, 69083)

    def test_low_ram_fails(self):
        # iNAPLink 0.3.0: booted, but 6.9 KB internal RAM -> BLE/Wi-Fi dead.
        log = GOOD_LOG.replace("internal_free=69083", "internal_free=6900")
        v = fs.judge_boot_log(log.splitlines(), fs.DEFAULT_READY, 40960)
        self.assertFalse(v.ok)
        self.assertIn("6900", v.reason)

    def test_missing_ready(self):
        v = fs.judge_boot_log(["MEM internal_free=90000 largest=1 psram_free=0"], fs.DEFAULT_READY, 1)
        self.assertFalse(v.ok)
        self.assertIn("ready marker", v.reason)

    def test_missing_mem_line(self):
        v = fs.judge_boot_log(["All services running"], fs.DEFAULT_READY, 1)
        self.assertFalse(v.ok)
        self.assertIn("MEM", v.reason)

    def test_panic_beats_everything(self):
        log = GOOD_LOG + "Guru Meditation Error: Core  0 panic'ed (LoadProhibited)\n"
        v = fs.judge_boot_log(log.splitlines(), fs.DEFAULT_READY, 1)
        self.assertFalse(v.ok)
        self.assertIn("Guru", v.reason)

    def test_freeze_line_fails(self):
        log = "LC_FREEZE reset=task_wdt stage=2 uptime=12\n" + GOOD_LOG
        self.assertFalse(fs.judge_boot_log(log.splitlines(), fs.DEFAULT_READY, 1).ok)

    def test_custom_marker(self):
        v = fs.judge_boot_log(["APP READY", "MEM internal_free=50000 largest=20000 psram_free=0"],
                              "APP READY", 40960)
        self.assertTrue(v.ok)


class Commands(unittest.TestCase):
    def setUp(self):
        self.info = fs.BuildInfo(Path("b"), "esp32s3", "16MB", Path("b/flash_args"))

    def test_backup_reads_whole_flash(self):
        cmd = fs.backup_cmd(self.info, "/dev/ttyUSB0", 460800, Path("x.bin"))
        self.assertEqual(cmd[-4:], ["read-flash", "0", "0x1000000", "x.bin"])
        v4 = fs.backup_cmd(self.info, "/dev/ttyUSB0", 460800, Path("x.bin"), v5=False)
        self.assertEqual(v4[-4], "read_flash")
        self.assertIn("esp32s3", cmd)

    def test_write_uses_build_flash_args(self):
        cmd = fs.write_cmd(self.info, "COM5", 921600)
        self.assertEqual(cmd[-2:], ["write-flash", "@flash_args"])
        self.assertIn("hard-reset", cmd)
        self.assertIn("write_flash", fs.write_cmd(self.info, "COM5", 921600, v5=False))

    def test_rollback_writes_backup_at_zero(self):
        r = fs.rollback_cmd("esp32", "/dev/ttyUSB0", 460800, Path("/tmp/a b.bin"))
        self.assertIn("write-flash 0x0 '/tmp/a b.bin'", r)


class Cli(unittest.TestCase):
    def test_port_required(self):
        rc, _, err = run_main([])
        self.assertEqual(rc, 2)
        self.assertIn("--port", err)

    def test_bad_listen(self):
        rc, _, _ = run_main(["-p", "x", "--listen", "0"])
        self.assertEqual(rc, 2)

    def test_missing_build_is_usage_error(self):
        with tempfile.TemporaryDirectory() as t:
            rc, _, err = run_main(["-p", "x", "--build-dir", str(Path(t) / "nope")])
            self.assertEqual(rc, 2)
            self.assertIn("build first", err)

    def test_detect_flash_size_refused(self):
        with tempfile.TemporaryDirectory() as t:
            b = make_build(Path(t), size="detect")
            rc, _, err = run_main(["-p", "x", "--build-dir", str(b), "--dry-run"])
            self.assertEqual(rc, 2)
            self.assertIn("detect", err)

    def test_dry_run_prints_plan_and_touches_nothing(self):
        with tempfile.TemporaryDirectory() as t:
            b = make_build(Path(t))
            bk = Path(t) / "bk"
            rc, out, _ = run_main(["-p", "/dev/ttyUSB0", "--build-dir", str(b),
                                   "--backup-dir", str(bk), "--dry-run"])
            self.assertEqual(rc, 0)
            for word in ("DRY RUN", "DTR=low RTS=low", "0 0x1000000",
                         "flash @flash_args", "internal_free>=40960", "rollback:"):
                self.assertIn(word, out)
            self.assertFalse(bk.exists())

    def test_check_log_pass_and_fail(self):
        with tempfile.TemporaryDirectory() as t:
            good = Path(t) / "good.log"
            good.write_text(GOOD_LOG)
            self.assertEqual(run_main(["--check-log", str(good)])[0], 0)
            self.assertEqual(run_main(["--check-log", str(good), "--min-internal-free", "70000"])[0], 1)


if __name__ == "__main__":
    unittest.main()
