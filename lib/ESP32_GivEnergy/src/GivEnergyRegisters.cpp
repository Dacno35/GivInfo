#include "GivEnergyRegisters.h"

#include <string.h>

namespace givenergy {
namespace {

bool bitIsSet(const uint8_t *bits, uint16_t address) {
  return (bits[address / 8] & static_cast<uint8_t>(1U << (address % 8))) != 0;
  //The code needs to know whtether a register has actually been received
  //It cannot simply check whether its value is zero, because zero may be a legitimate inverter value. Instead, it stores a separate
  //validity bit for every register
}
//One byte can hold eight validity flags:
//For register address 13:
void setBit(uint8_t *bits, uint16_t address) {
  bits[address / 8] |= static_cast<uint8_t>(1U << (address % 8));
}
//So register 13 uses: Byte bits[1]
//Bit number 5

bool raw(
    const RegisterStore &store,
    RegisterBank bank,
    uint16_t address,
    uint16_t &value) {
  return store.get(bank, address, value);
}
//This is a small convenience function
//instead of repeatedly writing:
//registers.get(RegisterBank::Input,50,value);
//The decoder can write it raw(...);
//It returns true if the register was received , and value was updated.
// false if the register is unavailable,

int16_t asInt16(uint16_t value) {
  return static_cast<int16_t>(value);
}
//Converting unsigned registers to signed values
const PollBlock kSingleFast[] = {
    {RegisterBank::Input, 0, 60},
    {RegisterBank::Input, 180, 60},
};
const PollBlock kSingleSlow[] = {
    {RegisterBank::Holding, 0, 60},
    {RegisterBank::Holding, 60, 60},
    {RegisterBank::Holding, 120, 60},
    {RegisterBank::Holding, 240, 60},
    {RegisterBank::Holding, 300, 60},
};

const PollBlock kThreePhaseFast[] = {
    {RegisterBank::Input, 1000, 60},
    {RegisterBank::Input, 1060, 60},
    {RegisterBank::Input, 1120, 60},
};
const PollBlock kThreePhaseSlow[] = {
    {RegisterBank::Holding, 0, 60},
    {RegisterBank::Holding, 1080, 45},
    {RegisterBank::Input, 1180, 60},
    {RegisterBank::Input, 1240, 60},
    {RegisterBank::Input, 1300, 60},
    {RegisterBank::Input, 1360, 54},
};

const PollBlock kEmsFast[] = {
    {RegisterBank::Input, 0, 60},
    {RegisterBank::Input, 2040, 56},
};
const PollBlock kEmsSlow[] = {
    {RegisterBank::Holding, 0, 60},
    {RegisterBank::Holding, 2040, 36},
};

const PollBlock kGatewayFast[] = {
    {RegisterBank::Input, 1600, 60},
    {RegisterBank::Input, 1800, 60},
};
const PollBlock kGatewaySlow[] = {
    {RegisterBank::Input, 1640, 60},
    {RegisterBank::Input, 1700, 60},
    {RegisterBank::Input, 1760, 60},
    {RegisterBank::Input, 1860, 60},
};

}  // namespace

uint16_t *RegisterStore::values(RegisterBank bank) {
  switch (bank) {
    case RegisterBank::Holding:
      return holding_;
    case RegisterBank::Input:
      return input_;
    case RegisterBank::MeterProduct:
      return meter_;
  }
  return input_;
}

const uint16_t *RegisterStore::values(RegisterBank bank) const {
  return const_cast<RegisterStore *>(this)->values(bank);
}

uint8_t *RegisterStore::validBits(RegisterBank bank) {
  switch (bank) {
    case RegisterBank::Holding:
      return holdingValid_;
    case RegisterBank::Input:
      return inputValid_;
    case RegisterBank::MeterProduct:
      return meterValid_;
  }
  return inputValid_;
}

const uint8_t *RegisterStore::validBits(RegisterBank bank) const {
  return const_cast<RegisterStore *>(this)->validBits(bank);
}

void RegisterStore::clear() {
  memset(holding_, 0, sizeof(holding_));
  memset(input_, 0, sizeof(input_));
  memset(meter_, 0, sizeof(meter_));
  memset(holdingValid_, 0, sizeof(holdingValid_));
  memset(inputValid_, 0, sizeof(inputValid_));
  memset(meterValid_, 0, sizeof(meterValid_));
}

bool RegisterStore::set(RegisterBank bank, uint16_t address, uint16_t value) {
  if (address >= kRegisterStoreSize) {
    return false;
  }
  values(bank)[address] = value;
  setBit(validBits(bank), address);
  return true;
}

bool RegisterStore::get(
    RegisterBank bank,
    uint16_t address,
    uint16_t &value) const {
  if (!has(bank, address)) {
    return false;
  }
  value = values(bank)[address];
  return true;
}

bool RegisterStore::has(RegisterBank bank, uint16_t address) const {
  return address < kRegisterStoreSize &&
         bitIsSet(validBits(bank), address);
}

int16_t RegisterStore::signed16(
    RegisterBank bank,
    uint16_t address,
    bool *valid) const {
  uint16_t value = 0;
  const bool ok = get(bank, address, value);
  if (valid != nullptr) {
    *valid = ok;
  }
  return ok ? asInt16(value) : 0;
}

uint32_t RegisterStore::unsigned32(
    RegisterBank bank,
    uint16_t highAddress,
    uint16_t lowAddress,
    bool *valid) const {
  uint16_t high = 0;
  uint16_t low = 0;
  const bool ok = get(bank, highAddress, high) && get(bank, lowAddress, low);
  if (valid != nullptr) {
    *valid = ok;
  }
  return ok ? (static_cast<uint32_t>(high) << 16) | low : 0;
}

int32_t RegisterStore::signed32(
    RegisterBank bank,
    uint16_t highAddress,
    uint16_t lowAddress,
    bool *valid) const {
  return static_cast<int32_t>(
      unsigned32(bank, highAddress, lowAddress, valid));
}

bool decodeLiveData(
    DeviceProfile profile,
    const RegisterStore &registers,
    LiveData &live) {
  live = LiveData{};
  uint16_t value = 0;

  if (profile == DeviceProfile::SinglePhase) {
    if (!raw(registers, RegisterBank::Input, 0, live.status)) {
      return false;
    }
    if (raw(registers, RegisterBank::Input, 1, value)) {
      live.pv1VoltageV = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 2, value)) {
      live.pv2VoltageV = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 5, value)) {
      live.gridVoltageV = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 13, value)) {
      live.gridFrequencyHz = value / 100.0F;
    }
    uint16_t pv1 = 0;
    uint16_t pv2 = 0;
    raw(registers, RegisterBank::Input, 18, pv1);
    raw(registers, RegisterBank::Input, 20, pv2);
    live.pvPowerW = static_cast<int32_t>(pv1) + pv2;
    if (raw(registers, RegisterBank::Input, 42, value)) {
      live.loadPowerW = value;
    }
    if (raw(registers, RegisterBank::Input, 30, value)) {
      live.gridPowerW = asInt16(value);
    }
    if (raw(registers, RegisterBank::Input, 24, value)) {
      live.inverterPowerW = asInt16(value);
    }
    if (raw(registers, RegisterBank::Input, 52, value)) {
      live.batteryPowerW = asInt16(value);
    }
    raw(registers, RegisterBank::Input, 59, live.batterySocPercent);
    if (raw(registers, RegisterBank::Input, 50, value)) {
      live.batteryVoltageV = value / 100.0F;
    }
    if (raw(registers, RegisterBank::Input, 56, value)) {
      live.batteryTemperatureC = asInt16(value) / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 17, value)) {
      live.pvEnergyTodayKWh = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 26, value)) {
      live.importEnergyTodayKWh = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 25, value)) {
      live.exportEnergyTodayKWh = value / 10.0F;
    }
    live.valid = true;
    return true;
  }

  if (profile == DeviceProfile::ThreePhase) {
    if (!raw(registers, RegisterBank::Input, 1076, live.status)) {
      return false;
    }
    if (raw(registers, RegisterBank::Input, 1001, value)) {
      live.pv1VoltageV = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 1002, value)) {
      live.pv2VoltageV = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 1061, value)) {
      live.gridVoltageV = value / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 1067, value)) {
      live.gridFrequencyHz = value / 100.0F;
    }
    bool ok1 = false;
    bool ok2 = false;
    const uint32_t pv1 =
        registers.unsigned32(RegisterBank::Input, 1017, 1018, &ok1);
    const uint32_t pv2 =
        registers.unsigned32(RegisterBank::Input, 1019, 1020, &ok2);
    if (ok1 || ok2) {
      live.pvPowerW = static_cast<int32_t>((pv1 + pv2) / 10U);
    }
    live.inverterPowerW =
        registers.signed32(RegisterBank::Input, 1069, 1070) / 10;
    const int32_t imported =
        static_cast<int32_t>(
            registers.unsigned32(RegisterBank::Input, 1079, 1080) / 10U);
    const int32_t exported =
        static_cast<int32_t>(
            registers.unsigned32(RegisterBank::Input, 1081, 1082) / 10U);
    live.gridPowerW = imported - exported;
    live.loadPowerW =
        static_cast<int32_t>(
            registers.unsigned32(RegisterBank::Input, 1089, 1090) / 10U);
    live.batterySocPercent =
        registers.has(RegisterBank::Input, 1132)
            ? static_cast<uint16_t>(
                  registers.signed16(RegisterBank::Input, 1132))
            : 0;
    if (raw(registers, RegisterBank::Input, 1131, value)) {
      live.batteryVoltageV = value / 10.0F;
    }
    const int32_t batteryDischarge =
        static_cast<int32_t>(
            registers.unsigned32(RegisterBank::Input, 1136, 1137) / 10U);
    const int32_t batteryCharge =
        static_cast<int32_t>(
            registers.unsigned32(RegisterBank::Input, 1138, 1139) / 10U);
    live.batteryPowerW = batteryDischarge - batteryCharge;
    live.valid = true;
    return true;
  }

  if (profile == DeviceProfile::Ems) {
    if (!raw(registers, RegisterBank::Input, 2040, live.status)) {
      return false;
    }
    if (raw(registers, RegisterBank::Input, 2086, value)) {
      live.loadPowerW = value;
    }
    if (raw(registers, RegisterBank::Input, 2089, value)) {
      live.gridPowerW = asInt16(value);
    }
    if (raw(registers, RegisterBank::Input, 2090, value)) {
      live.batteryPowerW = asInt16(value);
    }
    if (raw(registers, RegisterBank::Input, 52, value)) {
      live.inverterPowerW = asInt16(value);
    }
    live.valid = true;
    return true;
  }

  if (profile == DeviceProfile::Gateway) {
    if (!raw(registers, RegisterBank::Input, 1703, live.status)) {
      return false;
    }
    if (raw(registers, RegisterBank::Input, 1608, value)) {
      live.gridVoltageV = asInt16(value) / 10.0F;
    }
    if (raw(registers, RegisterBank::Input, 1617, value)) {
      live.pvPowerW = value;
    }
    if (raw(registers, RegisterBank::Input, 1618, value)) {
      live.loadPowerW = value;
    }
    if (raw(registers, RegisterBank::Input, 1616, value)) {
      live.gridPowerW = asInt16(value);
    }
    if (raw(registers, RegisterBank::Input, 1702, value)) {
      live.batteryPowerW = asInt16(value);
    }
    raw(registers, RegisterBank::Input, 1801, live.batterySocPercent);
    live.valid = true;
    return true;
  }

  return false;
}

const PollBlock *fastPollBlocks(DeviceProfile profile, size_t &count) {
  switch (profile) {
    case DeviceProfile::SinglePhase:
      count = sizeof(kSingleFast) / sizeof(kSingleFast[0]);
      return kSingleFast;
    case DeviceProfile::ThreePhase:
      count = sizeof(kThreePhaseFast) / sizeof(kThreePhaseFast[0]);
      return kThreePhaseFast;
    case DeviceProfile::Ems:
      count = sizeof(kEmsFast) / sizeof(kEmsFast[0]);
      return kEmsFast;
    case DeviceProfile::Gateway:
      count = sizeof(kGatewayFast) / sizeof(kGatewayFast[0]);
      return kGatewayFast;
  }
  count = 0;
  return nullptr;
}

const PollBlock *slowPollBlocks(DeviceProfile profile, size_t &count) {
  switch (profile) {
    case DeviceProfile::SinglePhase:
      count = sizeof(kSingleSlow) / sizeof(kSingleSlow[0]);
      return kSingleSlow;
    case DeviceProfile::ThreePhase:
      count = sizeof(kThreePhaseSlow) / sizeof(kThreePhaseSlow[0]);
      return kThreePhaseSlow;
    case DeviceProfile::Ems:
      count = sizeof(kEmsSlow) / sizeof(kEmsSlow[0]);
      return kEmsSlow;
    case DeviceProfile::Gateway:
      count = sizeof(kGatewaySlow) / sizeof(kGatewaySlow[0]);
      return kGatewaySlow;
  }
  count = 0;
  return nullptr;
}

}  // namespace givenergy
