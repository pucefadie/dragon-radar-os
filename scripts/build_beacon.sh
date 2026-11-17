#!/usr/bin/env bash
# Stage the shared protocol beside the generic transmitter for Arduino CLI Windows.
set -euo pipefail
if [[ $# -ne 3 || ! "$1" =~ ^[1-7]$ || ! "$2" =~ ^([1-9]|1[0-3])$ ]]; then
  echo 'Usage: scripts/build_beacon.sh <ID 1..7> <Wi-Fi channel 1..13> <FQBN>' >&2
  exit 2
fi
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN_TEMP_DIR="$(powershell.exe -NoProfile -Command '[IO.Path]::GetTempPath()' | tr -d "\r")"
ROOT="$(wslpath -u "$WIN_TEMP_DIR")/dragon_beacon_build"
STAGE="${ROOT}/dragon_ball_beacon"
CACHE="${ROOT}/cache"
OUT="${PROJECT_DIR}/build/beacon-$1"
WIN_OUT="${ROOT}/out-$1"
CLI="/mnt/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
mkdir -p "$STAGE" "$CACHE" "$OUT" "$WIN_OUT"
cp "$PROJECT_DIR/examples/dragon_ball_beacon/dragon_ball_beacon.ino" "$STAGE/"
cp "$PROJECT_DIR/include/beacon_protocol.h" "$PROJECT_DIR/src/beacon_protocol.cpp" "$PROJECT_DIR/include/radio_link.h" "$PROJECT_DIR/src/radio_link.cpp" "$STAGE/"
printf '#pragma once\n#define DRAGON_BALL_ID %d\n#define DRAGON_RADIO_CHANNEL %d\n' "$1" "$2" > "$STAGE/beacon_config.h"
cd "$ROOT"
"$CLI" compile --fqbn "$3" --jobs 4 --build-path "$(wslpath -w "$CACHE")" --output-dir "$(wslpath -w "$WIN_OUT")" "$(wslpath -w "$STAGE")" < /dev/null

cp -f "$WIN_OUT"/* "$OUT"/
