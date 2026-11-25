#!/usr/bin/env python3
"""Authorize a DragonRadar maintenance upload with its display-only session key."""
import argparse
import getpass
import hashlib
import hmac
import json
import pathlib
import re
import struct
import sys
import urllib.request


ADDRESS = "http://192.168.4.1/"
IDENTITY_OFFSET = 288
IMAGE_LIMIT = 0x2FF000
FAMILY = b"DragonRadar-M3f\0"


def release(image, key):
    if len(key) != 16:
        raise ValueError("The display key is 32 hex digits")
    if not IDENTITY_OFFSET + 20 <= len(image) <= IMAGE_LIMIT:
        raise ValueError("Image cannot fit in the permitted application area")
    if image[0] != 0xE9 or struct.unpack_from("<H", image, 12)[0] != 9:
        raise ValueError("Image is not ESP32-S3")
    if image[2:4] != b"\x02\x4f" or image[23] != 1:
        raise ValueError("Unexpected flash header or missing image hash")
    family, version = struct.unpack_from("<16sI", image, IDENTITY_OFFSET)
    if family != FAMILY or not 1 <= version <= 999999999:
        raise ValueError("Image has no valid DragonRadar M3f identity")
    manifest = f"M3F1:esp32s3:{version}:{len(image)}:{hashlib.sha256(image).hexdigest()}"
    return {"manifest": manifest, "signature": hmac.new(key, manifest.encode(), "sha256").hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("manifest", "upload"))
    parser.add_argument("firmware", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, help="new private manifest file")
    args = parser.parse_args()
    if bool(args.output) != (args.mode == "manifest"):
        parser.error("--output is required only for manifest")
    try:
        image = args.firmware.read_bytes()
        entered = "".join(getpass.getpass("Manifest key (join both display lines): ").split())
        if not re.fullmatch(r"[0-9a-fA-F]{32}", entered):
            raise ValueError("The display key is 32 hex digits")
        document = release(image, bytes.fromhex(entered))
        if args.mode == "manifest":
            import os
            fd = os.open(args.output, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            with os.fdopen(fd, "w", encoding="utf-8") as output:
                json.dump(document, output)
                output.write("\n")
        else:
            request = urllib.request.Request(ADDRESS + "firmware", data=image, headers={
                "Content-Type": "application/octet-stream",
                "X-Manifest": document["manifest"],
                "X-Signature": document["signature"],
            })
            client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            with client.open(request, timeout=100) as response:
                print(response.read(1024).decode())
    except (OSError, ValueError) as error:
        print(f"DragonRadar OTA: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
