#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH_NAME="dragon_radar_sketch"
OUT_DIR="${PROJECT_DIR}/build/out"

# Use native Windows NTFS directory for build staging & cache (avoids UNC path issues in cmd.exe/gcc)
WIN_TEMP_DIR="$(powershell.exe -NoProfile -Command '[IO.Path]::GetTempPath()' | tr -d "\r")"
WIN_BUILD_ROOT="$(wslpath -u "$WIN_TEMP_DIR")/dragon_radar_build"
STAGE_DIR="${WIN_BUILD_ROOT}/${SKETCH_NAME}"
CACHE_DIR="${WIN_BUILD_ROOT}/cache"
WIN_OUT_DIR="${WIN_BUILD_ROOT}/out"
WIN_LIBS_DIR="${WIN_BUILD_ROOT}/libraries"

ARDUINO_CLI="/mnt/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB"
SRC_LIBS_DIR="${PROJECT_DIR}/references/libraries"

MODE="${1:-full}"

mkdir -p "${WIN_BUILD_ROOT}" "${CACHE_DIR}" "${WIN_OUT_DIR}" "${OUT_DIR}"
if [[ -f "${STAGE_DIR}/.mode" ]] && [[ "$(cat "${STAGE_DIR}/.mode")" != "${MODE}" ]]; then
  rm -rf "${STAGE_DIR}"
fi
mkdir -p "${STAGE_DIR}"
printf '%s' "${MODE}" > "${STAGE_DIR}/.mode"
copy_changed() { if [[ ! -f "$2" ]] || ! cmp -s "$1" "$2"; then cp -f "$1" "$2"; fi; }
# Remove stale staged source files while keeping unchanged timestamps for Arduino cache.
for f in "${STAGE_DIR}"/*.cpp "${STAGE_DIR}"/*.h; do
  [[ -e "$f" ]] || continue
  name="${f##*/}"
  [[ "$name" == "radar_beep_data.h" || "$name" == "codec_test_data.h" ]] && continue
  if [[ ! -f "${PROJECT_DIR}/src/${name}" && ! -f "${PROJECT_DIR}/include/${name}" && ! -f "${PROJECT_DIR}/lib/BMM150/${name}" ]]; then rm -f "$f"; fi
done

# Sync Waveshare libraries once to native Windows temp for fast compilation
for lib in GFX_Library_for_Arduino SensorLib XPowersLib ESP32_IO_Expander esp-lib-utils Mylibrary; do
  if [[ ! -d "${WIN_LIBS_DIR}/${lib}" ]]; then cp -r "${SRC_LIBS_DIR}/${lib}" "${WIN_LIBS_DIR}/"; fi
done

# Sync the pinned Espressif codec for Arduino library discovery.
mkdir -p "${WIN_LIBS_DIR}/DragonEspAudioCodec"
cp -ru "${PROJECT_DIR}/lib/DragonEspAudioCodec/." "${WIN_LIBS_DIR}/DragonEspAudioCodec/"

# .ino wrapper (includes library headers at top level so arduino-cli libsdetect discovers all libraries)
if [[ "${MODE}" == "phase1" ]]; then
cat > "${STAGE_DIR}/${SKETCH_NAME}.ino.tmp" << 'EOF'
#include <Wire.h>
#include <WiFi.h>
EOF
else
cat > "${STAGE_DIR}/${SKETCH_NAME}.ino.tmp" << 'EOF'
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <ESP_I2S.h>
#include <esp_audio_simple_dec.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "XPowersLib.h"
#include "Arduino_GFX_Library.h"
#include "SensorPCF85063.hpp"
#include "SensorQMI8658.hpp"
#include "TouchDrvCSTXXX.hpp"
EOF
fi

copy_changed "${STAGE_DIR}/${SKETCH_NAME}.ino.tmp" "${STAGE_DIR}/${SKETCH_NAME}.ino"
rm -f "${STAGE_DIR}/${SKETCH_NAME}.ino.tmp"

if [[ "${MODE}" == "phase1" ]]; then
  cp -f "${PROJECT_DIR}/include/hw_config.h" "${STAGE_DIR}/"
  cp -f "${PROJECT_DIR}/include/i2c_bus.h" "${STAGE_DIR}/"
  cp -f "${PROJECT_DIR}/src/i2c_bus.cpp" "${STAGE_DIR}/"
  cp -f "${PROJECT_DIR}/src/main.cpp" "${STAGE_DIR}/"
else
  for f in "${PROJECT_DIR}/include/"*.h "${PROJECT_DIR}/src/"*.cpp; do copy_changed "$f" "${STAGE_DIR}/${f##*/}"; done
  if [[ -d "${PROJECT_DIR}/lib/BMM150" ]]; then
    for f in "${PROJECT_DIR}/lib/BMM150/"*; do [[ -f "$f" ]] && copy_changed "$f" "${STAGE_DIR}/${f##*/}"; done
  fi
fi

python3 "${PROJECT_DIR}/scripts/embed_audio.py" "${PROJECT_DIR}/assets/radar_reference_s16le.pcm" "${STAGE_DIR}/radar_beep_data.h"
python3 "${PROJECT_DIR}/scripts/embed_codec_sample.py" "${PROJECT_DIR}/assets/codec_probe.mp3" "${STAGE_DIR}/codec_test_data.h"

W_STAGE='C:\Users\franc\AppData\Local\Temp\dragon_radar_build\dragon_radar_sketch'
W_CACHE='C:\Users\franc\AppData\Local\Temp\dragon_radar_build\cache'
W_OUT='C:\Users\franc\AppData\Local\Temp\dragon_radar_build\out'
W_LIBS='C:\Users\franc\AppData\Local\Temp\dragon_radar_build\libraries'

echo "[BUILD] Compiling ${SKETCH_NAME} (mode=${MODE}) with FQBN=${FQBN}..."
cd /mnt/c/Users/franc/AppData/Local/Temp/dragon_radar_build
"${ARDUINO_CLI}" compile \
  --build-property "compiler.cpp.extra_flags=-DRADIO_DEV_INSECURE=${RADIO_DEV_INSECURE:-0}" \
  --fqbn "${FQBN}" \
  --jobs 4 \
  --libraries "${W_LIBS}" \
  --build-path "${W_CACHE}" \
  --output-dir "${W_OUT}" \
  "${W_STAGE}" < /dev/null

cp -f "${WIN_OUT_DIR}/"* "${OUT_DIR}/"
echo "[BUILD] Success! Binaries in ${OUT_DIR}:"
ls -lh "${OUT_DIR}"
