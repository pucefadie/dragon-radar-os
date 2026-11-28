#!/usr/bin/env bash
# Host-only regression suite. No serial, network, OTA or hardware operations.
set -euo pipefail
cd "$(dirname "$0")/.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cxx="${CXX:-g++}"
flags=(-std=c++11 -Wall -Wextra -Werror -Iinclude)
run() {
  local name="$1"; shift
  "$cxx" "${flags[@]}" "$@" -o "$tmp/$name"
  "$tmp/$name"
  printf 'PASS %s\n' "$name"
}
run app-manager tests/app_manager_test.cpp src/app_manager.cpp
run gesture tests/gesture_test.cpp src/gesture_manager.cpp
run standby-button tests/standby_button_test.cpp
run radar-feedback tests/radar_feedback_test.cpp
run radar-ui-geometry tests/radar_ui_geometry_test.cpp
run proximity tests/proximity_engine_test.cpp src/proximity_engine.cpp
run proximity-sanitized -fsanitize=address,undefined -fno-omit-frame-pointer tests/proximity_engine_test.cpp src/proximity_engine.cpp
run radio-link tests/radio_link_test.cpp src/radio_link.cpp src/beacon_protocol.cpp
run beacon-protocol tests/beacon_protocol_test.cpp src/beacon_protocol.cpp
run beacon-registry tests/beacon_registry_test.cpp src/beacon_registry.cpp src/beacon_protocol.cpp
run dev-radio tests/dev_radio_test.cpp src/dev_radio.cpp src/beacon_protocol.cpp
run secure-radio tests/secure_radio_test.cpp src/secure_radio.cpp src/radio_link.cpp -lcrypto
run secure-radio-fixture -DM1C_BENCH_MODE=1 -DM1C_BENCH_FIXTURE=1 tests/secure_radio_test.cpp src/secure_radio.cpp src/radio_link.cpp -lcrypto
run rx-diagnostics -DRADIO_DEV_INSECURE=0 -DRADIO_RX_HOST_TEST -Itests/rx_mocks tests/beacon_rx_diagnostics_test.cpp src/beacon_service.cpp src/beacon_protocol.cpp src/beacon_registry.cpp
run dev-tx -DRADIO_DEV_INSECURE=1 -Itests/tx_mocks tests/dev_radio_tx_test.cpp
python3 tests/make_radio_records_test.py
printf 'PASS make-radio-records\n'
