#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "GivEnergyIdentity.h"
#include "GivEnergyProtocol.h"

namespace givenergy {

class GivEnergyScanner {
 public:
  enum class State : uint8_t {
    Idle,
    Scanning,
    Finished,
    Failed,
  };

  static constexpr size_t kMaximumResults = 20;

  void start(uint8_t firstHost = 1, uint8_t lastHost = 254);
  void startAddress(IPAddress address);
  void cancel();
  void update();

  State state() const { return state_; }
  uint8_t currentHost() const { return currentHost_; }
  IPAddress currentAddress() const;
  uint8_t currentSlaveAddress() const;
  size_t foundCount() const { return foundCount_; }
  IPAddress foundAddress(size_t index) const;
  const char *foundSerial(size_t index) const;
  uint8_t foundSlaveAddress(size_t index) const;
  bool foundIdentityValid(size_t index) const;
  bool foundProfileKnown(size_t index) const;
  uint16_t foundModelCode(size_t index) const;
  DeviceModel foundModel(size_t index) const;
  DeviceProfile foundProfile(size_t index) const;

 private:
  enum class ProbeState : uint8_t {
    StartHost,
    WaitingForReply,
  };

  void advance();
  void finishProbe(
      bool found,
      const DeviceIdentity *identity = nullptr);
  bool startCurrentSlaveProbe();
  void tryNextSlaveOrFinish();

  State state_ = State::Idle;
  ProbeState probeState_ = ProbeState::StartHost;
  IPAddress networkBase_;
  uint8_t firstHost_ = 1;
  uint8_t lastHost_ = 254;
  uint8_t currentHost_ = 1;
  uint8_t currentSlaveIndex_ = 0;
  uint32_t probeStartedMs_ = 0;
  bool hostConnected_ = false;
  WiFiClient probe_;
  StreamFramer framer_;
  IPAddress foundAddresses_[kMaximumResults]{};
  DeviceIdentity foundIdentities_[kMaximumResults]{};
  size_t foundCount_ = 0;
};

}  // namespace givenergy
