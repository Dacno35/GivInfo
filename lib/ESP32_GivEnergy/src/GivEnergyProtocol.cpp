#include "GivEnergyProtocol.h"

#include <string.h>

namespace givenergy {
namespace {

constexpr uint8_t kHeaderMarker[] = {0x59, 0x59, 0x00, 0x01};
//Every valid GivEnergy packet is expected to begin with hese four bytes
constexpr size_t kRequestFrameLength = 34;
constexpr size_t kHeartbeatFrameLength = 19;
//These define the exact number of bytes used by:
//Read/write requests: 34 bytes
// Heartbeat messages: 19 bytes
//since they're in the unnamed namespace they are only accessible within this .cpp file

//heartbeat message tells the inverter's wifi adapter that the ESP32 is still connected and responding
//This is so that the adapter doesn't assume the connection is dead and close port 8899
uint16_t readBe16(const uint8_t *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
//This combines two bytes into one 16-bit value (its in hex)
//e.g. p[0] = 0x12, p[1] = 0x34 
//The first byte is shifted left/: 0x1200 | 0x34 = 0x1234
//"BE" means big-endian: the most significant byte comes first
void writeBe16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value >> 8);
  p[1] = static_cast<uint8_t>(value & 0xFF);
}
//This does the reverse. It breaks it down into the previous 2 values
//writeBe16(output + 28, holdingRegister);
//This means write the two bytes starting at positions 28 and 29
void writeSerial(uint8_t *destination, const char *serial) {
  //First, all ten serial positions are filled with *
  memset(destination, '*', kAdapterSerialLength);
  //If the serial pointer is invalid, the function leaves the starts in place:
  if (serial == nullptr) {
    return;
  }
  //if the serial pointer is invalid, the function leaves the stars in place
  const size_t length = strlen(serial);
  //It finds the string length 
  const size_t copyLength =
      length < kAdapterSerialLength ? length : kAdapterSerialLength;
      //It copies a maximum of ten characters
      //the ? and : are ternary conditional operators. A compact if/else
      //Its means Is length smaller than kAdapterSerialLength
      //Yes -> use length
      //No -> use kAdapterSerialLength
  const char *start = serial + (length - copyLength);
  //If the serial is longer than ten characters, the final ten are used.
  memcpy(destination + (kAdapterSerialLength - copyLength), start, copyLength);
  //The serial is right-aligned
  //e.g. Input: ABC Output: *******ABC
}

void readSerial(char *destination, const uint8_t *source, size_t length) {
  memcpy(destination, source, length);
  destination[length] = '\0';
}
//This copies the serial bytes from a received packet and then adds the terminating \0
//required by a C string

void writeOuterHeader(uint8_t *output, uint16_t innerLength, uint8_t function) {
  output[0] = 0x59;
  output[1] = 0x59;
  output[2] = 0x00;
  output[3] = 0x01;
  writeBe16(output + 4, static_cast<uint16_t>(innerLength + 2));
  output[6] = 0x01;
  output[7] = function;
}
//These are the packet marker bytes.
//The packet length is written into bytes 4-5
//Then the outer function distinguishes packet types
//For a normal request: writeOuterHeader(output, 26, 0x02);
//The length field becomes 26 + 2 = 28
//The total packet is: 6 header bytes + 28 = 34 bytes
MessageType typeFromFunction(uint8_t function) {
  //This converts numerical protocol functions into your readable enum
  switch (function) {
    case 0x03:
      return MessageType::ReadHolding;
    case 0x04:
      return MessageType::ReadInput;
    case 0x06:
      return MessageType::WriteHolding;
    case 0x16:
      return MessageType::ReadMeterProduct;
    default:
      return MessageType::Unknown;
  }
}
//The outer function distinguishes packet types
}  // namespace

uint16_t modbusCrc(const uint8_t *data, size_t length) {
  //The CRC is an error-detection value calculated from the Modbus portion of the packet

  uint16_t crc = 0xFFFF;
  //Each data byte is mixed into the CRC:
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    //Each byte is processed one bit at a time
    for (uint8_t bit = 0; bit < 8; ++bit) {
      //The least significant bit is checked
      if ((crc & 0x0001U) != 0U) {
        //of it is 1, the CRC shifts and applies the Modbus polynomial
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001U);
        //otherwise it only shifts by one 
      } else {
        crc >>= 1;
      //otherwise it only shifts by one 
      }
    }
  }
  return crc;
}
//when added to a packet, Modbus CRC uses low byte first
size_t buildReadRequest(
    uint8_t function,// 0x03, 0x04 or 0x16
    uint16_t baseRegister,//first register
    uint16_t registerCount,//number to read
    uint8_t slaveAddress,//usually 0x11 or 0x31
    const char *adapterSerial,//serial placed in packet
    uint8_t *output,//destination byte array
    size_t capacity) {//size of that array 
  if (output == nullptr || capacity < kRequestFrameLength ||
      registerCount == 0 || registerCount > kMaxRegistersPerMessage ||
      (function != 0x03 && function != 0x04 && function != 0x16)) {
    return 0;
  }//first, it validates everything 

  memset(output, 0, kRequestFrameLength);
  writeOuterHeader(output, 26, 0x02);
  writeSerial(output + 8, adapterSerial);
  output[25] = 0x08;
  output[26] = slaveAddress;
  output[27] = function;
  writeBe16(output + 28, baseRegister);
  writeBe16(output + 30, registerCount);

  const uint16_t crc = modbusCrc(output + 26, 6);
  output[32] = static_cast<uint8_t>(crc & 0xFF);
  output[33] = static_cast<uint8_t>(crc >> 8);
  return kRequestFrameLength;
}

size_t buildWriteRequest(
    uint16_t holdingRegister,
    uint16_t value,
    uint8_t slaveAddress,
    const char *adapterSerial,
    uint8_t *output,
    size_t capacity) {
  if (output == nullptr || capacity < kRequestFrameLength) {
    return 0;
  }

  memset(output, 0, kRequestFrameLength);
  writeOuterHeader(output, 26, 0x02);
  writeSerial(output + 8, adapterSerial);
  output[25] = 0x08;
  output[26] = slaveAddress;
  output[27] = 0x06;
  writeBe16(output + 28, holdingRegister);
  writeBe16(output + 30, value);

  const uint16_t crc = modbusCrc(output + 26, 6);
  output[32] = static_cast<uint8_t>(crc & 0xFF);
  output[33] = static_cast<uint8_t>(crc >> 8);
  return kRequestFrameLength;
}

size_t buildHeartbeatResponse(
    const char *adapterSerial,
    uint8_t adapterType,
    uint8_t *output,
    size_t capacity) {
  if (output == nullptr || capacity < kHeartbeatFrameLength) {
    return 0;
  }

  memset(output, 0, kHeartbeatFrameLength);
  writeOuterHeader(output, 11, 0x01);
  writeSerial(output + 8, adapterSerial);
  output[18] = adapterType;
  return kHeartbeatFrameLength;
}

bool decodeFrame(const uint8_t *frame, size_t length, Message &message) {
  message = Message{};
  if (frame == nullptr || length < kHeartbeatFrameLength ||
      memcmp(frame, kHeaderMarker, sizeof(kHeaderMarker)) != 0) {
    return false;
  }

  const uint16_t headerLength = readBe16(frame + 4);
  if (headerLength > kMaxFrameLength || length != 6U + headerLength ||
      (frame[6] != 0x00 && frame[6] != 0x01)) {
    return false;
  }

  const uint8_t outerFunction = frame[7];
  if (outerFunction == 0x01) {
    if (length != kHeartbeatFrameLength) {
      return false;
    }
    message.type = MessageType::Heartbeat;
    readSerial(message.adapterSerial, frame + 8, kAdapterSerialLength);
    message.adapterType = frame[18];
    return true;
  }

  if (outerFunction != 0x02 || length < 44) {
    return false;
  }

  readSerial(message.adapterSerial, frame + 8, kAdapterSerialLength);
  message.slaveAddress = frame[26];
  uint8_t transparentFunction = frame[27];
  message.error = (transparentFunction & 0x80U) != 0U;
  transparentFunction &= 0x7FU;
  message.function = transparentFunction;
  message.type = typeFromFunction(transparentFunction);
  readSerial(message.inverterSerial, frame + 28, kInverterSerialLength);

  if (message.type == MessageType::ReadHolding ||
      message.type == MessageType::ReadInput ||
      message.type == MessageType::ReadMeterProduct) {
    message.baseRegister = readBe16(frame + 38);
    message.registerCount = readBe16(frame + 40);

    const size_t valueCount = message.error ? 0 : message.registerCount;
    if (valueCount > kMaxRegistersPerMessage) {
      return false;
    }
    const size_t expectedLength = 44U + valueCount * 2U;
    if (length != expectedLength) {
      return false;
    }
    for (size_t i = 0; i < valueCount; ++i) {
      message.values[i] = readBe16(frame + 42 + i * 2);
    }
    message.check = readBe16(frame + length - 2);
    return true;
  }

  if (message.type == MessageType::WriteHolding) {
    if (length != 44) {
      return false;
    }
    message.baseRegister = readBe16(frame + 38);
    message.registerCount = 1;
    message.values[0] = readBe16(frame + 40);
    message.check = readBe16(frame + 42);
    return true;
  }

  return false;
}

void StreamFramer::reset() {
  length_ = 0;
  discardedBytes_ = 0;
}

void StreamFramer::discard(size_t count) {
  if (count >= length_) {
    discardedBytes_ += static_cast<uint32_t>(length_);
    length_ = 0;
    return;
  }
  memmove(buffer_, buffer_ + count, length_ - count);
  length_ -= count;
  discardedBytes_ += static_cast<uint32_t>(count);
}

int StreamFramer::findMarker(size_t from) const {
  if (length_ < sizeof(kHeaderMarker) || from >= length_) {
    return -1;
  }
  for (size_t i = from; i + sizeof(kHeaderMarker) <= length_; ++i) {
    if (memcmp(buffer_ + i, kHeaderMarker, sizeof(kHeaderMarker)) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void StreamFramer::push(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0) {
    return;
  }

  if (length > sizeof(buffer_)) {
    data += length - sizeof(buffer_);
    discardedBytes_ += static_cast<uint32_t>(length - sizeof(buffer_));
    length = sizeof(buffer_);
    length_ = 0;
  }

  if (length_ + length > sizeof(buffer_)) {
    discard(length_ + length - sizeof(buffer_));
  }
  memcpy(buffer_ + length_, data, length);
  length_ += length;
}

void StreamFramer::push(uint8_t byte) {
  push(&byte, 1);
}

bool StreamFramer::pop(Message &message) {
  while (length_ >= kHeartbeatFrameLength) {
    const int marker = findMarker();
    if (marker < 0) {
      const size_t keep = length_ < 3 ? length_ : 3;
      const size_t remove = length_ - keep;
      discard(remove);
      return false;
    }
    if (marker > 0) {
      discard(static_cast<size_t>(marker));
      continue;
    }

    const uint16_t headerLength = readBe16(buffer_ + 4);
    const uint8_t unitId = buffer_[6];
    const uint8_t function = buffer_[7];
    if (headerLength > kMaxFrameLength ||
        (unitId != 0x00 && unitId != 0x01) ||
        (function != 0x01 && function != 0x02)) {
      discard(4);
      continue;
    }

    const size_t frameLength = 6U + headerLength;
    if (frameLength > sizeof(buffer_)) {
      discard(4);
      continue;
    }
    if (length_ < frameLength) {
      return false;
    }

    const bool decoded = decodeFrame(buffer_, frameLength, message);
    discard(frameLength);
    if (decoded) {
      return true;
    }
  }
  return false;
}

}  // namespace givenergy
