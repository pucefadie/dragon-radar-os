#!/usr/bin/env python3
"""Prepare and send an authenticated M3e image on the sphere's local AP."""
import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import socket
import sys
import urllib.error
import urllib.request

DOMAIN = b"DragonRadar/M3e/maintenance"
SLOT = 0x1C0000
URL = "http://192.168.4.1/firmware"


def keys(path):
    if path.stat().st_mode & 0o077:
        raise ValueError("DRC record must have private permissions (0600)")
    record = path.read_bytes()
    if len(record) != 76 or record[:4] not in (b"DRC1", b"DRC2") or not 1 <= record[4] <= 7 or not any(record[28:60]):
        raise ValueError("invalid sphere DRC record")
    master = hmac.digest(record[28:60], DOMAIN, "sha256")
    return record[4], hmac.digest(master, b"sign", "sha256"), hmac.digest(master, b"access-point", "sha256")[:16].hex()


def package(image, sphere, signing, version):
    if not 256 <= len(image) <= SLOT or image[0] != 0xE9 or int.from_bytes(image[12:14], "little") != 13:
        raise ValueError("expected an ESP32-C6 application within one M3e slot")
    if not 1 <= version <= 0xFFFFFF or not re.search(rb"\bM3e\.app:%d:%d;" % (sphere, version), image):
        raise ValueError("image does not identify this sphere, version and M3e layout")
    manifest = f"M3E1:{sphere}:{version}:{len(image)}:{hashlib.sha256(image).hexdigest()}"
    return {"manifest": manifest, "signature": hmac.digest(signing, manifest.encode("ascii"), "sha256").hex()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("record", type=Path, help="private sphere.bin association")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("credentials", help="show local AP credentials on a terminal")
    for name in ("manifest", "upload"):
        cmd = sub.add_parser(name)
        cmd.add_argument("image", type=Path)
        cmd.add_argument("version", type=int)
        if name == "manifest":
            cmd.add_argument("output", type=Path)
    args = parser.parse_args()
    ident, signing, password = keys(args.record)
    if args.action == "credentials":
        if not sys.stdout.isatty():
            raise ValueError("credentials require a local terminal")
        print(f"DragonBall-{ident:02d}\n{password}")
        return
    image = args.image.read_bytes()
    signed = package(image, ident, signing, args.version)
    if args.action == "manifest":
        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w", encoding="ascii") as out:
            json.dump(signed, out)
        return
    request = urllib.request.Request(URL, data=image, headers={
        "Content-Type": "application/octet-stream",
        "X-Manifest": signed["manifest"], "X-Signature": signed["signature"],
    })
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=130) as response:
        print(response.read(256).decode("utf-8", errors="replace"))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, urllib.error.URLError, socket.timeout) as exc:
        sys.exit(str(exc))
