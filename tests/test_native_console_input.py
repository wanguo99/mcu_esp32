"""Console parsing tests are separate from the reusable servo driver tests."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]

class ConsoleInputTests(unittest.TestCase):
    def test_strict_numbers_and_hex_frames(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = pathlib.Path(temp) / "input"
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            f"-I{ROOT / 'tests/native/include'}", f"-I{ROOT / 'apps/component_demo/main'}",
                            str(ROOT / "apps/component_demo/main/console_input.c"),
                            str(ROOT / "tests/native/test_console_input.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)
