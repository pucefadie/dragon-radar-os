#!/usr/bin/env bash
# Compile only. Isolate the C6 board and role so no other sketch is staged.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 || ! "$1" =~ ^(radio|ota|provision)$ || -e "$2" ||
      ! "${3:-1}" =~ ^[1-7]$ || ( "$1" == provision && $# == 3 ) ]]; then
  echo "Usage: $0 radio|ota NEW_OUTPUT_DIRECTORY [ID 1..7] | provision NEW_OUTPUT_DIRECTORY" >&2
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
cp "$project/include/"{beacon_protocol,dev_radio,dev_radio_tx,radio_link,secure_radio,secure_radio_espnow,secure_radio_provision}.h "$stage/"
cp "$project/src/"{beacon_protocol,dev_radio,radio_link,secure_radio}.cpp "$stage/"
if [[ "$role" != provision ]]; then
  cp "$project/examples/$sketch/"{dra1_user_led,sphere_motor,sphere_motor_gpio,sphere_motor_rmt,sphere_rgb}.h "$stage/"
  printf '#pragma once\n#define DRAGON_BALL_ID %d\n#define DRAGON_RADIO_CHANNEL 6\n' "${3:-1}" >"$stage/beacon_config.h"
fi
white="${SPHERE_RGB_STARTUP_WHITE:-1}"
if [[ "$white" != 0 && "$white" != 1 ]]; then
  printf '%s\n' 'SPHERE_RGB_STARTUP_WHITE must be 0 or 1' >&2
  exit 2
fi
wrap="-Wl,--wrap=initArduino"
ota=0
version="${SPHERE_OTA_VERSION:-311}"
if [[ "$role" == ota ]]; then
  [[ "$version" =~ ^[1-9][0-9]{0,7}$ ]] && (( version <= 16777215 ))
  ota=1
  cp "$project/examples/$sketch/sphere_ota.csv" "$stage/partitions.csv"
  cp "$project/examples/$sketch/"{sphere_ota,sphere_nvs_guard}.h "$stage/"
  wrap+=",--wrap=esp_partition_write,--wrap=esp_partition_write_raw,--wrap=esp_partition_erase_range"
fi
trace="${SPHERE_OTA_NVS_TRACE:-0}"
if [[ "$trace" == 1 && "$ota" == 1 ]]; then
  cp "$project/examples/$sketch/sphere_nvs_trace.h" "$stage/"
  wrap+=",--wrap=esp_partition_read_raw,--wrap=_ZN3nvs4Page4loadEPNS_9PartitionEm,--wrap=_ZNK3nvs4Page12getSeqNumberERm,--wrap=_ZN3nvs4Page5eraseEv"
elif [[ "$trace" != 0 ]]; then
  printf '%s\n' 'SPHERE_OTA_NVS_TRACE=1 requires the OTA role' >&2
  exit 2
fi
cli="/mnt/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
"$cli" compile --fqbn 'esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota,CDCOnBoot=cdc' \
  --build-property "compiler.cpp.extra_flags=-DRADIO_DEV_INSECURE=0 -DM1C_C6_NVS_GUARD=1 -DM1C_C6_PROVISION=$provision -DSPHERE_RGB_STARTUP_WHITE=$white -DSPHERE_OTA=$ota -DSPHERE_OTA_VERSION=$version -DSPHERE_OTA_NVS_TRACE=$trace" \
  --build-property "compiler.c.elf.extra_flags=$wrap" \
  --jobs 4 --build-path "$(wslpath -w "$stage_root/cache")" \
  --output-dir "$(wslpath -w "$stage_root/artifacts")" "$(wslpath -w "$stage")" < /dev/null
cp "$stage_root/artifacts/"* "$out/"
