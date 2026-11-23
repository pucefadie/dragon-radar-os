#!/usr/bin/env bash
# Isolated compile only. The normal build scripts and source are unchanged.
set -euo pipefail
project="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${M1C_BENCH_PROVISION:=0}"
: "${M1C_BENCH_FIXTURE:=0}"
if [[ $# != 1 || -e "$1" || ! "$M1C_BENCH_PROVISION" =~ ^[01]$ ||
      ! "$M1C_BENCH_FIXTURE" =~ ^[01]$ || "$M1C_BENCH_PROVISION$M1C_BENCH_FIXTURE" == 11 ]]; then
  echo "Usage: M1C_BENCH_PROVISION=0|1 M1C_BENCH_FIXTURE=0|1 $0 NEW_OUTPUT_DIRECTORY (mutually exclusive)" >&2
  exit 2
fi
mkdir -p "$1"
out="$(cd "$1" && pwd)"
stage="$out/dragon_m1c_bench"
mkdir "$stage" "$out/artifacts"
cp "$project/examples/dragon_m1c_bench/"* "$stage/"
cp "$project/include/"{hw_config,beacon_service,beacon_protocol,beacon_registry,dev_radio,dev_radio_tx,secure_radio,secure_radio_espnow,radio_link}.h "$stage/"
cp "$project/src/"{beacon_service,beacon_protocol,beacon_registry,secure_radio,radio_link}.cpp "$stage/"
"${ARDUINO_CLI:-arduino-cli}" compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB' \
  --build-property "compiler.cpp.extra_flags=-DM1C_BENCH_MODE=1 -DRADIO_DEV_INSECURE=0 -DM1C_BENCH_PROVISION=$M1C_BENCH_PROVISION -DM1C_BENCH_FIXTURE=$M1C_BENCH_FIXTURE" \
  --build-property 'compiler.c.elf.extra_flags=-Wl,--wrap=initArduino' \
  --jobs 4 --build-path "$out/cache" --output-dir "$out/artifacts" "$stage"
