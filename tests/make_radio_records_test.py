from pathlib import Path
import hmac
import stat
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest


class RecordsTest(unittest.TestCase):
    def test_complementary_and_private(self):
        generator = Path(__file__).parent.parent / "scripts/make_radio_records.py"
        with TemporaryDirectory() as directory:
            dest = Path(directory) / "records"
            cmd = [sys.executable, generator, dest, "--channel", "6"]
            sample = "02:00:00:00:00:01\n02:00:00:00:00:02\n"
            subprocess.run(cmd, input=sample, text=True, capture_output=True, check=True)
            radar, sphere = [(dest / name).read_bytes() for name in ("radar.bin", "sphere.bin")]
            self.assertEqual(len(radar), 76)
            self.assertEqual(len(sphere), 76)
            self.assertEqual(radar[:6], b"DRC2\x01\x06")
            self.assertEqual(sphere[:6], radar[:6])
            self.assertEqual(radar[6:12], bytes.fromhex("020000000002"))
            self.assertEqual(sphere[6:12], bytes.fromhex("020000000001"))
            self.assertEqual(radar[12:], sphere[12:])
            self.assertEqual(len({radar[12:28], radar[28:44], radar[44:60], radar[60:76]}), 4)
            other = Path(directory) / "another-domain"
            subprocess.run([sys.executable, generator, other, "--channel", "6"],
                           input=sample, text=True, capture_output=True, check=True)
            regenerated = (other / "radar.bin").read_bytes()
            for key in (slice(12, 28), slice(28, 60), slice(60, 76)):
                self.assertNotEqual(radar[key], regenerated[key])
            self.assertEqual(stat.S_IMODE(dest.stat().st_mode), 0o700)
            for file in dest.iterdir():
                self.assertEqual(stat.S_IMODE(file.stat().st_mode), 0o600)
            result = subprocess.run(cmd, input=sample, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((dest / "radar.bin").read_bytes(), radar)

    def test_extend_existing_domain(self):
        generator = Path(__file__).parent.parent / "scripts/make_radio_records.py"
        sample = "02:00:00:00:00:01\n02:00:00:00:00:07\n"
        with TemporaryDirectory() as directory:
            base = Path(directory)
            for scheme in (b"DRC1", b"DRC2"):
                record = scheme + bytes((1, 6)) + bytes.fromhex("020000000002") + bytes(range(64))
                anchor = base / "anchor.bin"
                anchor.write_bytes(record)
                dest = base / (scheme.decode() + "-sphere7")
                cmd = [sys.executable, generator, dest, "--id", "7", "--channel", "6", "--domain-record", anchor]
                subprocess.run(cmd, input=sample, text=True, capture_output=True, check=True)
                radar = (dest / "radar.bin").read_bytes()
                sphere = (dest / "sphere.bin").read_bytes()
                self.assertEqual(radar[:6], b"DRC2\x07\x06")
                self.assertEqual(radar[12:], sphere[12:])
                self.assertNotEqual(radar[12:60], record[12:60])
                expected = hmac.digest(record[28:60], b"DRA1 M1c bench shared PMK", "sha256")[:16]
                self.assertEqual(radar[60:], expected if scheme == b"DRC1" else record[60:])
                self.assertEqual(anchor.read_bytes(), record)
            cmd = [sys.executable, generator, base / "wrong", "--id", "7", "--channel", "7", "--domain-record", anchor]
            self.assertNotEqual(subprocess.run(cmd, input=sample, text=True, capture_output=True).returncode, 0)


if __name__ == "__main__":
    unittest.main()
