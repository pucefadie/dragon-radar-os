from pathlib import Path
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
            self.assertEqual(radar[:6], b"DRC1\x01\x06")
            self.assertEqual(sphere[:6], radar[:6])
            self.assertEqual(radar[6:12], bytes.fromhex("020000000002"))
            self.assertEqual(sphere[6:12], bytes.fromhex("020000000001"))
            self.assertEqual(radar[12:60], sphere[12:60])
            self.assertNotEqual(radar[60:], sphere[60:])
            self.assertEqual(stat.S_IMODE(dest.stat().st_mode), 0o700)
            for file in dest.iterdir():
                self.assertEqual(stat.S_IMODE(file.stat().st_mode), 0o600)
            result = subprocess.run(cmd, input=sample, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((dest / "radar.bin").read_bytes(), radar)


if __name__ == "__main__":
    unittest.main()
