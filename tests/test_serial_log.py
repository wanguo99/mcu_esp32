"""Exercise serial file logging over pseudo terminals, without an MCU."""
import os
from pathlib import Path
import pty
import re
import subprocess
import sys
import tempfile
import time
import unittest

try:
    import serial
except ImportError:
    serial = None

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(serial, "Run with ESP-IDF Python to provide pyserial")
class SerialLogTests(unittest.TestCase):
    def wait_for(self, log, text, count=1):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if log.exists() and log.read_text().count(text) >= count:
                return
            time.sleep(0.03)
        self.fail(f"Missing {text!r} in {log.read_text() if log.exists() else 'no log'}")

    def test_timestamp_append_and_reconnect(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            log = folder / "serial.log"
            device = folder / "device"
            master, slave = pty.openpty()
            device.symlink_to(os.ttyname(slave))
            process = None
            try:
                def start():
                    return subprocess.Popen([sys.executable, str(ROOT / "tools/serial_log.py"),
                        "--port", str(device), "--output", str(log)], stdout=subprocess.DEVNULL,
                        stderr=subprocess.PIPE, text=True)

                process = start()
                self.wait_for(log, "HOST connected")
                os.write(master, "\x1b[31mE PWM 设置失败\x1b[0m\r\n".encode())
                self.wait_for(log, "E PWM 设置失败")
                self.assertNotIn("\x1b", log.read_text())
                self.assertRegex(log.read_text(), r"\[\d{4}-\d{2}-\d{2}T.*\] E PWM 设置失败")
                os.close(master)
                os.close(slave)
                master, slave = pty.openpty()
                device.unlink()
                device.symlink_to(os.ttyname(slave))
                self.wait_for(log, "HOST connected", 2)
                os.write(master, b"I reconnected\n")
                self.wait_for(log, "I reconnected")
                process.terminate()
                _, errors = process.communicate(timeout=5)
                self.assertEqual(process.returncode, 0, errors)
                self.assertFalse(log.with_suffix(".pid").exists())
                process = start()
                self.wait_for(log, "HOST logger started", 2)
                self.wait_for(log, "HOST connected", 3)
                os.write(master, b"I appended\n")
                self.wait_for(log, "I appended")
                self.assertIn("E PWM 设置失败", log.read_text())
            finally:
                if process and process.poll() is None:
                    process.terminate()
                    _, errors = process.communicate(timeout=5)
                    self.assertEqual(process.returncode, 0, errors)
                os.close(master)
                os.close(slave)
