"""Check original ESP32 pin reservations separately from the ESP32-S3 fixtures."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]


class ESP32ResourceTests(unittest.TestCase):
    def test_uart_flash_psram_and_shared_pwm_resources(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=pathlib.Path(folder)/"resources"
            include=["tests/native/rtos/include","tests/native/io/include","tests/native/include",
                     "components/peripheral_resources/include","components/board/include"]
            subprocess.run(["cc","-std=gnu11","-pthread","-Wall","-Wextra","-Werror",
                "-DCONFIG_IDF_TARGET_ESP32=1",*[f"-I{ROOT/p}" for p in include],
                str(ROOT/"components/peripheral_resources/peripheral_resources.c"),
                str(ROOT/"tests/native/io/test_resources_esp32.c"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)
