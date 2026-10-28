"""Compile real fan, UART relay, servo and MPU6050 drivers against fake hardware."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PeripheralTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / "peripherals"
        include = ["tests/native/rtos/include", "tests/native/io/include", "tests/native/include", "tests/native/io",
                   "components/fan/include", "components/servo/include", "components/relay_uart/include",
                   "components/relay_uart", "components/peripheral_resources/include",
                   "components/board/include", "boards/XIAO_ESP32_S3_SENSE/include", "components/mpu6050/include"]
        sources = ["tests/native/rtos/rtos.c", "components/fan/fan.c", "components/servo/servo.c",
                   "components/relay_uart/relay_uart.c", "components/relay_uart/relay_uart_codec.c",
                   "components/peripheral_resources/peripheral_resources.c",
                   "components/mpu6050/mpu6050.c", "tests/native/io/fake_hardware.c", "tests/native/io/test_peripherals.c"]
        subprocess.run(["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
                        *[f"-I{ROOT / path}" for path in include],
                        *[str(ROOT / path) for path in sources], "-lm", "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True, capture_output=True, timeout=5)

    def test_fan_pwm_rpm_coasting_and_stale_samples(self):
        self.run_case("fan_speed")

    def test_fan_invalid_pins_and_pwm_only(self):
        self.run_case("fan_validation")

    def test_fan_failed_create_releases_resources(self):
        self.run_case("fan_cleanup")

    def test_fan_servo_uart_and_board_resource_conflicts(self):
        self.run_case("resource_conflicts")

    def test_modbus_known_crc_vector(self):
        self.run_case("modbus_vectors")

    def test_modbus_write_ack_and_readback(self):
        self.run_case("relay_modbus")

    def test_modbus_timeout_crc_address_and_exception_errors(self):
        self.run_case("relay_failures")

    def test_fixed_frames_copy_no_ack_and_exact_ack(self):
        self.run_case("relay_fixed")

    def test_uart_exclusivity_console_and_invalid_configuration(self):
        self.run_case("relay_validation")

    def test_mpu6050_initialization_and_ad0_address(self):
        self.run_case("mpu_initialization")

    def test_mpu6050_all_ranges_signed_samples_and_temperature(self):
        self.run_case("mpu_scales")

    def test_mpu6050_rejects_wrong_sensor_identity(self):
        self.run_case("mpu_identity")

    def test_mpu6050_each_initialization_failure_releases_resources(self):
        self.run_case("mpu_cleanup")

    def test_mpu6050_sleep_and_i2c_errors_preserve_output(self):
        self.run_case("mpu_sleep")

    def test_mpu6050_camera_bus_pins_and_cross_module_conflicts(self):
        self.run_case("mpu_resources")
