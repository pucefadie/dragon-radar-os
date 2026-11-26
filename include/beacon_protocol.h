#pragma once
#include <stddef.h>
#include <stdint.h>
// Stable v2 wire frame: 24-byte header + 8-byte payload + CRC16-CCITT.
constexpr size_t RADIO_FRAME_SIZE = 34;
constexpr uint8_t RADIO_PROTOCOL_VERSION = 2;
constexpr size_t RADIO_PAYLOAD_SIZE = 8;
enum class RadioMessage : uint8_t { Beacon=1, Discovery=2, PairRequest=3, PairAck=4, Heartbeat=5, Status=6, Ping=7, Pong=8, Command=9, CommandAck=10, Found=11, ResetGame=12, BatteryStatus=13 };
enum class RadioCommand : uint8_t { LedOn=1, LedOff=2, LedPattern=3, Vibrate=4, Buzzer=5, FinalAlertOn=6, FinalAlertOff=7, StopAlert=8, Found=9, Reset=10 };
constexpr uint8_t RADIO_FLAG_ACK_REQUIRED=0x01, RADIO_FLAG_DIRECTED=0x02;
constexpr uint8_t RADIO_ID_CONTROLLER=0, RADIO_ID_BROADCAST=255;
constexpr size_t BEACON_FRAME_SIZE = RADIO_FRAME_SIZE;
constexpr uint32_t BEACON_STALE_MS = 15000;
struct RadioFrame { RadioMessage type; uint8_t id, destination=RADIO_ID_BROADCAST, flags, power_profile, tx_power_qdbm, payload_length; uint32_t session, sequence, ack_sequence; uint8_t payload[RADIO_PAYLOAD_SIZE]; };
bool radioDecode(const uint8_t *bytes, size_t length, RadioFrame &out);
void radioEncode(const RadioFrame &frame, uint8_t out[RADIO_FRAME_SIZE]);
uint16_t radioCrc16(const uint8_t *bytes, size_t length);
struct BeaconFrame {
  uint8_t id;
  bool battery_valid;
  uint32_t boot_id, sequence;
  uint16_t battery_mv;
};
bool beaconDecode(const uint8_t *bytes, size_t length, BeaconFrame &out);
void beaconEncode(const BeaconFrame &frame, uint8_t out[BEACON_FRAME_SIZE]);
bool beaconSequenceNewer(uint32_t sequence, uint32_t previous);
