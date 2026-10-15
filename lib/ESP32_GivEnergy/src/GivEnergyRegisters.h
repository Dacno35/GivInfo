#pragma once

#include <stddef.h>
#include <stdint.h>

namespace givenergy {

constexpr size_t kRegisterStoreSize = 5000;

enum class RegisterBank : uint8_t {
  Holding,
  Input,
  MeterProduct,
};

enum class DeviceProfile : uint8_t {
  SinglePhase,
  ThreePhase,
  Ems,
  Gateway,
};

class RegisterStore {
 public:
  void clear();
  bool set(RegisterBank bank, uint16_t address, uint16_t value);
  bool get(RegisterBank bank, uint16_t address, uint16_t &value) const;
  bool has(RegisterBank bank, uint16_t address) const;

  int16_t signed16(RegisterBank bank, uint16_t address, bool *valid = nullptr) const;
  uint32_t unsigned32(
      RegisterBank bank,
      uint16_t highAddress,
      uint16_t lowAddress,
      bool *valid = nullptr) const;
  int32_t signed32(
      RegisterBank bank,
      uint16_t highAddress,
      uint16_t lowAddress,
      bool *valid = nullptr) const;

 private:
  uint16_t holding_[kRegisterStoreSize]{};
  uint16_t input_[kRegisterStoreSize]{};
  uint16_t meter_[kRegisterStoreSize]{};
  uint8_t holdingValid_[(kRegisterStoreSize + 7) / 8]{};
  uint8_t inputValid_[(kRegisterStoreSize + 7) / 8]{};
  uint8_t meterValid_[(kRegisterStoreSize + 7) / 8]{};

  uint16_t *values(RegisterBank bank);
  const uint16_t *values(RegisterBank bank) const;
  uint8_t *validBits(RegisterBank bank);
  const uint8_t *validBits(RegisterBank bank) const;
};

struct LiveData {
  bool valid = false;
  uint16_t status = 0;
  float pv1VoltageV = 0;
  float pv2VoltageV = 0;
  float gridVoltageV = 0;
  float gridFrequencyHz = 0;
  int32_t pvPowerW = 0;
  int32_t loadPowerW = 0;
  int32_t gridPowerW = 0;
  int32_t inverterPowerW = 0;
  int32_t batteryPowerW = 0;
  uint16_t batterySocPercent = 0;
  float batteryVoltageV = 0;
  float batteryTemperatureC = 0;
  float pvEnergyTodayKWh = 0;
  float importEnergyTodayKWh = 0;
  float exportEnergyTodayKWh = 0;
};

bool decodeLiveData(
    DeviceProfile profile,
    const RegisterStore &registers,
    LiveData &live);

struct PollBlock {
  RegisterBank bank;
  uint16_t base;
  uint16_t count;
};

const PollBlock *fastPollBlocks(DeviceProfile profile, size_t &count);
const PollBlock *slowPollBlocks(DeviceProfile profile, size_t &count);

}  // namespace givenergy
