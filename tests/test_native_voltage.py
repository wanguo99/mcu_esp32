"""Test ADC driver sharing, calibrated divider math and cleanup/failure paths."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
class VoltageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / "adc"
        include = ["tests/native/adc/include", "tests/native/rtos/include", "tests/native/io/include", "tests/native/include",
                   "components/voltage_sensor/include", "components/peripheral_resources/include", "components/board/include",
                   "boards/XIAO_ESP32_S3_SENSE/include"]
        sources = ["tests/native/rtos/rtos.c", "components/voltage_sensor/voltage_sensor.c",
                   "components/peripheral_resources/peripheral_resources.c", "tests/native/adc/test_voltage.c"]
        subprocess.run(["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror", *(["-DADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED=0"] if getattr(cls,"line_fitting",False) else []), *[f"-I{ROOT / p}" for p in include],
                        *[str(ROOT / p) for p in sources], "-lm", "-o", str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def test_shared_adc_unit_and_independent_channels(self): subprocess.run([str(self.binary),"sharing"], check=True, timeout=5)
    def test_calibration_failure_saturation_and_retry(self): subprocess.run([str(self.binary),"faults"], check=True, timeout=5)

class VoltageLineFittingTests(VoltageTests):
    line_fitting = True
