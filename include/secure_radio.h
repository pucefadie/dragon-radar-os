#pragma once
#include "radio_link.h"
#include <cstddef>
#include <cstdint>

#if M1C_BENCH_FIXTURE && (M1C_BENCH_MODE != 1 || M1C_BENCH_PROVISION || RADIO_DEV_INSECURE)
#error "The DRA1 fixture requires the isolated, authenticated M1c radio build."
#endif

constexpr size_t SECURE_RADIO_SIZE = 80;
constexpr size_t SECURE_CONFIG_SIZE = 76;
constexpr char SECURE_RECORD_NAMESPACE[] = "dragon_radio";
constexpr char SECURE_RECORD_V1_KEY[] = "peer_v1";
constexpr char SECURE_RECORD_V2_KEY[] = "peer_v2";

enum class SecureRecordVersion : uint8_t { None, V1, V2 };
enum class SecureLinkStatus : uint8_t { Unavailable, AwaitingHandshake, Paired, Lost, ChannelMismatch };
enum class SecureRxEvent : uint8_t { None, SessionStarted, ProbeAck };
enum class SecureRecordRead { Missing, Loaded, Error };
using SecureRecordReader = SecureRecordRead (*)(void *, const char *, uint8_t *, size_t &);

// One deliberately provisioned sphere (ID 1). Keys have separate purposes.
struct SecureRadioConfig {
  uint8_t channel, peer[6], lmk[16], auth[32], pmk[16];
  SecureRecordVersion version;
};
bool secureRadioDecodeConfig(const uint8_t *bytes, size_t length, SecureRadioConfig &out);
bool secureRadioLoadConfig(SecureRecordReader read, void *storage, SecureRadioConfig &out);
void secureRadioClear(void *bytes, size_t length);

class SecureRadioIo {
 public:
  virtual ~SecureRadioIo() = default;
  virtual bool sign(const uint8_t *key, const uint8_t *bytes, size_t length, uint8_t *tag) = 0;
  virtual void random(uint8_t *bytes, size_t length) = 0;
  virtual bool send(const uint8_t *mac, const uint8_t *bytes, size_t length) = 0;
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
  SecureRxEvent receive(const uint8_t *source, const uint8_t *bytes, size_t length, uint32_t now);
  LinkState state(uint32_t now) const { return peers_.state(1, now); }
  SecureLinkStatus linkStatus(uint32_t now, bool channelAligned) const;
  uint32_t confirmations() const { return confirmations_; }
  const SecureRadioDiagnostics &diagnostics() const { return diagnostics_; }
#if M1C_BENCH_FIXTURE
  bool benchAction(M1cRadioAction action, uint32_t now);
  const M1cRadioResult &benchResult() const { return benchResult_; }
#endif
 private:
  enum class Kind : uint8_t { Hello=1, Challenge, Finish, Ready, Probe, Ack };
  struct Nonces { uint8_t a[16], b[16]; };
  void emit(Kind kind, const Nonces &nonce, uint32_t seq=0, uint32_t ack=0);
  bool pack(uint8_t *bytes, Kind kind, const Nonces &nonce, uint32_t seq, uint32_t ack);
  void established(uint32_t now);
  SecureRadioIo &io_;
  bool controller_;
  SecureRadioConfig config_;
  PairingRegistry peers_;
  ReplayWindow replay_;
  RadioAckTracker ack_;
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
