"""Test portable satellite search and energy accounting with deterministic samples."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
class SatelliteAlgorithms(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = pathlib.Path(cls.temp.name) / "satellite"
        include = ["tests/native/include", "components/angle_search/include", "components/power_monitor/include"]
        sources = ["components/angle_search/angle_search.c", "components/power_monitor/power_monitor.c", "tests/native/satellite/test_algorithms.c"]
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", *[f"-I{ROOT / p}" for p in include],
                        *[str(ROOT / p) for p in sources], "-lm", "-o", str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=5)
    def test_coarse_fine_search_converges(self): self.case("converge")
    def test_invalid_stale_duplicate_and_timeout(self): self.case("validity")
    def test_weak_light_stops_search(self): self.case("weak")
    def test_energy_integration_missing_samples_and_hysteresis(self): self.case("power")

class SatelliteApplication(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import re
        cls.temp = tempfile.TemporaryDirectory()
        folder = pathlib.Path(cls.temp.name)
        cls.binary = folder / "app"
        kconfig = (ROOT / "apps/satellite/main/Kconfig.projbuild").read_text()
        values = {}
        for name, block in re.findall(r'^config (SAT_\w+)\n(.*?)(?=^config |^endmenu|\Z)', kconfig, re.M | re.S):
            default = re.search(r'^\s+default (-?\d+|y|n)\s*$', block, re.M)
            if default:
                value = default.group(1)
                values[name] = {"y": "1", "n": "0"}.get(value, value)
        values.update(SAT_AXIS0_GPIO="15", SAT_AXIS1_GPIO="16", SAT_AXIS2_GPIO="17",
                      SAT_MON0_V_GPIO="1", SAT_MON1_V_GPIO="2", SAT_MON2_V_GPIO="3",
                      SAT_MON0_I_GPIO="4", SAT_MON1_I_GPIO="5")
        (folder / "sdkconfig.h").write_text("#pragma once\n" + "".join(f"#define CONFIG_{k} {v}\n" for k, v in values.items()) +
            "#define CONFIG_MCU_ENABLE_SERVO 1\n#define CONFIG_MCU_ENABLE_VOLTAGE_SENSOR 1\n#define CONFIG_MCU_ENABLE_NETWORK 1\n")
        include = ["tests/native/rtos/include", "tests/native/io/include", "tests/native/include", "apps/satellite/main",
                   "components/angle_search/include", "components/power_monitor/include", "components/servo/include",
                   "components/voltage_sensor/include", "components/network/include", "components/runtime/include", "components/app_config/include"]
        sources = ["tests/native/rtos/rtos.c", "components/angle_search/angle_search.c", "components/power_monitor/power_monitor.c", "tests/native/satellite/test_app.c"]
        subprocess.run(["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror", f"-I{folder}",
                        *[f"-I{ROOT / p}" for p in include], *[str(ROOT / p) for p in sources], "-lm", "-o", str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=5)
    def test_manual_ownership_and_home_capture(self): self.case("ownership")
    def test_low_battery_stale_sensor_and_recovery(self): self.case("policy")
    def test_ap_identity_change_and_standby(self): self.case("standby")

    def test_single_scan_trace_and_voltage_only_rejection(self): self.case("once")
    def test_unified_angle_initial_release_explicit_stop_and_error_recovery(self): self.case("angle")
