#!/usr/bin/env python3
import hashlib
import hmac
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import sphere_ota
from prepare_sphere_ota_migration import NEW_TABLE, partitions


class SphereOtaTests(unittest.TestCase):
    def test_manifest(self):
        with tempfile.TemporaryDirectory() as root:
            record = Path(root) / "sphere.bin"
            record.write_bytes(b"DRC1\1" + bytes(23) + bytes(range(32)) + bytes(16))
            record.chmod(0o600)
            ident, key, password = sphere_ota.keys(record)
            self.assertEqual((1, 32), (ident, len(password)))
            data = bytearray(512)
            data[0] = 0xE9
            data[12] = 13
            data[100:114] = b"M3e.app:1:302;"
            signed = sphere_ota.package(data, 1, key, 302)
            digest = hashlib.sha256(data).hexdigest()
            self.assertEqual(f"M3E1:1:302:{len(data)}:{digest}", signed["manifest"])
            self.assertEqual(hmac.digest(key, signed["manifest"].encode(), "sha256").hex(), signed["signature"])
            for sphere, version in ((2, 302), (1, 301), (1, 0)):
                with self.assertRaises(ValueError):
                    sphere_ota.package(data, sphere, key, version)
            data[12] = 9
            with self.assertRaises(ValueError):
                sphere_ota.package(data, 1, key, 302)
            record.chmod(0o644)
            with self.assertRaises(ValueError):
                sphere_ota.keys(record)

    def test_layout(self):
        offsets = sorted((start, start + size) for _, _, start, size in NEW_TABLE.values())
        self.assertTrue(all(a[1] <= b[0] for a, b in zip(offsets, offsets[1:])))
        self.assertEqual(offsets[-1][1], 0x400000)
        for name in ("app0", "app1"):
            self.assertEqual(NEW_TABLE[name][3], sphere_ota.SLOT)
            self.assertEqual(NEW_TABLE[name][2] % 0x10000, 0)
        with self.assertRaises(ValueError):
            partitions(bytes(3072))


if __name__ == "__main__":
    unittest.main()
