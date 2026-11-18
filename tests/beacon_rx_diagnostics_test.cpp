#include <cassert>
#include "beacon_protocol.h"
#include "beacon_service.h"
#include <esp_now.h>

MockRxCallback mockRxCallback = nullptr;

static void expect(const char *name, unsigned value) {
  Stream report;
  beaconServicePrint(report);
  const std::string field = std::string(name) + '=' + std::to_string(value);
  const auto index = report.text.find(field);
  assert(index != std::string::npos);
  const char next = report.text.at(index + field.size());
  assert(next == ' ' || next == '\n');
}

static void fixCrc(uint8_t *frame) {
  const auto crc = radioCrc16(frame, BEACON_FRAME_SIZE - 2);
  frame[BEACON_FRAME_SIZE - 2] = uint8_t(crc);
  frame[BEACON_FRAME_SIZE - 1] = uint8_t(crc >> 8);
}

int main() {
  beaconServiceInit();
  assert(mockRxCallback != nullptr);
  expect("ESPNOW_READY", 1);
  expect("CALLBACK_OBSERVED", 0);

  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6}, otherMac[6] = {2, 2, 3, 4, 5, 6};
  const wifi_pkt_rx_ctrl_t control{-50};
  const esp_now_recv_info_t info{mac, &control}, conflictInfo{otherMac, &control};
  const esp_now_recv_info_t noSource{nullptr, &control}, noControl{mac, nullptr};
  BeaconFrame beacon{1, false, 1, 1, 0};
  uint8_t frame[BEACON_FRAME_SIZE];
  beaconEncode(beacon, frame);
  mockRxCallback(nullptr, frame, sizeof frame);
  mockRxCallback(&info, nullptr, sizeof frame);
  mockRxCallback(&noSource, frame, sizeof frame);
  mockRxCallback(&noControl, frame, sizeof frame);
  mockRxCallback(&info, frame, sizeof frame - 1);
  expect("RX_CALLBACK_TOTAL", 5);
  expect("RX_BAD_METADATA", 4);
  expect("RX_LEN_34", 4);
  expect("RX_LEN_OTHER", 1);
  expect("RX_QUEUE_OK", 0);
  expect("RX_DECODER_DELIVERED", 0);
  expect("CALLBACK_OBSERVED", 1);

  const auto deliver = [&](const esp_now_recv_info_t &metadata) {
    mockRxCallback(&metadata, frame, sizeof frame);
    beaconServiceUpdate();
  };
  deliver(info);
  expect("RX_DECODER_DELIVERED", 1);
  expect("RX_MAGIC_OK", 1);
  expect("RX_PROTOCOL_OK", 1);
  expect("RX_CRC_OK", 1);
  expect("RX_TYPE_BEACON", 1);
  expect("RX_ID_OK", 1);
  expect("RX_BEACON_DECODED", 1);
  expect("RX_REGISTRY_ACCEPTED", 1);

  deliver(info);
  deliver(conflictInfo);
  beacon.id = 0;
  beaconEncode(beacon, frame);
  deliver(info);
  expect("RX_REJECT_DUPLICATE", 1);
  expect("RX_REJECT_CONFLICT", 1);
  expect("RX_REJECT_INVALID", 1);
  expect("RX_REGISTRY_REJECTED", 3);
  expect("RX_DECODE_INVALID", 0);

  beacon.id = 1;
  beaconEncode(beacon, frame);
  frame[0] ^= 1;
  fixCrc(frame);
  deliver(info);
  expect("RX_MAGIC_BAD", 1);
  expect("RX_CRC_BAD", 0);
  beaconEncode(beacon, frame);
  ++frame[4];
  fixCrc(frame);
  deliver(info);
  expect("RX_PROTOCOL_BAD", 1);
  expect("RX_CRC_BAD", 0);
  beaconEncode(beacon, frame);
  frame[32] ^= 1;
  deliver(info);
  expect("RX_CRC_BAD", 1);
  beaconEncode(beacon, frame);
  frame[6] = 8;
  fixCrc(frame);
  deliver(info);
  expect("RX_ID_BAD", 2);
  expect("RX_DECODE_INVALID", 4);
  expect("RX_BEACON_DECODED", 4);
  expect("RX_REGISTRY_ACCEPTED", 1);

  beaconEncode(beacon, frame);
  for (int i = 0; i < 17; ++i) mockRxCallback(&info, frame, sizeof frame);
  expect("RX_CALLBACK_TOTAL", 30);
  expect("RX_QUEUE_DROP", 1);
  expect("RX_QUEUE_OK", 24);
  expect("RX_DECODER_DELIVERED", 8);
  beaconServiceUpdate();
  expect("RX_DECODER_DELIVERED", 24);
}
