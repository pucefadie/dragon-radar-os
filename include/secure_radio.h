#pragma once
#include "radio_link.h"
#include <cstddef>
#include <cstdint>

#if M1C_BENCH_FIXTURE && (M1C_BENCH_MODE != 1 || M1C_BENCH_PROVISION || RADIO_DEV_INSECURE)
#error "The DRA1 fixture requires the isolated, authenticated M1c radio build."
#endif

constexpr uint8_t SECURE_SPHERE_COUNT = 7;
constexpr size_t SECURE_RADIO_SIZE = 80;
constexpr size_t SECURE_CONFIG_SIZE = 76;
constexpr char SECURE_RECORD_NAMESPACE[] = "dragon_radio";
constexpr char SECURE_RECORD_V1_KEY[] = "peer_v1";
constexpr char SECURE_RECORD_V2_KEY[] = "peer_v2";

enum class SecureRecordVersion : uint8_t { None, V1, V2 };
enum class SecureLinkStatus : uint8_t { Unavailable, AwaitingHandshake, Paired, Lost, ChannelMismatch };
enum class SecureMotorStatus : uint8_t { Idle, Pending, Accepted, CoolingDown, Unavailable, Unconfirmed };
// Packed RGB operation: low 7 bits brightness, next 3 palette, next 2 action.
enum class RgbAction : uint8_t { Off, On, Found };
inline uint32_t rgbCommand(RgbAction action,uint8_t brightness=50,uint8_t color=0) {
  return brightness | (uint32_t(color)<<7) | (uint32_t(action)<<10);
}
using SecureRgbHandler = bool (*)(uint32_t command,uint32_t now);
using SecureMotorHandler = SecureMotorStatus (*)(uint32_t now);
enum class SecureRxEvent : uint8_t { None, SessionStarted, ProbeAck };
enum class SecureRecordRead { Missing, Loaded, Error };
using SecureRecordReader = SecureRecordRead (*)(void *, const char *, uint8_t *, size_t &);

// One deliberately provisioned sphere. Keys have separate purposes.
struct SecureRadioConfig {
  uint8_t channel, peer[6], lmk[16], auth[32], pmk[16];
  SecureRecordVersion version;
  uint8_t id = 1;
};
bool secureRadioDecodeConfig(const uint8_t *bytes, size_t length, SecureRadioConfig &out);
const char *secureRadioRecordKey(uint8_t id);
bool secureRadioLoadConfig(SecureRecordReader read, void *storage, SecureRadioConfig &out, uint8_t id=1);
void secureRadioClear(void *bytes, size_t length);

class SecureRadioIo {
 public:
  virtual ~SecureRadioIo() = default;
  virtual bool sign(const uint8_t *key, const uint8_t *bytes, size_t length, uint8_t *tag) = 0;
  virtual void random(uint8_t *bytes, size_t length) = 0;
  virtual bool send(const uint8_t *mac, const uint8_t *bytes, size_t length) = 0;
  virtual bool sendMotor(const uint8_t *mac, const uint8_t *bytes, size_t length) { return send(mac,bytes,length); }
};
bool secureRadioResolvePmk(SecureRadioConfig &config, SecureRadioIo &io);

struct SecureRadioDiagnostics {
  uint32_t probe_retry_queued = 0;
  uint32_t auth_rejected = 0;
  uint32_t stale_session = 0;
  uint32_t duplicate_probe = 0;
};

#if M1C_BENCH_FIXTURE
enum class M1cRadioAction { None, DropAck, CorruptProbe, CaptureProbe, Replay };

struct M1cRadioResult {
  uint32_t ack_dropped = 0;
  uint32_t corrupt_queued = 0;
  uint32_t replay_queued = 0;
  bool captured = false;
};
#endif

class SecureRadio {
 public:
  SecureRadio(SecureRadioIo &io, bool controller, const SecureRadioConfig &config);
  void tick(uint32_t now);
  SecureRxEvent receive(const uint8_t *source, const uint8_t *bytes, size_t length, uint32_t now, uint32_t queueAge=0);
  bool requestMotor(uint32_t now);
  bool requestRgb(uint32_t now,uint32_t command);
  SecureMotorStatus rgbStatus() const { return rgbStatus_; }
  void setRgbHandler(SecureRgbHandler handler) { rgbHandler_=handler; }
  void cancelMotorRequest();
  SecureMotorStatus motorStatus() const { return motorStatus_; }
  void setMotorHandler(SecureMotorHandler handler) { motorHandler_=handler; }
  LinkState state(uint32_t now) const { return peers_.state(config_.id, now); }
  SecureLinkStatus linkStatus(uint32_t now, bool channelAligned) const;
  uint32_t confirmations() const { return confirmations_; }
  const SecureRadioDiagnostics &diagnostics() const { return diagnostics_; }
#if M1C_BENCH_FIXTURE
  bool benchAction(M1cRadioAction action, uint32_t now);
  const M1cRadioResult &benchResult() const { return benchResult_; }
#endif
 private:
  enum class Kind : uint8_t { Hello=1, Challenge, Finish, Ready, Probe, Ack, Vibrate, VibrateResult, Rgb, RgbResult };
  struct Nonces { uint8_t a[16], b[16]; };
  void emit(Kind kind, const Nonces &nonce, uint32_t seq=0, uint32_t ack=0);
  bool pack(uint8_t *bytes, Kind kind, const Nonces &nonce, uint32_t seq, uint32_t ack);
  void established(uint32_t now);
  SecureRadioIo &io_;
  bool controller_;
  SecureRadioConfig config_;
  PairingRegistry peers_;
  ReplayWindow replay_;
  RadioAckTracker ack_, motorAck_;
  ReplayWindow motorReplay_, rgbReplay_;
  RadioAckTracker rgbAck_;
  SecureRgbHandler rgbHandler_=nullptr;
  SecureMotorStatus rgbStatus_=SecureMotorStatus::Idle;
  uint32_t rgbSequence_=0, lastRgb_=0, rgbStarted_=0;
  bool lastRgbAccepted_=false;
  SecureMotorHandler motorHandler_=nullptr;
  SecureMotorStatus motorStatus_=SecureMotorStatus::Idle, lastMotorResult_=SecureMotorStatus::Unavailable;
  uint32_t motorSequence_=0, lastMotorRequest_=0, motorStarted_=0;
  SecureRadioDiagnostics diagnostics_;
  Nonces current_{}, pending_{};
  uint32_t started_=0, lastSend_=0, pendingAt_=0, nextProbe_=0, seq_=0, lastProbe_=0, confirmations_=0;
  bool attempting_=false, challenged_=false, pendingValid_=false, online_=false;
#if M1C_BENCH_FIXTURE
  bool benchSendProbe(uint8_t *frame, uint8_t length);
  M1cRadioAction benchAction_ = M1cRadioAction::None;
  M1cRadioResult benchResult_;
  uint8_t benchCaptured_[SECURE_RADIO_SIZE]{};
#endif
};
