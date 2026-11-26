import hashlib
import hmac
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib


def script(name):
    path = Path(__file__).resolve().parents[1] / "scripts" / (name + ".py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ota = script("radar_ota")
prep = script("prepare_radar_ota")


class RadarOtaTest(unittest.TestCase):
    def test_manifest_canonical_and_authenticated(self):
        image = bytearray(500)
        image[:4] = b"\xe9\x06\x02\x4f"
        image[12], image[23] = 9, 1
        struct.pack_into("<16sI", image, ota.IDENTITY_OFFSET, ota.FAMILY, 2)
        key = bytes(range(16))
        document = ota.release(image, key)
        manifest = f"M3F1:esp32s3:2:500:{hashlib.sha256(image).hexdigest()}"
        self.assertEqual(document["manifest"], manifest)
        self.assertEqual(document["signature"], hmac.digest(key, manifest.encode(), "sha256").hex())
        self.assertNotEqual(document, ota.release(image, bytes(16)))
        image[-1] = 1
        self.assertNotEqual(document, ota.release(image, key))
        for offset in (0, 2, 3, 12, 23, ota.IDENTITY_OFFSET):
            bad = bytearray(image)
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                ota.release(bad, key)
        with self.assertRaises(ValueError):
            ota.release(image, b"wrong")

    def test_verified_pair(self):
        with tempfile.TemporaryDirectory() as folder:
            first, second = (Path(folder) / name for name in ("first", "second"))
            first.write_bytes(b"abc")
            second.write_bytes(b"abd")
            with patch.object(prep, "FLASH", 3):
                with self.assertRaises(ValueError):
                    prep.verified_pair(first, second)
                second.write_bytes(b"abc")
                self.assertEqual(prep.verified_pair(first, second), b"abc")
            with self.assertRaises(ValueError):
                prep.verified_pair(first, second)

    def test_recovery_preserves_complete_slots_and_selectors(self):
        def record(target, offset, sequence):
            seq = struct.pack("<I", sequence)
            struct.pack_into("<I20xII", target, offset, sequence, 2, zlib.crc32(seq, 0xFFFFFFFF))

        original = bytearray(prep.FLASH)
        record(original, 0xE000, 3)
        record(original, 0xF000, 2)
        current = bytearray(original)
        record(current, 0xF000, 4)
        current[prep.OTA1] = 0xE9
        with (patch.object(prep, "ORIGINAL", prep.sha(original)),
              patch.object(prep, "LOADER", prep.sha(current[:0x8000])),
              patch.object(prep, "TABLE", prep.sha(current[0x8000:0x8C00]))):
            files = prep.recovery_files(original, current)
            self.assertEqual(files["original-0x10000.bin"], original[prep.OTA0:prep.OTA1])
            self.assertEqual(files["baseline-0x310000.bin"], current[prep.OTA1:prep.OTA1 + prep.SLOT])
            self.assertEqual(files["original-0xe000.bin"], original[0xE000:0x10000])
            self.assertEqual(files["baseline-0xe000.bin"], current[0xE000:0x10000])
            self.assertEqual(len(files["release-0x30f000.bin"]), prep.SECTOR)
            self.assertTrue(files["release-0x30f000.bin"].startswith(prep.PERMIT))
            current[prep.OTA0] ^= 1
            with self.assertRaises(ValueError):
                prep.recovery_files(original, current)
            current[prep.OTA0] ^= 1
            current[0xF000 + 28] ^= 1
            with self.assertRaises(ValueError):
                prep.recovery_files(original, current)


if __name__ == "__main__":
    unittest.main()
