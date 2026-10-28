"""Verify application shutdown hook ordering, retries and registration bounds."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]

class RuntimeTests(unittest.TestCase):
    def test_shutdown_hooks(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = pathlib.Path(temp) / "runtime"
            include = ["tests/native/runtime/include", "tests/native/rtos/include", "tests/native/include",
                       "components/runtime/include", "components/board/include", "components/settings/include",
                       "boards/XIAO_ESP32_S3_SENSE/include"]
            subprocess.run(["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
                            *[f"-I{ROOT / p}" for p in include], str(ROOT / "components/runtime/runtime.c"),
                            str(ROOT / "tests/native/runtime/test_runtime.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)
