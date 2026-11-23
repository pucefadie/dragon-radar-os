#!/usr/bin/env bash
# Compile only. Isolate the C6 board and role so no other sketch is staged.
set -euo pipefail
: "${M1C_LINK_PMK_PROBE:=0}"
if [[ $# != 2 || ! "$1" =~ ^(radio|provision)$ || -e "$2" || ! "$M1C_LINK_PMK_PROBE" =~ ^[01]$ ||
      ("$1" == provision && "$M1C_LINK_PMK_PROBE" == 1) ]]; then
  echo "Usage: $0 radio|provision NEW_OUTPUT_DIRECTORY" >&2
  exit 2
fi
project="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
role="$1"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
win_temp="$(powershell.exe -NoProfile -Command '[IO.Path]::GetTempPath()' | tr -d '\r')"
stage_root="$(mktemp -d "$(wslpath -u "$win_temp")/dragon_m1c_c6-XXXXXX")"

if [[ "$role" == provision ]]; then
  sketch=dragon_radio_provision
  provision=1
else
  sketch=dragon_ball_beacon
  provision=0
fi
stage="$stage_root/$sketch"
mkdir "$stage" "$stage_root/cache" "$stage_root/artifacts"
cp "$project/examples/$sketch/$sketch.ino" "$project/examples/dragon_m1c_c6/nvs_startup.cpp" "$stage/"
cp "$project/include/"{beacon_protocol,dev_radio,dev_radio_tx,radio_link,secure_radio,secure_radio_espnow}.h "$stage/"
cp "$project/src/"{beacon_protocol,dev_radio,radio_link,secure_radio}.cpp "$stage/"
if [[ "$role" == radio ]]; then
  printf '#pragma once\n#define DRAGON_BALL_ID 1\n#define DRAGON_RADIO_CHANNEL 6\n' >"$stage/beacon_config.h"
fi
cli="/mnt/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
"$cli" compile --fqbn 'esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota,CDCOnBoot=cdc' \
  --build-property "compiler.cpp.extra_flags=-DRADIO_DEV_INSECURE=0 -DM1C_C6_NVS_GUARD=1 -DM1C_C6_PROVISION=$provision -DM1C_LINK_PMK_PROBE=$M1C_LINK_PMK_PROBE" \
  --build-property 'compiler.c.elf.extra_flags=-Wl,--wrap=initArduino' \
  --jobs 4 --build-path "$(wslpath -w "$stage_root/cache")" \
  --output-dir "$(wslpath -w "$stage_root/artifacts")" "$(wslpath -w "$stage")" < /dev/null
cp "$stage_root/artifacts/"* "$out/"
