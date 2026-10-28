"""Exercise the real servo worker with POSIX threads and a monotonic clock."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class ServoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / "servo"
        include = ["tests/native/rtos/include", "tests/native/io/include", "tests/native/include",
                   "components/servo/include", "components/peripheral_resources/include", "components/board/include",
                   "boards/XIAO_ESP32_S3_SENSE/include"]
        sources = ["tests/native/rtos/rtos.c", "components/servo/servo.c",
                   "components/peripheral_resources/peripheral_resources.c",
                   "tests/native/servo/test_servo.c"]
        subprocess.run(["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
                        *[f"-I{ROOT / p}" for p in include], *[str(ROOT / p) for p in sources],
                        "-lm", "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True, capture_output=True, timeout=5)

    def test_calibration_initial_state_and_resource_release(self): self.run_case("basics")
    def test_motion_completion_limits_and_cancel(self): self.run_case("motion")
    def test_copied_sequence_repeat_sweep_and_worker_shutdown(self): self.run_case("sequence")
    def test_task_and_pwm_failure_recovery(self): self.run_case("failures")
    def test_relative_motion_home_record_and_reset(self): self.run_case("relative_home")
    def test_runtime_limits_cover_all_motion_entry_points(self): self.run_case("limits")
    def test_no_fade_service_and_pwm_update_failure_recovery(self): self.run_case("pwm_recovery")
    def test_pad_setup_and_failed_initialization_release(self): self.run_case("pad_setup")
    def test_reversed_angle_pulse_relative_motion_home_and_limits(self): self.run_case("reversed_direction")
    def test_half_second_reversed_motion_has_progressive_pwm_and_bounded_duration(self): self.run_case("half_second_motion")
    def test_automatic_release_resume_home_and_retrigger(self): self.run_case("auto_release")
    def test_automatic_release_task_failure_keeps_pwm_disabled(self): self.run_case("release_task_failure")
