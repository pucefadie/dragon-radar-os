#!/usr/bin/env bash
# Read private copies through Espressif's NVS driver with an in-memory flash backend.
set -euo pipefail
umask 077
if (( $# < 2 || $# > 3 )); then
  echo 'Usage: run.sh ESP_IDF_ROOT PRIVATE_20K_NVS [PRIVATE_DRC1_RECORD]' >&2
  exit 1
fi
testdir="$(cd "$(dirname "$0")" && pwd)"
d="$(cd "$1" && pwd)/components"
shift
tmp="$(mktemp -d)"
trap 'find "$tmp" -depth -delete' EXIT
printf '#pragma once\n' > "$tmp/sdkconfig.h"
printf '#pragma once\n' > "$tmp/esp_heap_caps.h"
printf '#pragma once\n#include "esp_err.h"\n#define ESP_ERR_FLASH_OP_FAIL (ESP_ERR_FLASH_BASE + 1)\n' > "$tmp/spi_flash_mmap.h"
cat > "$tmp/esp_log.h" <<'HEADER'
#pragma once
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
HEADER
cat > "$tmp/esp_rom_crc.h" <<'HEADER'
#pragma once
#include <zlib.h>
#include <stdint.h>
static inline uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t *buf,uint32_t size) {
    return crc32(seed,buf,size);
}
HEADER
g++ -std=c++20 -DNO_DEBUG_STORAGE -DLINUX_TARGET=1 -I"$tmp" -I"$d/nvs_flash/src" \
    -I"$d/nvs_flash/include" -I"$d/nvs_flash/private_include" -I"$d/esp_common/include" \
    -I"$d/esp_partition/include" "$testdir/mount.cpp" \
    "$d/nvs_flash/src/"{nvs_page,nvs_pagemanager,nvs_storage,nvs_types,nvs_item_hash_list}.cpp \
    -lz -o "$tmp/mount"
"$tmp/mount" "$@"
