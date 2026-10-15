#pragma once

#include <stdint.h>

#include "GivEnergyProtocol.h"
#include "GivEnergyRegisters.h"

namespace givenergy {

enum class DeviceModel : uint8_t {
  Unknown,
  HybridGen1,
  HybridGen2,
  HybridGen3,
  AcCoupled,
  HybridThreePhase,
  Ems,
  Gateway,
  AllInOne,
};

struct DeviceIdentity {
  bool identityValid = false;
  bool profileKnown = false;
  uint8_t slaveAddress = 0;
  uint16_t modelCode = 0;
  uint16_t armFirmwareVersion = 0;
  DeviceModel model = DeviceModel::Unknown;
  DeviceProfile profile = DeviceProfile::SinglePhase;
  char serial[kInverterSerialLength + 1]{};
};

DeviceModel detectDeviceModel(
    uint16_t modelCode,
    uint16_t armFirmwareVersion);

bool deviceProfileForModel(
    DeviceModel model,
    DeviceProfile &profile);

bool decodeDeviceIdentity(
    const Message &message,
    DeviceIdentity &identity);

const char *deviceModelName(DeviceModel model);
const char *deviceProfileName(DeviceProfile profile);

}  // namespace givenergy
