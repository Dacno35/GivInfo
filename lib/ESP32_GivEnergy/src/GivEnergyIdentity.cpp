#include "GivEnergyIdentity.h"

#include <ctype.h>
#include <string.h>
//First it contains the declarations and types such as DeviceModel
//& DeviceIdentity
namespace givenergy {
namespace {
//groups the library code together
uint8_t leadingHexDigit(uint16_t value) {
  while (value >= 0x10U) {
    value >>= 4U; // shifts the bit to the right by four and then removes one hex digit
  }
  return static_cast<uint8_t>(value);
}
//this bit finds the first hexadecmial digits
bool isUsableSerial(const char *serial) {
  //This receives a character array containing a possible serial number
  if (serial == nullptr) {
    return false;
  //This prevents the function from trying to access a noneistent haracter array 
  }

  size_t alphanumericCount = 0;
  bool hasNonFillerCharacter = false;
  for (size_t i = 0; i < kInverterSerialLength; ++i) {
    
    const unsigned char character =
        static_cast<unsigned char>(serial[i]);
    if (character == '\0') {
      break;
    }
    if (isalnum(character) != 0) {
      ++alphanumericCount;
    }
    if (character != '0' && character != '*' && character != ' ') {
      hasNonFillerCharacter = true;
    }
  }
  return alphanumericCount >= 6 && hasNonFillerCharacter;
}

void serialFromHoldingRegisters(
  //The inverter serial is stored across holding registers 13-17
  //each register is 16 bits, so each register contains two 8-bit characters
    const Message &message,
    char *serial) {
  size_t outputIndex = 0;
  for (size_t registerIndex = 13;
       registerIndex <= 17 &&
       registerIndex < message.registerCount;
       ++registerIndex) {
    const uint16_t value = message.values[registerIndex];
    //the first character comes from the upper eight bits
    serial[outputIndex++] = static_cast<char>(value >> 8U);
    serial[outputIndex++] = static_cast<char>(value & 0xFFU);
    //the second comes from the lower 8 bits
    //e.g. 0x5754 becomes 0x57 = W and 0x54 = T
  }
  serial[kInverterSerialLength] = '\0';
  //This adds the terminating null character
}

}  // namespace

DeviceModel detectDeviceModel(
  //Used to detect the inverter model
    uint16_t modelCode,
    uint16_t armFirmwareVersion) {
  //The leading hex digit selects the model
  // 0x02 - Single phase hybrid
  // 0x03 - AC coupled
  // 0x04 - Three-phase hybrid
  // 0x05 - EMS
  // 0x07 - Gateaway
  // 0x08 - All-in-One
  // Anything else is unknown
  switch (leadingHexDigit(modelCode)) {
    case 0x02: {
      const uint16_t firmwareFamily =
          static_cast<uint16_t>(armFirmwareVersion / 100U);
      if (firmwareFamily == 3U) {
        return DeviceModel::HybridGen3;
      }
      if (firmwareFamily == 8U || firmwareFamily == 9U) {
        return DeviceModel::HybridGen2;
      }
      return DeviceModel::HybridGen1;
      //for model family 0x02 it also looks at the ARM firmware
      //It then distinguishes the generations
      //e.g. ARM version 300 means familyu 3 so its a hybrid gen 3
      // ARM version 800 means family 8 so its a hybrid gen 2
    }
    case 0x03:
      return DeviceModel::AcCoupled;
    case 0x04:
      return DeviceModel::HybridThreePhase;
    case 0x05:
      return DeviceModel::Ems;
    case 0x07:
      return DeviceModel::Gateway;
    case 0x08:
      return DeviceModel::AllInOne;
    default:
      return DeviceModel::Unknown;
  }
}

bool deviceProfileForModel(
    DeviceModel model,
    DeviceProfile &profile) {
//A model describes the physical inverter family. A profile tells the rest
//of your code which register map and control commands to use.
  switch (model) {
    case DeviceModel::HybridGen1:
    case DeviceModel::HybridGen2:
    case DeviceModel::HybridGen3:
    case DeviceModel::AcCoupled:
    case DeviceModel::AllInOne:
      profile = DeviceProfile::SinglePhase;
      return true;
    case DeviceModel::HybridThreePhase:
      profile = DeviceProfile::ThreePhase;
      return true;
    case DeviceModel::Ems:
      profile = DeviceProfile::Ems;
      return true;
    case DeviceModel::Gateway:
      profile = DeviceProfile::Gateway;
      return true;
    case DeviceModel::Unknown:
      return false;
  }
  return false;
}

bool decodeDeviceIdentity(
//This is the main function
    const Message &message,
    DeviceIdentity &identity) {
//First it cleas all previous identity data
  identity = DeviceIdentity{};
  //It then validates the received message
  if (message.type != MessageType::ReadHolding ||
      message.error ||
      message.baseRegister != 0 ||
      message.registerCount < 22) {
    return false;
    //Then it checks that the message has met a criteria of:
    //Be a holding register response
    //COntains no Modbus error
    //Start at holding register 0
    //Contain at least 22 registers
  }
//It then validates the received message
  identity.identityValid = true;
  identity.slaveAddress = message.slaveAddress;
  identity.modelCode = message.values[0];//holding register 0
  identity.armFirmwareVersion = message.values[21]; //holding register 21
  //The important values are collected 
  identity.model = detectDeviceModel(
      identity.modelCode,
      identity.armFirmwareVersion);
      //Then the correct register profile is selected
  identity.profileKnown =
      deviceProfileForModel(identity.model, identity.profile);
      //Finally it extracts the serial

  serialFromHoldingRegisters(message, identity.serial);
  if (!isUsableSerial(identity.serial) &&
      isUsableSerial(message.inverterSerial)) {
    strncpy(
      //if registers 13-17 did not contain a believable serial, it tries the serial decoded
      //from the surrounding GivEnergy packet
        identity.serial,
        message.inverterSerial,
        kInverterSerialLength);
        //if neither location provides a usable serial
    identity.serial[kInverterSerialLength] = '\0';
  }
  if (!isUsableSerial(identity.serial)) {
    strncpy(identity.serial, "Unknown", kInverterSerialLength);
    identity.serial[kInverterSerialLength] = '\0';
  }
  return true;
  //decodeDeviceIdentity() can return true while profileKnown is false
  //That means the register response was valid, but its model code was not recoginised.
}

const char *deviceModelName(DeviceModel model) {
  switch (model) {
    case DeviceModel::HybridGen1:
      return "Hybrid Gen 1";
    case DeviceModel::HybridGen2:
      return "Hybrid Gen 2";
    case DeviceModel::HybridGen3:
      return "Hybrid Gen 3";
    case DeviceModel::AcCoupled:
      return "AC coupled";
    case DeviceModel::HybridThreePhase:
      return "Hybrid three-phase";
    case DeviceModel::Ems:
      return "EMS";
    case DeviceModel::Gateway:
      return "Gateway";
    case DeviceModel::AllInOne:
      return "All-in-One";
    case DeviceModel::Unknown:
      return "Unknown model";
  }
  return "Unknown model";
}
//These functions convert enum valuesd into text for the UI
//e.g. DeviceModel::HybridGen3 becomes Hybrid Gen 3

const char *deviceProfileName(DeviceProfile profile) {
  switch (profile) {
    case DeviceProfile::SinglePhase:
      return "single-phase";
    case DeviceProfile::ThreePhase:
      return "three-phase";
    case DeviceProfile::Ems:
      return "EMS";
    case DeviceProfile::Gateway:
      return "gateway";
  }
  return "unknown";
}
//Same concept

}  // namespace givenergy
