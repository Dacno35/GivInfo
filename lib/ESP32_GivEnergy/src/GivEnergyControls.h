#pragma once

#include <stddef.h>
#include <stdint.h>

#include "GivEnergyRegisters.h"

namespace givenergy {

enum class ControlId : uint8_t {
  EnableCharge,
  EnableDischarge,
  EnableChargeTarget,
  ChargeTargetSoc,
  ChargeTargetSocOnly,
  BatterySocReserve,
  BatteryPowerReserve,
  BatteryChargeLimit,
  BatteryDischargeLimit,
  BatteryChargeLimitAc,
  BatteryDischargeLimitAc,
  EcoMode,
  ActivePowerRate,
  BatteryPauseMode,
  ForceCharge,
  ForceDischarge,
  AcCharge,
};

struct RegisterWrite {
  uint16_t address = 0;
  uint16_t value = 0;
};

struct ControlCommand {
  RegisterWrite writes[3]{};
  uint8_t count = 0;
};
// A strongly typed enumeration
enum class ControlValidation : uint8_t {
  Ok,//the command is valid and can be queued
  UnsupportedForProfile, //That inverter type does not support the command
  //it describes whether a requested inverter control is valid
  ValueOutOfRange,//value too high
};

ControlValidation makeControlCommand(
    DeviceProfile profile,//returns the state from the enum datatype DeviceProfile
    ControlId control,
    uint16_t value,
    ControlCommand &command);
//means makeControlCommand returns.a ControlValidation
ControlValidation makeTimeSlotCommand(
    DeviceProfile profile,
    bool discharge,
    uint8_t slot,
    uint8_t startHour,
    uint8_t startMinute,
    uint8_t endHour,
    uint8_t endMinute,
    ControlCommand &command);

ControlValidation makeSlotTargetCommand(
    DeviceProfile profile,
    bool discharge,
    uint8_t slot,
    uint16_t targetSoc,
    ControlCommand &command);

bool isWhitelistedWrite(
    DeviceProfile profile,
    uint16_t address,
    uint16_t value);

}  // namespace givenergy
