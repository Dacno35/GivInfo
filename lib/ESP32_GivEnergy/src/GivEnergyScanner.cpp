#include "GivEnergyScanner.h"

#include <string.h>
//provides C string functions such as strncpy()
namespace givenergy {
//Places everything inside the givenergy namespace. The full class nams is therefore
//givenergy::GivEnergyScanner
namespace {

//constexpr means the values are known at compile time and cannot change.
//uint8_t means each value is an unsigned 8-bit integer
constexpr uint8_t kDiscoverySlaveAddresses[] = {
  //Creates a fixed array containing the Modbus slave addresses the scanner wil try

    0x11,
    0x31,//the two hex values
};// the two accepted slave addresses 
constexpr size_t kDiscoverySlaveCount =
    sizeof(kDiscoverySlaveAddresses) / //the size of the entire array 
    sizeof(kDiscoverySlaveAddresses[0]); //the size of one item 
    //calculates how many items are in the array.
constexpr uint32_t kConnectTimeoutMs = 500; //allows up to 500 milliseconds for establishing a TCP connection to an IP address
constexpr uint32_t kReplyTimeoutMs = 5000;
//Allows up to 5 seconds for the inverter to return a Modbus reply
}  // namespace

constexpr uint8_t kSkippedHosts[] = {
    133,
    157,
    161,
    169
};

bool shouldSkipAddress(const IPAddress &address)
{
    // Only skip these addresses on 192.168.1.x
    if (address[0] != 192 ||
        address[1] != 168 ||
        address[2] != 1) {
        return false;
    }

    for (uint8_t skippedHost : kSkippedHosts) {
        if (address[3] == skippedHost) {
            return true;
        }
    }

    return false;
}

void GivEnergyScanner::start(uint8_t firstHost, uint8_t lastHost) {

  cancel();
  if (WiFi.status() != WL_CONNECTED || firstHost == 0 ||
      lastHost < firstHost) {
    state_ = State::Failed;
    return;
  }

  const IPAddress local = WiFi.localIP();
  networkBase_ = IPAddress(local[0], local[1], local[2], 0);
  firstHost_ = firstHost;
  lastHost_ = lastHost;
  currentHost_ = firstHost_;
  currentSlaveIndex_ = 0;
  hostConnected_ = false;
  foundCount_ = 0;
  probeState_ = ProbeState::StartHost;
  state_ = State::Scanning;
}

void GivEnergyScanner::startAddress(IPAddress address) {
  cancel();
  if (WiFi.status() != WL_CONNECTED ||
      address[0] == 0 ||
      address[0] >= 224 ||
      address[3] == 0 ||
      address[3] == 255) {
    state_ = State::Failed;
    return;
  }

  networkBase_ = IPAddress(
      address[0],
      address[1],
      address[2],
      0);
  firstHost_ = address[3];
  lastHost_ = address[3];
  currentHost_ = address[3];
  currentSlaveIndex_ = 0;
  hostConnected_ = false;
  foundCount_ = 0;
  probeState_ = ProbeState::StartHost;
  state_ = State::Scanning;
}

void GivEnergyScanner::cancel() {
  probe_.stop();
  framer_.reset();
  state_ = State::Idle;
  probeState_ = ProbeState::StartHost;
  currentSlaveIndex_ = 0;
  hostConnected_ = false;
}

IPAddress GivEnergyScanner::currentAddress() const {
  return IPAddress(
      networkBase_[0],
      networkBase_[1],
      networkBase_[2],
      currentHost_);
}

uint8_t GivEnergyScanner::currentSlaveAddress() const {
  return currentSlaveIndex_ < kDiscoverySlaveCount
             ? kDiscoverySlaveAddresses[currentSlaveIndex_]
             : 0;
}

IPAddress GivEnergyScanner::foundAddress(size_t index) const {
  return index < foundCount_ ? foundAddresses_[index]
                             : IPAddress(0, 0, 0, 0);
}

const char *GivEnergyScanner::foundSerial(size_t index) const {
  return index < foundCount_ ? foundIdentities_[index].serial : "";
}

uint8_t GivEnergyScanner::foundSlaveAddress(size_t index) const {
  return index < foundCount_
             ? foundIdentities_[index].slaveAddress
             : 0;
}

bool GivEnergyScanner::foundIdentityValid(size_t index) const {
  return index < foundCount_ &&
         foundIdentities_[index].identityValid;
}

bool GivEnergyScanner::foundProfileKnown(size_t index) const {
  return index < foundCount_ &&
         foundIdentities_[index].profileKnown;
}

uint16_t GivEnergyScanner::foundModelCode(size_t index) const {
  return index < foundCount_
             ? foundIdentities_[index].modelCode
             : 0;
}

DeviceModel GivEnergyScanner::foundModel(size_t index) const {
  return index < foundCount_
             ? foundIdentities_[index].model
             : DeviceModel::Unknown;
}

DeviceProfile GivEnergyScanner::foundProfile(size_t index) const {
  return index < foundCount_
             ? foundIdentities_[index].profile
             : DeviceProfile::SinglePhase;
}

void GivEnergyScanner::advance() {
  if (currentHost_ >= lastHost_ || foundCount_ >= kMaximumResults) {
    probe_.stop();
    state_ = State::Finished;
    return;
  }
  ++currentHost_;
  currentSlaveIndex_ = 0;
  probeState_ = ProbeState::StartHost;
}

void GivEnergyScanner::finishProbe(
    bool found,
    const DeviceIdentity *identity) {
  if (found && foundCount_ < kMaximumResults) {
    foundAddresses_[foundCount_] = IPAddress(
        networkBase_[0],
        networkBase_[1],
        networkBase_[2],
        currentHost_);
    if (identity != nullptr) {
      foundIdentities_[foundCount_] = *identity;
    } else {
      foundIdentities_[foundCount_] = DeviceIdentity{};
      strncpy(
          foundIdentities_[foundCount_].serial,
          "Unknown",
          kInverterSerialLength);
      foundIdentities_[foundCount_]
          .serial[kInverterSerialLength] = '\0';
    }
    ++foundCount_;
  }
  probe_.stop();
  framer_.reset();
  advance();
}

bool GivEnergyScanner::startCurrentSlaveProbe() {
  const IPAddress candidate(
      networkBase_[0],
      networkBase_[1],
      networkBase_[2],
      currentHost_);

  if (shouldSkipAddress(candidate)) {
      Serial.print("[Scanner] Skipping protected inverter: ");
      Serial.println(candidate);
      return false;
  }

  if (!probe_.connected()) {
    probe_.stop();
    if (!probe_.connect(candidate, kPort, kConnectTimeoutMs)) {
      return false;
    }
    probe_.setNoDelay(true);
    probe_.setTimeout(1);
  }

  // Remember that this host accepted a TCP connection even if neither
  // register request returns a usable serial number.
  hostConnected_ = true;

  uint8_t request[40]{};
  const size_t length = buildReadRequest(
      0x03,
      0,
      60,
      currentSlaveAddress(),
      "AB1234G567",
      request,
      sizeof(request));

  if (length == 0 || probe_.write(request, length) != length) {
    return false;
  }

  probeStartedMs_ = millis();
  probeState_ = ProbeState::WaitingForReply;
  return true;
}

void GivEnergyScanner::tryNextSlaveOrFinish() {
  if (static_cast<size_t>(currentSlaveIndex_) + 1U >=
      kDiscoverySlaveCount) {
    finishProbe(hostConnected_);
    return;
  }

  ++currentSlaveIndex_;
  framer_.reset();

  // Reuse the existing port-8899 connection whenever it is still alive.
  // Reconnect only if the inverter closed it after the first probe.
  if (!startCurrentSlaveProbe()) {
    finishProbe(hostConnected_);
  }
}

void GivEnergyScanner::update() {
  if (state_ != State::Scanning) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    probe_.stop();
    state_ = State::Failed;
    return;
  }

  if (probeState_ == ProbeState::StartHost) {
    probe_.stop();
    currentSlaveIndex_ = 0;
    hostConnected_ = false;
    if (!startCurrentSlaveProbe()) {
      finishProbe(hostConnected_);
      return;
    }
    return;
  }

  uint8_t chunk[96];
  while (probe_.available() > 0) {
    const int available = probe_.available();
    const size_t wanted =
        static_cast<size_t>(available) < sizeof(chunk)
            ? static_cast<size_t>(available)
            : sizeof(chunk);
    const int read = probe_.read(chunk, wanted);
    if (read <= 0) break;
    framer_.push(chunk, static_cast<size_t>(read));
  }

  Message message;
  while (framer_.pop(message)) {
    if (message.type == MessageType::Heartbeat) {
      uint8_t response[32]{};
      const size_t length = buildHeartbeatResponse(
          "AB1234G567",
          message.adapterType,
          response,
          sizeof(response));
      probe_.write(response, length);
      continue;
    }
    if (message.type == MessageType::ReadHolding &&
        message.baseRegister == 0 &&
        message.slaveAddress == currentSlaveAddress()) {
      DeviceIdentity identity;
      if (decodeDeviceIdentity(message, identity)) {
        finishProbe(true, &identity);
        return;
      }

      tryNextSlaveOrFinish();
      return;
    }
  }

  if (!probe_.connected() ||
      millis() - probeStartedMs_ >= kReplyTimeoutMs) {
    tryNextSlaveOrFinish();
  }
}



}  // namespace givenergy
