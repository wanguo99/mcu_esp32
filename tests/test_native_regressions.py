"""Exercise production camera/file code on the host, without an ESP32 or SD card."""
import ctypes
import errno
import pathlib
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
NATIVE = ROOT / "tests/native"


def compile_library(output, sources, includes=(), flags=()):
    subprocess.run([
        "cc", "-std=gnu11", "-shared", "-fPIC", "-pthread",
        "-Werror=implicit-function-declaration",
        *[f"-I{directory}" for directory in includes],
        *map(str, sources), *flags, "-o", str(output),
    ], check=True)
    return ctypes.CDLL(str(output), use_errno=True)


def wait_for(predicate):
    deadline = time.monotonic() + 3
    while not predicate():
        if time.monotonic() > deadline:
            raise AssertionError("Timed out waiting for worker")
        time.sleep(0.002)


class CameraConcurrencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.lib = compile_library(pathlib.Path(cls.temp.name) / "camera.so", [
            ROOT / "components/camera/camera_service.c", NATIVE / "camera_driver.c",
        ], [NATIVE / "include", ROOT / "components/camera/include",
            ROOT / "components/board/include", ROOT / "components/app_config/include",
            ROOT / "boards/XIAO_ESP32_S3_SENSE/include"])
        cls.lib.camera_service_get_frame.restype = ctypes.c_void_p
        cls.lib.camera_service_return_frame.argtypes = [ctypes.c_void_p]
        cls.lib.camera_service_set_enabled.argtypes = [ctypes.c_bool]
        for name in ("is_ready", "claim_stream", "stream_stop_requested"):
            getattr(cls.lib, "camera_service_" + name).restype = ctypes.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.frames, self.jobs = [], []
        self.assertEqual(self.lib.camera_service_init(), 0)
        self.assertEqual(self.lib.camera_service_set_enabled(True), 0)

    def tearDown(self):
        self.lib.camera_service_end_recording()
        self.lib.camera_service_release_stream()
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            for frame in self.frames[:]:
                self.give(frame)
            for job, _ in self.jobs:
                job.join(0.02)
            if not self.frames and all(not job.is_alive() for job, _ in self.jobs):
                break
        self.assertTrue(all(not job.is_alive() for job, _ in self.jobs))
        self.assertEqual(self.lib.camera_service_set_enabled(False), 0)
        self.assertEqual(self.lib.test_camera_bad_deinit(), 0)

    def get(self):
        frame = self.lib.camera_service_get_frame()
        if frame:
            self.frames.append(frame)
        return frame

    def give(self, frame):
        self.lib.camera_service_return_frame(frame)
        self.frames.remove(frame)

    def worker(self, function):
        result = {}
        job = threading.Thread(target=lambda: result.update(value=function()), daemon=True)
        self.jobs.append((job, result))
        job.start()
        return job, result

    def test_return_frame_unblocks_third_getter_when_both_buffers_are_borrowed(self):
        first, second = self.get(), self.get()
        self.assertTrue(first and second)
        job, result = self.worker(self.get)
        wait_for(lambda: self.lib.test_camera_waiters() > 0)
        self.give(first)
        job.join(2)
        self.assertFalse(job.is_alive())
        self.assertTrue(result["value"], "Returning a buffer must wake the pending getter")

    def test_shutdown_drains_pending_getter_and_its_borrowed_frame(self):
        first, second = self.get(), self.get()
        getter, result = self.worker(self.get)
        wait_for(lambda: self.lib.test_camera_waiters() > 0)
        closer, closed = self.worker(lambda: self.lib.camera_service_set_enabled(False))
        wait_for(lambda: not self.lib.camera_service_is_ready())
        self.give(first)
        getter.join(2)
        self.assertTrue(result["value"])
        self.give(second)
        time.sleep(0.06)
        self.assertTrue(closer.is_alive(), "Driver must survive until the final borrower returns")
        self.assertEqual(self.lib.test_camera_initialized(), 1)
        self.give(result["value"])
        closer.join(2)
        self.assertEqual(closed["value"], 0)

    def test_failed_getter_releases_its_driver_reservation(self):
        self.lib.test_camera_fail_next()
        self.assertFalse(self.get())
        self.assertEqual(self.lib.camera_service_set_enabled(False), 0)

    def test_resolution_change_drains_frames_and_rejects_new_capture_and_recording(self):
        frame = self.get()
        self.assertTrue(self.lib.camera_service_claim_stream())
        changer, changed = self.worker(lambda: self.lib.camera_service_set_framesize(12))
        wait_for(self.lib.camera_service_stream_stop_requested)
        self.assertFalse(self.get(), "New getters must not race with sensor reconfiguration")
        self.assertNotEqual(self.lib.camera_service_begin_recording(), 0)
        self.lib.camera_service_release_stream()
        self.give(frame)
        changer.join(2)
        self.assertEqual(changed["value"], 0)
        self.assertEqual(self.lib.camera_service_get_framesize(), 12)
        self.assertTrue(self.get())


class PhotoFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.lib = compile_library(pathlib.Path(cls.temp.name) / "photo.so", [
            ROOT / "components/storage/photo_file.c", NATIVE / "photo_io.c",
        ], flags=["-Wl,--wrap=open"])
        cls.lib.photo_file_create.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32),
                                             ctypes.c_char_p, ctypes.c_size_t]
        cls.lib.photo_file_create.restype = ctypes.c_void_p
        cls.libc = ctypes.CDLL(None)
        cls.libc.fclose.argtypes = [ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.lib.test_photo_open_error(0)

    def create(self, start=1, size=1024):
        sequence = ctypes.c_uint32(start)
        output = ctypes.create_string_buffer(size)
        stream = self.lib.photo_file_create(self.directory.name.encode(), ctypes.byref(sequence), output, size)
        error = ctypes.get_errno()
        if stream:
            self.addCleanup(self.libc.fclose, stream)
        return stream, sequence.value, output.value.decode(), error

    def test_existing_photo_is_not_overwritten(self):
        existing = pathlib.Path(self.directory.name) / "photo_00001.jpg"
        existing.write_bytes(b"keep existing photo")
        stream, sequence, name, _ = self.create()
        self.assertTrue(stream)
        self.assertEqual(sequence, 2)
        self.assertTrue(name.endswith("photo_00002.jpg"))
        self.assertEqual(existing.read_bytes(), b"keep existing photo")

    def test_full_card_io_and_permission_errors_are_not_retried(self):
        for error in (errno.ENOSPC, errno.EIO, errno.EACCES, errno.EROFS):
            with self.subTest(error=error):
                self.lib.test_photo_open_error(error)
                stream, _, _, reported = self.create()
                self.assertFalse(stream)
                self.assertEqual(reported, error)
                self.assertEqual(self.lib.test_photo_open_calls(), 1)

    def test_collision_retries_are_bounded(self):
        for sequence in range(1, 34):
            (pathlib.Path(self.directory.name) / f"photo_{sequence:05d}.jpg").write_bytes(b"keep")
        stream, _, _, error = self.create()
        self.assertFalse(stream)
        self.assertEqual(error, errno.EEXIST)
        self.assertEqual(self.lib.test_photo_open_calls(), 32)
        self.assertTrue(all(p.read_bytes() == b"keep" for p in pathlib.Path(self.directory.name).iterdir()))

    def test_sequence_does_not_wrap_and_overwrite_an_earlier_photo(self):
        (pathlib.Path(self.directory.name) / "photo_4294967295.jpg").write_bytes(b"keep")
        stream, sequence, _, error = self.create(2**32-1)
        self.assertFalse(stream)
        self.assertEqual(sequence, 2**32-1)
        self.assertEqual(error, errno.EOVERFLOW)
        self.assertEqual(self.lib.test_photo_open_calls(), 1)

    def test_short_output_path_does_not_create_a_truncated_filename(self):
        stream, _, _, error = self.create(size=4)
        self.assertFalse(stream)
        self.assertEqual(error, errno.ENAMETOOLONG)
        self.assertEqual(self.lib.test_photo_open_calls(), 0)
        self.assertEqual(list(pathlib.Path(self.directory.name).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
