#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "GivEnergyBatteries.h"
#include "GivEnergyControls.h"
#include "GivEnergyProtocol.h"
#include "GivEnergyRegisters.h"

namespace givenergy {

enum class ClientState : uint8_t {
  Stopped,
  WifiUnavailable,
  Connecting,
  Online,
  WaitingForResponse,
};

enum class ControlStatus : uint8_t {
  Idle,
  Queued,
  Sent,
  Verified,
  Rejected,
  Failed,
};

struct ClientStats {
  uint32_t connections = 0;
  uint32_t reconnects = 0;
  uint32_t requestsSent = 0;
  uint32_t responsesReceived = 0;
  uint32_t timeouts = 0;
  uint32_t protocolErrors = 0;
  uint32_t registerUpdates = 0;
  uint32_t writesVerified = 0;
};

class GivEnergyClient {
 public:
  GivEnergyClient();

  void begin(
      IPAddress inverterAddress,
      DeviceProfile profile = DeviceProfile::SinglePhase,
      uint8_t slaveAddress = kDefaultSlaveAddress,
      const char *adapterSerial = "AB1234G567");
  void stop();
  void update();

  void setPollIntervals(uint32_t fastMilliseconds, uint32_t slowMilliseconds);
  void setResponseTimeout(uint32_t milliseconds);

  bool requestRefresh(bool includeSlowRegisters = false);
  bool requestRegisters(
      RegisterBank bank,
      uint16_t baseRegister,
      uint16_t count,
      uint8_t slaveAddress = 0);
  bool requestBatteryRefresh(bool rediscover = false);

  ControlValidation control(ControlId id, uint16_t value);
  ControlValidation setTimeSlot(
      bool discharge,
      uint8_t slot,
      uint8_t startHour,
      uint8_t startMinute,
      uint8_t endHour,
      uint8_t endMinute);

  bool connected();
  ClientState state() const { return state_; }
  DeviceProfile profile() const { return profile_; }
  ControlStatus lastControlStatus() const { return controlStatus_; }
  uint16_t lastControlRegister() const { return lastControlRegister_; }
  uint16_t lastControlValue() const { return lastControlValue_; }
  const char *lastError() const { return lastError_; }
  const char *inverterSerial() const { return inverterSerial_; }
  uint32_t lastSuccessfulResponseMs() const { return lastSuccessfulResponseMs_; }
  uint32_t readGeneration() const { return readGeneration_; }
  RegisterBank lastReadBank() const { return lastReadBank_; }
  uint16_t lastReadBaseRegister() const {
    return lastReadBaseRegister_;
  }
  uint16_t lastReadRegisterCount() const {
    return lastReadRegisterCount_;
  }

  const RegisterStore &registers() const { return registers_; }
  size_t batteryCount() const { return batteryCount_; }
  const BatteryData *battery(size_t index) const {
    return index < batteryCount_ ? &batteries_[index] : nullptr;
  }
  bool batteryRefreshInProgress() const {
    return batteryCommandsPending_ > 0;
  }
  uint32_t batteryGeneration() const {
    return batteryGeneration_;
  }
  uint32_t lastBatteryRefreshMs() const {
    return lastBatteryRefreshMs_;
  }
  bool liveData(LiveData &live) const {
    return decodeLiveData(profile_, registers_, live);
  }
  const ClientStats &stats() const { return stats_; }

 private:
  enum class CommandKind : uint8_t {
    Read,
    BatteryRead,
    Write,
    VerifyWrite,
  };

  struct Command {
    CommandKind kind = CommandKind::Read;
    RegisterBank bank = RegisterBank::Input;
    uint16_t address = 0;
    uint16_t countOrValue = 0;
    uint16_t expectedValue = 0;
    uint8_t slaveAddress = 0;
    uint8_t retriesRemaining = 1;
    BatteryTelemetryFormat batteryFormat =
        BatteryTelemetryFormat::LowVoltage16;
  };

  static constexpr size_t kQueueSize = 48;

  bool connectSocket();
  void disconnectSocket(const char *reason);
  void receiveMessages();
  void handleMessage(const Message &message);
  void handleTimeout();
  bool sendCurrent();
  bool enqueue(const Command &command);
  bool dequeue(Command &command);
  size_t queuedCount() const;
  size_t queueFree() const;
  bool enqueuePollBlocks(const PollBlock *blocks, size_t count);
  bool enqueueBatteryRead(
      BatteryTelemetryFormat format,
      uint8_t slaveAddress,
      uint16_t baseRegister);
  bool enqueueControlCommand(const ControlCommand &command);
  bool responseMatchesCurrent(const Message &message) const;
  void handleBatteryMessage(const Message &message);
  void finishBatteryCommand();
  void failControl(const char *error);
  void setError(const char *error);

  WiFiClient socket_;
  IPAddress inverterAddress_;
  DeviceProfile profile_ = DeviceProfile::SinglePhase;
  uint8_t slaveAddress_ = kDefaultSlaveAddress;
  char adapterSerial_[kAdapterSerialLength + 1] = "AB1234G567";
  char inverterSerial_[kInverterSerialLength + 1]{};

  ClientState state_ = ClientState::Stopped;
  bool enabled_ = false;
  uint32_t lastConnectAttemptMs_ = 0;
  uint32_t requestSentMs_ = 0;
  uint32_t lastFastPollMs_ = 0;
  uint32_t lastSlowPollMs_ = 0;
  uint32_t lastSuccessfulResponseMs_ = 0;
  uint32_t readGeneration_ = 0;
  RegisterBank lastReadBank_ = RegisterBank::Input;
  uint16_t lastReadBaseRegister_ = 0;
  uint16_t lastReadRegisterCount_ = 0;
  uint32_t fastPollIntervalMs_ = 5000;
  uint32_t slowPollIntervalMs_ = 60000;
  uint32_t responseTimeoutMs_ = 1800;
  uint32_t reconnectIntervalMs_ = 5000;

  StreamFramer framer_;
  RegisterStore registers_;
  BatteryData batteries_[kMaximumBatteries]{};
  size_t batteryCount_ = 0;
  size_t batteryCommandsPending_ = 0;
  uint32_t batteryGeneration_ = 0;
  uint32_t lastBatteryRefreshMs_ = 0;
  Command queue_[kQueueSize]{};
  size_t queueHead_ = 0;
  size_t queueTail_ = 0;
  size_t queueCount_ = 0;
  Command current_{};
  bool awaitingResponse_ = false;

  ControlStatus controlStatus_ = ControlStatus::Idle;
  uint8_t controlWritesPending_ = 0;
  uint16_t lastControlRegister_ = 0;
  uint16_t lastControlValue_ = 0;
  char lastError_[96]{};
  ClientStats stats_{};
};

}  // namespace givenergy
