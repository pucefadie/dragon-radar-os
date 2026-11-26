#!/usr/bin/env python3
"""Generate a private DRC2 pair for a new or existing Dragon Radar radio domain."""
import argparse
import hmac
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
    parser.add_argument("--id", type=int, choices=range(1, 8), default=1)
    parser.add_argument("--domain-record", type=Path, help="private radar record for the current sphere 1")
    args = parser.parse_args()
    if (args.id > 1) != bool(args.domain_record):
        parser.error("sphere IDs 2–7 require the existing private radar record for sphere 1")
    if args.domain_record:
        existing = args.domain_record.read_bytes()
        if (len(existing) != 76 or existing[:4] not in (b"DRC1", b"DRC2")
                or existing[4:6] != bytes((1, args.channel)) or not any(existing[28:60])
                or not any(existing[60:76])):
            parser.error("expected a valid sphere 1 record on the same channel")
    root = Path(__file__).resolve().parent.parent
    dest = args.directory.resolve()
    if root == dest or root in dest.parents:
        parser.error("keep keys outside the repository")
    # Read identifiers interactively rather than storing them in shell history.
    s3 = mac(input("Radar STA MAC: ").strip())
    c6 = mac(input("Sphere STA MAC: ").strip())
    if s3 == c6:
        parser.error("devices must have different STA addresses")
    if args.domain_record and existing[6:12] in (s3, c6):
        parser.error("new peer must differ from the existing sphere 1 MAC")
    dest.mkdir(mode=0o700)
    # One PMK for the ESP-NOW domain, with a separate LMK and HMAC key per pair.
    lmk = secrets.token_bytes(16)
    auth = secrets.token_bytes(32)
    if not args.domain_record:
        pmk = secrets.token_bytes(16)
    elif existing[:4] == b"DRC1":
        pmk = hmac.digest(existing[28:60], b"DRA1 M1c bench shared PMK", "sha256")[:16]
    else:
        pmk = existing[60:76]
    for name, target in (("radar.bin", c6), ("sphere.bin", s3)):
        file = dest / name
        file.touch(mode=0o600, exist_ok=False)
        file.write_bytes(b"DRC2" + bytes((args.id, args.channel)) + target + lmk + auth + pmk)


if __name__ == "__main__":
    main()
