from __future__ import annotations

from array import array
import json
import os
from pathlib import Path
import stat
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch
import zlib

import kilix_state as ks


class StoreLifecycleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="kilix-state-py-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def store(self, **kwargs) -> ks.Store:
        return ks.Store(
            "test-app",
            "save.state",
            base_directory=self.root,
            **kwargs,
        )

    def test_crc_record_lifecycle_and_atomic_replace(self) -> None:
        store = self.store(max_payload=32)
        self.addCleanup(store.close)
        path = self.root / "test-app" / "save.state"

        self.assertEqual(store.path, path)
        self.assertEqual(store.directory, path.parent)
        self.assertEqual(store.format, ks.Format.CRC32)
        self.assertEqual(store.max_payload, 32)
        self.assertIn("format=CRC32", repr(store))
        self.assertEqual(stat.S_IMODE(path.parent.stat().st_mode), 0o700)
        with self.assertRaises(ks.StateNotFoundError) as caught:
            store.load()
        self.assertEqual(caught.exception.result, ks.Result.NOT_FOUND)
        self.assertEqual(caught.exception.operation, "load")

        payload = b"\0\x01hello\xff"
        store.save(payload)
        self.assertEqual(store.load(), payload)
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        expected = b"KST1" + struct.pack(
            "<III", 1, len(payload), zlib.crc32(payload)) + payload
        self.assertEqual(path.read_bytes(), expected)
        self.assertFalse(any(".tmp" in item.name for item in path.parent.iterdir()))

        replacement = b"new state"
        store.save(replacement)
        self.assertEqual(store.load(), replacement)
        with self.assertRaises(ks.StateTooLargeError):
            store.save(bytes(33))

        self.assertTrue(store.remove())
        self.assertFalse(path.exists())
        with self.assertRaises(ks.StateNotFoundError):
            store.remove()
        self.assertFalse(store.remove(missing_ok=True))

    def test_context_manager_empty_state_and_close_are_safe(self) -> None:
        with self.store() as store:
            store.save(b"")
            self.assertEqual(store.load(), b"")
            self.assertFalse(store.closed)
        self.assertTrue(store.closed)
        self.assertEqual(repr(store), "Store(closed=True)")
        store.close()
        with self.assertRaises(ks.StoreClosedError):
            store.load()
        with self.assertRaises(ks.StoreClosedError):
            _ = store.path
        with self.assertRaises(ks.StoreClosedError):
            store.save(b"late")

    def test_load_or_only_defaults_missing_state(self) -> None:
        with self.store() as store:
            fallback = array("I", [1, 2, 3])
            self.assertEqual(store.load_or(fallback), fallback.tobytes())
            store.save(b"present")
            self.assertEqual(store.load_or(b"fallback"), b"present")

            damaged = bytearray(store.path.read_bytes())
            damaged[-1] ^= 0xFF
            store.path.write_bytes(damaged)
            with self.assertRaises(ks.CorruptStateError):
                store.load_or(b"must not hide corruption")

    def test_buffer_protocol_and_contiguity(self) -> None:
        with self.store() as store:
            payload = array("I", [0x01020304, 0xA0B0C0D0])
            store.save(payload)
            self.assertEqual(store.load(), payload.tobytes())
            with self.assertRaises(BufferError):
                store.save(memoryview(b"abcdef")[::2])
            with self.assertRaises(TypeError):
                store.save("not bytes")

    def test_raw_format_is_byte_identical(self) -> None:
        path = self.root / "raw-app" / "settings.dat"
        payload = b"plain legacy state\0"
        with ks.Store(
            "raw-app",
            "settings.dat",
            base_directory=self.root,
            max_payload=64,
            format=ks.Format.RAW,
        ) as store:
            store.save(payload)
            self.assertEqual(store.load(), payload)
            self.assertEqual(path.read_bytes(), payload)

    def test_absolute_path_supports_hidden_legacy_filename(self) -> None:
        path = self.root / ".legacy-save"
        with ks.Store(
            app_id="../ignored-by-explicit-path",
            absolute_path=path,
            max_payload=8,
            format=ks.Format.RAW,
        ) as store:
            store.save(b"legacy")
            self.assertEqual(store.path, path)
            self.assertEqual(path.read_bytes(), b"legacy")

        with self.assertRaises(ks.InvalidStateError):
            ks.Store(absolute_path="relative/save")
        with self.assertRaises(ks.InvalidStateError):
            ks.Store("app", "state", base_directory="relative")

    def test_corrupt_and_oversized_external_records_are_rejected(self) -> None:
        with self.store(max_payload=8) as store:
            store.save(b"good")
            record = bytearray(store.path.read_bytes())
            record[-1] ^= 1
            store.path.write_bytes(record)
            with self.assertRaises(ks.CorruptStateError):
                store.load()

        with ks.Store(
            "raw-large",
            "state",
            base_directory=self.root,
            max_payload=4,
            format=ks.Format.RAW,
        ) as store:
            store.path.write_bytes(b"12345")
            with self.assertRaises(ks.StateTooLargeError):
                store.load()

    def test_same_instance_operations_are_thread_serialized(self) -> None:
        with self.store(max_payload=64) as store:
            payloads = [json.dumps({"worker": i}).encode() for i in range(6)]
            errors: list[BaseException] = []
            barrier = threading.Barrier(len(payloads))

            def worker(payload: bytes) -> None:
                try:
                    barrier.wait()
                    for _ in range(40):
                        store.save(payload)
                        self.assertIn(store.load(), payloads)
                except BaseException as error:
                    errors.append(error)

            threads = [
                threading.Thread(target=worker, args=(payload,))
                for payload in payloads
            ]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=10)
            self.assertFalse(any(thread.is_alive() for thread in threads))
            self.assertEqual(errors, [])
            self.assertIn(store.load(), payloads)


class PathSecurityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="kilix-state-py-security-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_symlink_app_directory_is_rejected(self) -> None:
        target = self.root / "target"
        target.mkdir()
        (self.root / "linked-app").symlink_to(target, target_is_directory=True)
        with self.assertRaises(ks.UnsafeStatePathError):
            ks.Store(
                "linked-app", "state", base_directory=self.root)

    def test_state_symlink_is_not_followed_and_save_replaces_link(self) -> None:
        target = self.root / "target-file"
        target.write_bytes(b"untouched")
        with ks.Store(
            "safe-app", "state", base_directory=self.root) as store:
            store.path.symlink_to(target)
            with self.assertRaises(ks.UnsafeStatePathError):
                store.load()
            store.save(b"replacement")
            self.assertFalse(store.path.is_symlink())
            self.assertEqual(store.load(), b"replacement")
            self.assertEqual(target.read_bytes(), b"untouched")

    def test_non_regular_state_is_rejected(self) -> None:
        with ks.Store(
            "directory-app", "state", base_directory=self.root) as store:
            store.path.mkdir()
            with self.assertRaises(ks.UnsafeStatePathError):
                store.load()

    def test_xdg_and_home_resolution_match_native_contract(self) -> None:
        xdg = self.root / "xdg-data"
        with patch.dict(os.environ, {"XDG_DATA_HOME": str(xdg)}):
            with ks.Store("xdg-app", "state") as store:
                self.assertEqual(store.path, xdg / "xdg-app" / "state")

        home = self.root / "home"
        home.mkdir()
        environment = {"HOME": str(home), "XDG_DATA_HOME": ""}
        with patch.dict(os.environ, environment):
            with ks.Store("home-app", "state") as store:
                self.assertEqual(
                    store.path,
                    home / ".local" / "share" / "home-app" / "state",
                )

        with patch.dict(
            os.environ,
            {"HOME": str(home), "XDG_DATA_HOME": "relative/path"},
        ):
            with self.assertRaises(ks.InvalidStateError):
                ks.Store("bad-xdg", "state")


class ValidationTests(unittest.TestCase):
    def test_invalid_components_limits_and_format(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            for app_id, filename in (
                ("../escape", "state"),
                ("valid", ".hidden"),
                ("valid", "../state"),
                ("snowman-☃", "state"),
            ):
                with self.subTest(app_id=app_id, filename=filename):
                    with self.assertRaises(ks.InvalidStateError):
                        ks.Store(
                            app_id,
                            filename,
                            base_directory=root,
                        )

        for maximum in (0, -1, ks.MAX_PAYLOAD + 1):
            with self.subTest(maximum=maximum):
                with self.assertRaises(ValueError):
                    ks.Store(
                        "app", "state", max_payload=maximum)
        with self.assertRaises(ValueError):
            ks.Store("app", "state", format=99)
        with self.assertRaises(ValueError):
            ks.Store("bad\0app", "state")


if __name__ == "__main__":
    unittest.main()
