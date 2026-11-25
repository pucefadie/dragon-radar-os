#!/usr/bin/env bash
# Build an ESP-IDF 5.5.x rollback-enabled loader. Only writes host files.
set -euo pipefail
umask 077
[[ $# == 1 && ! -e "$1" ]] || { echo "Usage: $0 NEW_OUTPUT_DIRECTORY" >&2; exit 2; }
repo="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$1/main"
cd "$1"
cat > CMakeLists.txt <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dragon_c6_loader)
CMAKE
printf '%s\n' 'idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")' > main/CMakeLists.txt
printf '%s\n' 'void app_main(void) {}' > main/main.c
cp "$repo/examples/dragon_ball_beacon/sphere_ota.csv" partitions.csv
cat > sdkconfig.defaults <<'CONFIG'
CONFIG_IDF_TARGET="esp32c6"
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_BOOTLOADER_WDT_ENABLE=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG
idf.py -DIDF_TARGET=esp32c6 bootloader partition-table
rg -qx 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' sdkconfig
