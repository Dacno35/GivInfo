#include "GivEnergyClient.h"

#include <string.h>

namespace givenergy {
namespace {

uint8_t functionForBank(RegisterBank bank) {
  switch (bank) {
    case RegisterBank::Holding:
      return 0x03;
    case RegisterBank::Input:
      return 0x04;
    case RegisterBank::MeterProduct:
      return 0x16;
  }
  return 0x04;
}

RegisterBank bankForMessage(MessageType type) {
  switch (type) {
    case MessageType::ReadHolding:
      return RegisterBank::Holding;
    case MessageType::ReadMeterProduct:
      return RegisterBank::MeterProduct;
    case MessageType::ReadInput:
    default:
      return RegisterBank::Input;
  }
}

}  // namespace

GivEnergyClient::GivEnergyClient() {
  registers_.clear();
}

void GivEnergyClient::begin(
    IPAddress inverterAddress,
    DeviceProfile profile,
    uint8_t slaveAddress,
    const char *adapterSerial) {
  stop();
  inverterAddress_ = inverterAddress;
  profile_ = profile;
  slaveAddress_ = slaveAddress;
  registers_.clear();
  for (size_t i = 0; i < kMaximumBatteries; ++i) {
    batteries_[i] = BatteryData{};
  }
  batteryCount_ = 0;
  batteryCommandsPending_ = 0;
  ++batteryGeneration_;
  lastBatteryRefreshMs_ = 0;
  memset(inverterSerial_, 0, sizeof(inverterSerial_));
  memset(adapterSerial_, 0, sizeof(adapterSerial_));
  if (adapterSerial != nullptr) {
    strncpy(adapterSerial_, adapterSerial, kAdapterSerialLength);
  }
  if (adapterSerial_[0] == '\0') {
    memcpy(adapterSerial_, "AB1234G567", kAdapterSerialLength + 1);
  }
  enabled_ = true;
  state_ = ClientState::Connecting;
  lastConnectAttemptMs_ = millis() - reconnectIntervalMs_;
  lastFastPollMs_ = 0;
  lastSlowPollMs_ = 0;
  controlStatus_ = ControlStatus::Idle;
  setError("");
}

void GivEnergyClient::stop() {
  enabled_ = false;
  socket_.stop();
  framer_.reset();
  queueHead_ = 0;
  queueTail_ = 0;
  queueCount_ = 0;
  awaitingResponse_ = false;
  controlWritesPending_ = 0;
  batteryCommandsPending_ = 0;
  state_ = ClientState::Stopped;
}

void GivEnergyClient::setPollIntervals(
    uint32_t fastMilliseconds,
    uint32_t slowMilliseconds) {
  fastPollIntervalMs_ = fastMilliseconds < 1000 ? 1000 : fastMilliseconds;
  slowPollIntervalMs_ =
      slowMilliseconds < fastPollIntervalMs_
          ? fastPollIntervalMs_
          : slowMilliseconds;
}

void GivEnergyClient::setResponseTimeout(uint32_t milliseconds) {
  responseTimeoutMs_ = milliseconds < 250 ? 250 : milliseconds;
}

bool GivEnergyClient::connected() {
  return enabled_ && socket_.connected();
}

void GivEnergyClient::setError(const char *error) {
  memset(lastError_, 0, sizeof(lastError_));
  if (error != nullptr) {
    strncpy(lastError_, error, sizeof(lastError_) - 1);
  }
}

void GivEnergyClient::failControl(const char *error) {
  queueHead_ = 0;
  queueTail_ = 0;
  queueCount_ = 0;
  awaitingResponse_ = false;
  current_ = Command{};
  controlWritesPending_ = 0;
  controlStatus_ = ControlStatus::Failed;
  setError(error);
}

bool GivEnergyClient::connectSocket() {
  if (WiFi.status() != WL_CONNECTED) {
    state_ = ClientState::WifiUnavailable;
    return false;
  }
  if (socket_.connected()) {
    state_ = awaitingResponse_ ? ClientState::WaitingForResponse
                               : ClientState::Online;
    return true;
  }

  const uint32_t now = millis();
  if (now - lastConnectAttemptMs_ < reconnectIntervalMs_) {
    state_ = ClientState::Connecting;
    return false;
  }
  lastConnectAttemptMs_ = now;
  state_ = ClientState::Connecting;

  socket_.stop();
  if (!socket_.connect(inverterAddress_, kPort, 1000)) {
    setError("TCP connect to port 8899 failed");
    return false;
  }

  socket_.setNoDelay(true);
  socket_.setTimeout(1);
  framer_.reset();
  awaitingResponse_ = false;
  state_ = ClientState::Online;
  ++stats_.connections;
  if (stats_.connections > 1) {
    ++stats_.reconnects;
  }
  setError("");
  requestRefresh(true);
  return true;
}

void GivEnergyClient::disconnectSocket(const char *reason) {
  if (awaitingResponse_ &&
      current_.kind == CommandKind::BatteryRead) {
    finishBatteryCommand();
  }
  if (awaitingResponse_ &&
      (current_.kind == CommandKind::Write ||
       current_.kind == CommandKind::VerifyWrite)) {
    failControl(
        reason != nullptr && reason[0] != '\0'
            ? reason
            : "Inverter connection lost during battery control");
  }
  socket_.stop();
  framer_.reset();
  awaitingResponse_ = false;
  state_ = enabled_ ? ClientState::Connecting : ClientState::Stopped;
  setError(reason);
}

size_t GivEnergyClient::queuedCount() const {
  return queueCount_;
}

size_t GivEnergyClient::queueFree() const {
  return kQueueSize - queueCount_;
}

bool GivEnergyClient::enqueue(const Command &command) {
  if (queueCount_ >= kQueueSize) {
    setError("Command queue full");
    return false;
  }
  queue_[queueTail_] = command;
  queueTail_ = (queueTail_ + 1) % kQueueSize;
  ++queueCount_;
  return true;
}

bool GivEnergyClient::dequeue(Command &command) {
  if (queueCount_ == 0) {
    return false;
  }
  command = queue_[queueHead_];
  queueHead_ = (queueHead_ + 1) % kQueueSize;
  --queueCount_;
  return true;
}

bool GivEnergyClient::requestRegisters(
    RegisterBank bank,
    uint16_t baseRegister,
    uint16_t count,
    uint8_t slaveAddress) {
  if (count == 0 || count > kMaxRegistersPerMessage) {
    setError("Read count must be between 1 and 60");
    return false;
  }
  Command command;
  command.kind = CommandKind::Read;
  command.bank = bank;
  command.address = baseRegister;
  command.countOrValue = count;
  command.slaveAddress = slaveAddress == 0 ? slaveAddress_ : slaveAddress;
  command.retriesRemaining = 1;
  return enqueue(command);
}

bool GivEnergyClient::enqueueBatteryRead(
    BatteryTelemetryFormat format,
    uint8_t slaveAddress,
    uint16_t baseRegister) {
  Command command;
  command.kind = CommandKind::BatteryRead;
  command.bank = RegisterBank::Input;
  command.address = baseRegister;
  command.countOrValue = 60;
  command.slaveAddress = slaveAddress;
  command.retriesRemaining = 0;
  command.batteryFormat = format;
  if (!enqueue(command)) {
    return false;
  }
  ++batteryCommandsPending_;
  return true;
}

bool GivEnergyClient::requestBatteryRefresh(bool rediscover) {
  if (profile_ == DeviceProfile::Ems ||
      controlWritesPending_ > 0 ||
      batteryCommandsPending_ > 0) {
    return false;
  }

  const bool discover = rediscover || batteryCount_ == 0;
  const size_t required =
      discover ? kMaximumBatteries : batteryCount_;
  if (required == 0 || queueFree() < required) {
    setError("Not enough command queue space for battery refresh");
    return false;
  }

  if (!discover) {
    for (size_t i = 0; i < batteryCount_; ++i) {
      if (!enqueueBatteryRead(
              batteries_[i].format,
              batteries_[i].slaveAddress,
              batteries_[i].baseRegister)) {
        return false;
      }
    }
    return true;
  }

  for (size_t i = 0; i < kMaximumBatteries; ++i) {
    batteries_[i] = BatteryData{};
  }
  batteryCount_ = 0;
  ++batteryGeneration_;

  const bool highVoltage =
      profile_ == DeviceProfile::ThreePhase;
  for (size_t i = 0; i < kMaximumBatteries; ++i) {
    const BatteryTelemetryFormat format =
        highVoltage
            ? BatteryTelemetryFormat::HighVoltage24
            : BatteryTelemetryFormat::LowVoltage16;
    const uint8_t address = static_cast<uint8_t>(
        (highVoltage ? 0x50U : 0x32U) + i);
    if (!enqueueBatteryRead(format, address, 60)) {
      return false;
    }
  }
  return true;
}

bool GivEnergyClient::enqueuePollBlocks(
    const PollBlock *blocks,
    size_t count) {
  if (blocks == nullptr || queueFree() < count) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!requestRegisters(blocks[i].bank, blocks[i].base, blocks[i].count)) {
      return false;
    }
  }
  return true;
}

bool GivEnergyClient::requestRefresh(bool includeSlowRegisters) {
  size_t fastCount = 0;
  const PollBlock *fast = fastPollBlocks(profile_, fastCount);
  size_t slowCount = 0;
  const PollBlock *slow = slowPollBlocks(profile_, slowCount);
  const size_t required = fastCount + (includeSlowRegisters ? slowCount : 0);
  if (queueFree() < required) {
    setError("Not enough command queue space for refresh");
    return false;
  }
  if (!enqueuePollBlocks(fast, fastCount)) {
    return false;
  }
  if (includeSlowRegisters && !enqueuePollBlocks(slow, slowCount)) {
    return false;
  }
  return true;
}

bool GivEnergyClient::enqueueControlCommand(const ControlCommand &command) {
  if (controlWritesPending_ > 0) {
    setError("A battery control is already active");
    controlStatus_ = ControlStatus::Rejected;
    return false;
  }

  if (command.count == 0 || command.count > kQueueSize) {
    setError("Not enough command queue space for control");
    controlStatus_ = ControlStatus::Failed;
    return false;
  }
  for (uint8_t i = 0; i < command.count; ++i) {
    const RegisterWrite &write = command.writes[i];
    if (!isWhitelistedWrite(profile_, write.address, write.value)) {
      setError("Control write rejected by register whitelist");
      controlStatus_ = ControlStatus::Rejected;
      return false;
    }
  }

  // Discard queued background reads so a requested control follows only the
  // read already in flight. Fresh polling resumes after verification.
  queueHead_ = 0;
  queueTail_ = 0;
  queueCount_ = 0;
  batteryCommandsPending_ =
      awaitingResponse_ &&
              current_.kind == CommandKind::BatteryRead
          ? 1
          : 0;

  for (uint8_t i = 0; i < command.count; ++i) {
    Command queued;
    queued.kind = CommandKind::Write;
    queued.bank = RegisterBank::Holding;
    queued.address = command.writes[i].address;
    queued.countOrValue = command.writes[i].value;
    queued.slaveAddress = slaveAddress_;
    queued.retriesRemaining = 1;
    enqueue(queued);
  }
  controlWritesPending_ =
      static_cast<uint8_t>(controlWritesPending_ + command.count);
  controlStatus_ = ControlStatus::Queued;
  setError("");
  return true;
}

ControlValidation GivEnergyClient::control(ControlId id, uint16_t value) {
  ControlCommand command;
  const ControlValidation validation =
      makeControlCommand(profile_, id, value, command);
  if (validation != ControlValidation::Ok) {
    controlStatus_ = ControlStatus::Rejected;
    setError(validation == ControlValidation::ValueOutOfRange
                 ? "Control value outside its allowed range"
                 : "Control is unsupported for this device profile");
    return validation;
  }
  if (!enqueueControlCommand(command)) {
    return ControlValidation::UnsupportedForProfile;
  }
  return ControlValidation::Ok;
}

ControlValidation GivEnergyClient::setTimeSlot(
    bool discharge,
    uint8_t slot,
    uint8_t startHour,
    uint8_t startMinute,
    uint8_t endHour,
    uint8_t endMinute) {
  ControlCommand command;
  const ControlValidation validation = makeTimeSlotCommand(
      profile_,
      discharge,
      slot,
      startHour,
      startMinute,
      endHour,
      endMinute,
      command);
  if (validation != ControlValidation::Ok) {
    controlStatus_ = ControlStatus::Rejected;
    setError(validation == ControlValidation::ValueOutOfRange
                 ? "Invalid time or slot number"
                 : "Time slot unsupported for this device profile");
    return validation;
  }
  if (!enqueueControlCommand(command)) {
    return ControlValidation::UnsupportedForProfile;
  }
  return ControlValidation::Ok;
}

bool GivEnergyClient::sendCurrent() {
  uint8_t frame[kMaxFrameLength]{};
  size_t length = 0;
  if (current_.kind == CommandKind::Write) {
    length = buildWriteRequest(
        current_.address,
        current_.countOrValue,
        current_.slaveAddress,
        adapterSerial_,
        frame,
        sizeof(frame));
    Serial.print("[CONTROL] Writing HR");
    Serial.print(current_.address);
    Serial.print(" = ");
    Serial.print(current_.countOrValue);
    Serial.print(" slave 0x");
    Serial.println(current_.slaveAddress, HEX);
    controlStatus_ = ControlStatus::Sent;
    lastControlRegister_ = current_.address;
    lastControlValue_ = current_.countOrValue;
  } else {
    length = buildReadRequest(
        functionForBank(current_.bank),
        current_.address,
        current_.kind == CommandKind::VerifyWrite ? 1
                                                  : current_.countOrValue,
        current_.slaveAddress,
        adapterSerial_,
        frame,
        sizeof(frame));
  }

  if (length == 0 || socket_.write(frame, length) != length) {
    if (current_.kind == CommandKind::BatteryRead) {
      finishBatteryCommand();
    }
    if (current_.kind == CommandKind::Write ||
        current_.kind == CommandKind::VerifyWrite ||
        controlWritesPending_ > 0) {
      failControl("TCP write failed during battery control");
    }
    disconnectSocket("TCP write failed");
    return false;
  }
  requestSentMs_ = millis();
  awaitingResponse_ = true;
  state_ = ClientState::WaitingForResponse;
  ++stats_.requestsSent;
  return true;
}

bool GivEnergyClient::responseMatchesCurrent(const Message &message) const {
  if (!awaitingResponse_ || message.slaveAddress != current_.slaveAddress ||
      message.baseRegister != current_.address) {
    return false;
  }
  if (current_.kind == CommandKind::Write) {
    return message.type == MessageType::WriteHolding;
  }
  const MessageType expected =
      current_.bank == RegisterBank::Holding
          ? MessageType::ReadHolding
          : (current_.bank == RegisterBank::MeterProduct
                 ? MessageType::ReadMeterProduct
                 : MessageType::ReadInput);
  const uint16_t expectedCount =
      current_.kind == CommandKind::VerifyWrite ? 1 : current_.countOrValue;
  return message.type == expected && message.registerCount == expectedCount;
}

void GivEnergyClient::finishBatteryCommand() {
  if (batteryCommandsPending_ > 0) {
    --batteryCommandsPending_;
  }
  if (batteryCommandsPending_ == 0) {
    lastBatteryRefreshMs_ = millis();
    ++batteryGeneration_;
  }
}

void GivEnergyClient::handleBatteryMessage(
    const Message &message) {
  BatteryData decoded;
  if (decodeBatteryData(
          current_.batteryFormat,
          current_.slaveAddress,
          current_.address,
          message.values,
          message.registerCount,
          decoded)) {
    size_t index = batteryCount_;
    for (size_t i = 0; i < batteryCount_; ++i) {
      if (batteries_[i].slaveAddress ==
              decoded.slaveAddress &&
          batteries_[i].baseRegister ==
              decoded.baseRegister) {
        index = i;
        break;
      }
    }
    if (index < kMaximumBatteries) {
      batteries_[index] = decoded;
      if (index == batteryCount_) {
        ++batteryCount_;
      }
      ++batteryGeneration_;
    }
  }

  finishBatteryCommand();
}

void GivEnergyClient::handleMessage(const Message &message) {
  if (message.type == MessageType::Heartbeat) {
    uint8_t response[32]{};
    const size_t length = buildHeartbeatResponse(
        adapterSerial_,
        message.adapterType,
        response,
        sizeof(response));
    if (length > 0) {
      socket_.write(response, length);
    }
    return;
  }

  ++stats_.responsesReceived;
  lastSuccessfulResponseMs_ = millis();
  if (message.inverterSerial[0] != '\0') {
    memcpy(inverterSerial_, message.inverterSerial, sizeof(inverterSerial_));
  }

  if (!responseMatchesCurrent(message)) {
    ++stats_.protocolErrors;
    return;
  }
  if (message.error) {
    if (current_.kind == CommandKind::BatteryRead) {
      awaitingResponse_ = false;
      state_ = ClientState::Online;
      finishBatteryCommand();
    } else if (current_.kind == CommandKind::Read) {
      awaitingResponse_ = false;
      setError("Inverter returned a Modbus exception");
    } else {
      failControl(
          "Inverter returned a Modbus exception during battery control");
    }
    return;
  }

  if (current_.kind == CommandKind::BatteryRead) {
    handleBatteryMessage(message);
    awaitingResponse_ = false;
    state_ = ClientState::Online;
    return;
  }

  if (message.type == MessageType::ReadHolding ||
      message.type == MessageType::ReadInput ||
      message.type == MessageType::ReadMeterProduct) {
    const RegisterBank bank = bankForMessage(message.type);
    for (uint16_t i = 0; i < message.registerCount; ++i) {
      registers_.set(
          bank,
          static_cast<uint16_t>(message.baseRegister + i),
          message.values[i]);
    }
    stats_.registerUpdates += message.registerCount;
    lastReadBank_ = bank;
    lastReadBaseRegister_ = message.baseRegister;
    lastReadRegisterCount_ = message.registerCount;
    ++readGeneration_;
  }

  if (current_.kind == CommandKind::Write) {
    if (message.values[0] != current_.countOrValue) {
      failControl(
          "Battery control write response did not match");
      return;
    }
    const uint16_t address = current_.address;
    const uint16_t expected = current_.countOrValue;
    const uint8_t slave = current_.slaveAddress;
    current_ = Command{};
    current_.kind = CommandKind::VerifyWrite;
    current_.bank = RegisterBank::Holding;
    current_.address = address;
    current_.countOrValue = 1;
    current_.expectedValue = expected;
    current_.slaveAddress = slave;
    current_.retriesRemaining = 1;
    awaitingResponse_ = false;
    sendCurrent();
    return;
  }

  if (current_.kind == CommandKind::VerifyWrite) {
    if (message.registerCount == 1 &&
        message.values[0] == current_.expectedValue) {
      if (controlWritesPending_ > 0) {
        --controlWritesPending_;
      }
      controlStatus_ = controlWritesPending_ == 0
                           ? ControlStatus::Verified
                           : ControlStatus::Sent;
      lastControlRegister_ = current_.address;
      lastControlValue_ = current_.expectedValue;
      ++stats_.writesVerified;
      setError("");
    } else {
      failControl(
          "Battery control read-back verification failed");
      return;
    }
  }

  awaitingResponse_ = false;
  state_ = ClientState::Online;
}

void GivEnergyClient::receiveMessages() {
  uint8_t chunk[128];
  size_t totalRead = 0;
  while (socket_.available() > 0 && totalRead < 512) {
    const int available = socket_.available();
    const size_t wanted =
        static_cast<size_t>(available) < sizeof(chunk)
            ? static_cast<size_t>(available)
            : sizeof(chunk);
    const int read = socket_.read(chunk, wanted);
    if (read <= 0) {
      break;
    }
    framer_.push(chunk, static_cast<size_t>(read));
    totalRead += static_cast<size_t>(read);
  }

  Message message;
  while (framer_.pop(message)) {
    handleMessage(message);
  }
}

void GivEnergyClient::handleTimeout() {
  ++stats_.timeouts;
  if (current_.kind == CommandKind::BatteryRead) {
    awaitingResponse_ = false;
    finishBatteryCommand();
    if (socket_.connected()) {
      state_ = ClientState::Online;
    } else {
      disconnectSocket("Battery telemetry connection closed");
    }
    return;
  }
  if (current_.retriesRemaining > 0) {
    --current_.retriesRemaining;
    awaitingResponse_ = false;
    sendCurrent();
    return;
  }
  if (current_.kind != CommandKind::Read) {
    failControl(
        "Timed out waiting for battery control response");
    disconnectSocket(
        "Timed out waiting for battery control response");
    return;
  }
  disconnectSocket("Timed out waiting for inverter response");
}

void GivEnergyClient::update() {
  if (!enabled_) {
    state_ = ClientState::Stopped;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (socket_.connected()) {
      socket_.stop();
    }
    if (awaitingResponse_ &&
        current_.kind == CommandKind::BatteryRead) {
      finishBatteryCommand();
    }
    if (awaitingResponse_ &&
        (current_.kind == CommandKind::Write ||
         current_.kind == CommandKind::VerifyWrite)) {
      failControl(
          "Wi-Fi disconnected during battery control");
    }
    awaitingResponse_ = false;
    state_ = ClientState::WifiUnavailable;
    setError("Wi-Fi is not connected");
    return;
  }
  if (!connectSocket()) {
    return;
  }

  receiveMessages();
  if (!socket_.connected()) {
    disconnectSocket("Inverter closed the TCP connection");
    return;
  }

  const uint32_t now = millis();
  if (awaitingResponse_) {
    if (now - requestSentMs_ >= responseTimeoutMs_) {
      handleTimeout();
    }
    return;
  }

  if (queuedCount() == 0) {
    if (lastFastPollMs_ == 0 ||
        now - lastFastPollMs_ >= fastPollIntervalMs_) {
      size_t count = 0;
      const PollBlock *blocks = fastPollBlocks(profile_, count);
      if (enqueuePollBlocks(blocks, count)) {
        lastFastPollMs_ = now;
      }
    }
    if (lastSlowPollMs_ == 0 ||
        now - lastSlowPollMs_ >= slowPollIntervalMs_) {
      size_t count = 0;
      const PollBlock *blocks = slowPollBlocks(profile_, count);
      if (enqueuePollBlocks(blocks, count)) {
        lastSlowPollMs_ = now;
      }
    }
  }

  if (dequeue(current_)) {
    sendCurrent();
  } else {
    state_ = ClientState::Online;
  }
}

}  // namespace givenergy
