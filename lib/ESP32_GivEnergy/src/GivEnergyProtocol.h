#pragma once

#include <stddef.h>
#include <stdint.h>

namespace givenergy {

constexpr uint16_t kPort = 8899;
constexpr uint8_t kDefaultSlaveAddress = 0x11;
constexpr size_t kAdapterSerialLength = 10;
constexpr size_t kInverterSerialLength = 10;
constexpr size_t kMaxRegistersPerMessage = 60;
constexpr size_t kMaxFrameLength = 300;

enum class MessageType : uint8_t {
  None,
  Heartbeat,
  ReadHolding,
  ReadInput,
  ReadMeterProduct,
  WriteHolding,
  Unknown,
};

struct Message {
  MessageType type = MessageType::None;
  bool error = false;
  uint8_t slaveAddress = 0;
  uint8_t function = 0;
  uint16_t baseRegister = 0;
  uint16_t registerCount = 0;
  uint16_t values[kMaxRegistersPerMessage]{};
  uint16_t check = 0;
  uint8_t adapterType = 0;
  char adapterSerial[kAdapterSerialLength + 1]{};
  char inverterSerial[kInverterSerialLength + 1]{};
};

uint16_t modbusCrc(const uint8_t *data, size_t length);

size_t buildReadRequest(
    uint8_t function,
    uint16_t baseRegister,
    uint16_t registerCount,
    uint8_t slaveAddress,
    const char *adapterSerial,
    uint8_t *output,
    size_t capacity);

size_t buildWriteRequest(
    uint16_t holdingRegister,
    uint16_t value,
    uint8_t slaveAddress,
    const char *adapterSerial,
    uint8_t *output,
    size_t capacity);

size_t buildHeartbeatResponse(
    const char *adapterSerial,
    uint8_t adapterType,
    uint8_t *output,
    size_t capacity);

bool decodeFrame(const uint8_t *frame, size_t length, Message &message);

class StreamFramer {
 public:
  void reset();
  void push(const uint8_t *data, size_t length);
  void push(uint8_t byte);
  bool pop(Message &message);
  size_t bufferedBytes() const { return length_; }
  uint32_t discardedBytes() const { return discardedBytes_; }

 private:
  void discard(size_t count);
  int findMarker(size_t from = 0) const;

  uint8_t buffer_[kMaxFrameLength * 2]{};
  size_t length_ = 0;
  uint32_t discardedBytes_ = 0;
};

}  // namespace givenergy
