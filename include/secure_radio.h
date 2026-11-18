#pragma once
#include "radio_link.h"
#include <cstddef>
#include <cstdint>

constexpr size_t SECURE_RADIO_SIZE = 80;
constexpr size_t SECURE_CONFIG_SIZE = 76;

// One deliberately provisioned sphere (ID 1). Keys have separate purposes.
struct SecureRadioConfig {
  uint8_t channel, peer[6], lmk[16], auth[32], pmk[16];
};
bool secureRadioDecodeConfig(const uint8_t *bytes, size_t length, SecureRadioConfig &out);

class SecureRadioIo {
 public:
  virtual ~SecureRadioIo() = default;
  virtual bool sign(const uint8_t *key, const uint8_t *bytes, size_t length, uint8_t *tag) = 0;
  virtual void random(uint8_t *bytes, size_t length) = 0;
  virtual bool send(const uint8_t *mac, const uint8_t *bytes, size_t length) = 0;
};

class SecureRadio {
 public:
  SecureRadio(SecureRadioIo &io, bool controller, const SecureRadioConfig &config);
  void tick(uint32_t now);
  void receive(const uint8_t *source, const uint8_t *bytes, size_t length, uint32_t now);
  LinkState state(uint32_t now) const { return peers_.state(1, now); }
  uint32_t confirmations() const { return confirmations_; }
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
  Nonces current_{}, pending_{};
  uint32_t started_=0, lastSend_=0, pendingAt_=0, nextProbe_=0, seq_=0, lastProbe_=0, confirmations_=0;
  bool attempting_=false, challenged_=false, pendingValid_=false, online_=false;
};
