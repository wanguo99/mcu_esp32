"""Run real WS2812 effects and worker using pthread notifications and fake RMT."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class Ws2812Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / "ws2812"
        includes = ["tests/native/rtos/include", "tests/native/ws2812/include", "tests/native/io/include", "tests/native/include",
                    "tests/native/ws2812", "components/ws2812/include", "components/ws2812",
                    "components/peripheral_resources/include", "components/board/include",
                    "boards/XIAO_ESP32_S3_SENSE/include"]
        sources = ["tests/native/rtos/rtos.c", "components/ws2812/ws2812.c", "components/ws2812/ws2812_effects.c",
                   "components/peripheral_resources/peripheral_resources.c",
                   "tests/native/ws2812/fake_hardware.c", "tests/native/ws2812/test_ws2812.c"]
        subprocess.run(["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
                        *[f"-I{ROOT / path}" for path in includes],
                        *[str(ROOT / path) for path in sources], "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True, capture_output=True, timeout=5)

    def test_color_brightness_breath_chase_reverse_and_blink(self):
        self.run_case("effects")

    def test_worker_animates_then_stops_and_destroys(self):
        self.run_case("worker")

    def test_failed_create_and_retryable_teardown_release_resources(self):
        self.run_case("failures")

    def test_invalid_effects_and_gpio_conflicts(self):
        self.run_case("validation")
