#!/usr/bin/env python3
"""Offline inspection of a built M1c S3 application, never a device."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", type=Path)
parser.add_argument("elf", type=Path)
parser.add_argument("--xtensa-prefix", default="xtensa-esp32s3-elf-")
parser.add_argument("--provision", action="store_true")
args = parser.parse_args()


def tool(name, *options):
    return subprocess.check_output([args.xtensa_prefix + name, *options, args.elf],
                                   text=True)


def require(condition, reason):
    if not condition:
        raise SystemExit("FAIL: " + reason)


image = args.image.read_bytes()
require(b"M1C_BENCH_MODE=1 ACTUATOR_CODE_EXCLUDED" in image, "missing bench marker")
require((b"M1C_BENCH_PROVISION=1 USB_ONLY" in image) == args.provision,
        "provision build marker does not match")
require((b"RADIO_PROVISION_OK" in image) == args.provision,
        "unexpected provisioning receiver presence")
require((b"[SECURE RADIO]" in image) != args.provision, "secure radio in wrong variant")
require(b"[DEV INSECURE]" not in image, "DEV transport embedded")

symbols = tool("nm", "-C", "--defined-only")
excluded = r"\b(motor(Init|SafePreInit|PulseMs|StartPattern|Trigger\w*|WatchdogCallback|Update|Stop)|" \
           r"sensorsRunMotorInterferenceTest|appRadarHaptic|runAllTestsSequential|" \
           r"audioService(TestTone|RadarBeep)|esp_ota_mark_app[^\s]*|ArduinoOTA|initArduino)\b"
require(not re.search(excluded, symbols), "unexpected actuator, OTA or core startup symbol")
if args.provision:
    require(not re.search(r"\besp_wifi_start\b", symbols), "Wi-Fi starts in provision image")
entry = tool("objdump", "-d", "--disassemble=app_main")
require(re.search(r"\bcall\w*\s+\w+ <__wrap_initArduino>", entry),
        "app_main does not call the bench startup")
startup = tool("objdump", "-d", "--disassemble=__wrap_initArduino")
calls = re.findall(r"\bcall\w*\s+\w+ <([^>]+)>", startup)
require(calls == ["gpio_set_level", "gpio_config", "gpio_get_level", "nvs_flash_init"],
        f"unexpected bench startup calls: {calls}")
print("PASS: markers, role separation, actuator symbols and actual startup call order")
