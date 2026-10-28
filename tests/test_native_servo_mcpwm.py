"""Exercise original ESP32 MCPWM servo ownership, rollback and real motion worker."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class MCPWMServoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / "servo_mcpwm"
        include = ["tests/native/rtos/include", "tests/native/io/include", "tests/native/include",
                   "components/servo/include", "components/peripheral_resources/include",
                   "components/board/include", "boards/ESP32_DEVKIT/include"]
        sources = ["tests/native/rtos/rtos.c", "components/servo/servo.c",
                   "components/peripheral_resources/peripheral_resources.c", "tests/native/servo/test_servo_mcpwm.c"]
        subprocess.run(["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
                        "-DCONFIG_IDF_TARGET_ESP32=1", *[f"-I{ROOT / p}" for p in include],
                        *[str(ROOT / p) for p in sources], "-lm", "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def case(self, name):
        subprocess.run([str(self.binary), name], check=True, capture_output=True, timeout=6)

    def test_start_stop_reverse_relative_and_home(self): self.case("basics")
    def test_each_initialization_failure_releases_resources(self): self.case("failures")
    def test_both_groups_and_independent_ledc_resources(self): self.case("resources")
    def test_500ms_motion_updates_without_pwm_cycle_wait(self): self.case("motion")
    def test_second_move_after_idle_matches_first_without_restarting_pwm(self): self.case("repeat")
    def test_automatic_release_resume_home_and_retrigger(self): self.case("auto_release")
    def test_two_servos_release_independently_and_force_signals_low(self): self.case("independent_release")
