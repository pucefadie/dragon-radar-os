#!/usr/bin/env bash
# Native Linux alternative; does not touch the Windows staging directory.
set -euo pipefail
project="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ARDUINO_CLI:=arduino-cli}"
: "${RADIO_DEV_INSECURE:=0}"
: "${RADAR_UI_TRIAL:=0}"
if [[ $# != 1 || ( "$RADIO_DEV_INSECURE" != 0 && "$RADIO_DEV_INSECURE" != 1 ) ]]; then
  echo "Usage: RADIO_DEV_INSECURE=0|1 $0 NEW_OUTPUT_DIRECTORY" >&2; exit 2
fi
if [[ ! "$RADAR_UI_TRIAL" =~ ^[01]$ || "$RADIO_DEV_INSECURE$RADAR_UI_TRIAL" == 11 ]]; then
  echo "RADAR_UI_TRIAL=1 requires RADIO_DEV_INSECURE=0" >&2; exit 2
fi
out="$1"
if [[ -e "$out" ]]; then echo "Output already exists: $out" >&2; exit 2; fi
mkdir -p "$out"
out="$(cd "$out" && pwd)"
stage="$out/dragon_radar_sketch"
mkdir -p "$stage" "$out/cache" "$out/libraries/DragonEspAudioCodec" "$out/artifacts"
cp "$project"/src/*.cpp "$project"/include/*.h "$project"/lib/BMM150/* "$stage/"
cp -R "$project"/lib/DragonEspAudioCodec/. "$out/libraries/DragonEspAudioCodec/"
flags=()
if [[ "$RADAR_UI_TRIAL" == 1 ]]; then
  cp "$project/examples/dragon_radar_ui_trial/nvs_startup.cpp" "$stage/"
  flags+=(--build-property 'compiler.c.elf.extra_flags=-Wl,--wrap=initArduino,--wrap=esp_partition_erase_range')
fi
cat > "$stage/dragon_radar_sketch.ino" <<'SKETCH'
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
SKETCH
python3 "$project/scripts/embed_audio.py" "$project/assets/radar_reference_s16le.pcm" "$stage/radar_beep_data.h"
python3 "$project/scripts/embed_codec_sample.py" "$project/assets/codec_probe.mp3" "$stage/codec_test_data.h"
"$ARDUINO_CLI" compile --build-property "compiler.cpp.extra_flags=-DRADIO_DEV_INSECURE=$RADIO_DEV_INSECURE -DRADAR_UI_TRIAL=$RADAR_UI_TRIAL" \
 "${flags[@]}" \
 --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB' \
 --jobs 4 --libraries "$project/references/libraries" --libraries "$out/libraries" \
 --build-path "$out/cache" --output-dir "$out/artifacts" "$stage"
