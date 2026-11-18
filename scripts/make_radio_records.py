#!/usr/bin/env python3
"""Generate complementary Dragon Radar M1 provisioning records offline."""
import argparse
from pathlib import Path
import secrets


def mac(text):
    try:
        raw = bytes.fromhex(text.replace(":", ""))
    except ValueError as error:
        raise argparse.ArgumentTypeError("invalid hex MAC") from error
    if len(raw) != 6 or raw[0] & 1 or not any(raw):
        raise argparse.ArgumentTypeError("expected nonzero unicast STA MAC")
    return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="new private directory outside the repository")
    parser.add_argument("--channel", type=int, choices=range(1, 14), required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    dest = args.directory.resolve()
    if root == dest or root in dest.parents:
        parser.error("keep keys outside the repository")
    # Read identifiers interactively rather than storing them in shell history.
    s3 = mac(input("Radar STA MAC: ").strip())
    c6 = mac(input("Sphere STA MAC: ").strip())
    if s3 == c6:
        parser.error("devices must have different STA addresses")
    dest.mkdir(mode=0o700)
    common = secrets.token_bytes(16) + secrets.token_bytes(32)
    for name, target in (("radar.bin", c6), ("sphere.bin", s3)):
        file = dest / name
        file.touch(mode=0o600, exist_ok=False)
        file.write_bytes(b"DRC1" + bytes((1, args.channel)) + target + common + secrets.token_bytes(16))


if __name__ == "__main__":
    main()
