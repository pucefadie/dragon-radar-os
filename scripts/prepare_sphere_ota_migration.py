#!/usr/bin/env python3
"""Validate two ROM dumps and prepare local migration inputs. Never opens a port."""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib

FLASH_SIZE = 0x400000
TABLE = {
    "nvs": (1, 2, 0x9000, 0x5000), "otadata": (1, 0, 0xE000, 0x2000),
    "app0": (0, 16, 0x10000, 0x200000),
    "spiffs": (1, 130, 0x210000, 0x1E0000),
    "coredump": (1, 3, 0x3F0000, 0x10000),
}
NEW_TABLE = dict(TABLE, app0=(0, 16, 0x10000, 0x1C0000),
                 app1=(0, 17, 0x1D0000, 0x1C0000), spiffs=(1, 130, 0x390000, 0x60000))


def partitions(data):
    out = {}
    for off in range(0, len(data), 32):
        magic, typ, sub, start, size, name, flags = struct.unpack_from("<HBBII16sI", data, off)
        if magic == 0xEBEB:
            if data[off + 16:off + 32] != hashlib.md5(data[:off]).digest():
                raise ValueError("partition checksum mismatch")
            return out
        if magic != 0x50AA or flags:
            raise ValueError("invalid or unsupported partition entry")
        label = name.split(b"\0", 1)[0].decode("ascii")
        if label in out:
            raise ValueError("duplicate partition")
        out[label] = (typ, sub, start, size)
    raise ValueError("missing table checksum")


def preflight(a, b, table, loader, image):
    if a != b or len(a) != FLASH_SIZE:
        raise ValueError("expected two identical 4 MiB dumps")
    if partitions(a[0x8000:0x8C00]) != TABLE or partitions(table) != NEW_TABLE:
        raise ValueError("layout mismatch")
    if any(byte != 255 for byte in a[0x210000:0x3F0000]):
        raise ValueError("SPIFFS contains data; coordinate a separate data migration")
    ota = a[0xE000:0x10000]
    seq, state, crc = struct.unpack_from("<I20xII", ota)
    other = struct.unpack_from("<I20xII", ota, 0x1000)
    if (seq, state, crc) != (1, 0xFFFFFFFF, zlib.crc32(ota[:4], 0xFFFFFFFF)) or (
            other[0] not in (0, 0xFFFFFFFF) or other[2] != 0xFFFFFFFF):
        raise ValueError("expected original OTA0 selection in preserved otadata")
    for blob, limit in ((loader, 0x8000), (image, 0x1C0000)):
        if not 32 <= len(blob) <= limit or blob[0] != 0xE9 or blob[12:14] != b"\x0d\0":
            raise ValueError("unexpected ESP32-C6 image")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("dump_a", "dump_b", "table", "bootloader", "app", "output"):
        p.add_argument(name, type=Path)
    args = p.parse_args()
    a, b, table, loader, app = (getattr(args, k).read_bytes() for k in ("dump_a", "dump_b", "table", "bootloader", "app"))
    preflight(a, b, table, loader, app)
    args.output.mkdir(mode=0o700)
    files = {"migration-0x0000.bin": loader, "migration-0x8000.bin": table,
             "migration-0x10000.bin": app, "restore-0x0000.bin": a[:0x8000],
             "restore-0x8000.bin": a[0x8000:0x9000],
             "restore-0xe000.bin": a[0xE000:0x10000],
             "restore-0x10000.bin": a[0x10000:0x3F0000]}
    for name, data in files.items():
        (args.output/name).write_bytes(data)
    (args.output/"SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in files.items()), encoding="ascii")
    print("Prepared private files only; no device actions")


if __name__ == "__main__":
    main()
