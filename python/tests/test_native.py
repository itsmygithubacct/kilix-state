from __future__ import annotations

import ctypes.util
import os
import unittest
from unittest.mock import patch
import zlib

import kilix_state as ks


class NativeLibraryTests(unittest.TestCase):
    def test_default_library_exposes_expected_abi(self) -> None:
        library = ks.default_library()
        self.assertEqual(library.abi_version, (0, 5))
        self.assertEqual(ks.KILIX_STATE_ABI, (0, 5))
        self.assertIn("kilix-state", library.path)
        self.assertIn("abi=(0, 5)", repr(library))
        self.assertTrue(callable(library.raw.kilixstate_store_init))

    def test_native_abi_and_store_size_are_verified_on_load(self) -> None:
        library = ks.default_library()
        self.assertEqual(library.native_abi[0], 0)
        self.assertGreaterEqual(library.native_abi, (0, 5))
        self.assertEqual(
            library.store_size, ctypes.sizeof(ks._native._Store))

    def test_store_size_mismatch_is_hard_refused(self) -> None:
        path = ks.default_library().path
        with patch.object(ks._native, "_STORE_SIZE", 1):
            with self.assertRaises(ks.IncompatibleLibraryError) as caught:
                ks.KilixStateLibrary(path)
        self.assertIn("refusing to bind", str(caught.exception))

    def test_unsupported_abi_version_is_hard_refused(self) -> None:
        path = ks.default_library().path
        with patch.object(ks._native, "MINIMUM_ABI_MINOR", 0xFFFF):
            with self.assertRaises(ks.IncompatibleLibraryError):
                ks.KilixStateLibrary(path)
        with patch.object(ks._native, "SUPPORTED_ABI_MAJOR", 999):
            with self.assertRaises(ks.IncompatibleLibraryError) as caught:
                ks.KilixStateLibrary(path)
        self.assertIn("reports kilix-state ABI", str(caught.exception))

    def test_explicit_missing_library_has_actionable_error(self) -> None:
        missing = "/definitely/not/a/library/libkilix-state.so"
        with self.assertRaises(ks.LibraryNotFoundError) as caught:
            ks.KilixStateLibrary(missing)
        self.assertIn(missing, str(caught.exception))
        self.assertIn("KILIX_STATE_LIBRARY", str(caught.exception))

    def test_environment_override_is_authoritative(self) -> None:
        missing = "/missing/from/environment/libkilix-state.so"
        with patch.dict(os.environ, {"KILIX_STATE_LIBRARY": missing}):
            with self.assertRaises(ks.LibraryNotFoundError) as caught:
                ks.KilixStateLibrary()
        self.assertIn(missing, str(caught.exception))

    def test_library_without_required_symbols_is_rejected(self) -> None:
        libc = ctypes.util.find_library("c")
        if not libc:
            self.skipTest("platform C library was not discoverable")
        with self.assertRaises(ks.IncompatibleLibraryError) as caught:
            ks.KilixStateLibrary(libc)
        self.assertIn("kilixstate_options_init", str(caught.exception))

    def test_native_crc_and_result_names_are_public(self) -> None:
        self.assertEqual(ks.crc32(b"123456789"), 0xCBF43926)
        self.assertEqual(ks.crc32(b""), 0)
        for payload in (
            bytes(range(256)),
            bytearray(bytes(range(251)) * 257),
            memoryview(bytes(range(127)) * 1024),
        ):
            with self.subTest(size=len(payload)):
                self.assertEqual(ks.crc32(payload), zlib.crc32(payload))
        self.assertEqual(ks.result_name(ks.Result.CORRUPT), "corrupt state")
        self.assertEqual(ks.result_name(999), "unknown state result")


if __name__ == "__main__":
    unittest.main()
