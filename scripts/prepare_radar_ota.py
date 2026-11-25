#!/usr/bin/env python3
"""Prepare private ROM recovery files and the optional OTA0 release sector offline."""
import argparse
import hashlib
import pathlib
import struct
import zlib

FLASH = 0x1000000
SECTOR = 0x1000
OTA0 = 0x10000
OTA1 = 0x310000
SLOT = 0x300000
PERMIT_ADDRESS = OTA1 - SECTOR
PERMIT = b"DragonRadar M3f OTA0 released via ROM v1\0"
ORIGINAL = "8c3fc210f28f407c9e26e091214096ae7f367f0f0ae48ecd251a5bd8962d4a6e"
LOADER = "925239a911e376c690218a5b91341555f6ff447a37b333069df630770cf6f9ae"
TABLE = "ace02503447d0f470692e65fa76002f2d77a92dc81cd3813d8aa66718d716da9"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verified_pair(first, second):
    a, b = first.read_bytes(), second.read_bytes()
    if len(a) != FLASH or a != b:
        raise ValueError("Expected two identical 16 MiB captures")
    return a


def ota_record(snapshot, offset):
    sequence, state, crc = struct.unpack_from("<I20xII", snapshot, offset)
    if sequence in (0, 0xFFFFFFFF) or crc != zlib.crc32(struct.pack("<I", sequence), 0xFFFFFFFF):
        raise ValueError("Expected two valid OTA records")
    if state != 2:
        raise ValueError("Expected both OTA records VALID")
    return sequence, (sequence - 1) % 2


def recovery_files(original, current):
    if sha(original) != ORIGINAL:
        raise ValueError("Original radar capture differs from the archived baseline")
    if sha(current[:0x8000]) != LOADER or sha(current[0x8000:0x8C00]) != TABLE:
        raise ValueError("Current loader or partition table differs from the verified profile")
    if current[OTA0:OTA1] != original[OTA0:OTA1]:
        raise ValueError("Current OTA0 differs from preserved original")
    records = tuple(ota_record(current, off) for off in (0xE000, 0xF000))
    if {slot for _, slot in records} != {0, 1} or max(records)[1] != 1:
        raise ValueError("Expected preserved OTA0 and selected OTA1")
    tail = current[PERMIT_ADDRESS:OTA1]
    if PERMIT in tail:
        raise ValueError("OTA0 already contains a release marker")
    return {
        "original-0x10000.bin": original[OTA0:OTA1],
        "original-0xe000.bin": original[0xE000:0x10000],
        "baseline-0x310000.bin": current[OTA1:OTA1 + SLOT],
        "baseline-0xe000.bin": current[0xE000:0x10000],
        "restore-0x30f000.bin": tail,
        "release-0x30f000.bin": PERMIT + tail[len(PERMIT):],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original_a", "original_b", "current_a", "current_b", "output"):
        parser.add_argument(name, type=pathlib.Path)
    args = parser.parse_args()
    try:
        files = recovery_files(verified_pair(args.original_a, args.original_b),
                               verified_pair(args.current_a, args.current_b))
        args.output.mkdir(mode=0o700)
        manifest = []
        for name, data in files.items():
            (args.output / name).write_bytes(data)
            manifest.append(f"{sha(data)}  {name}\n")
        (args.output / "SHA256SUMS").write_text("".join(manifest), encoding="ascii")
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
