#include "GivEnergyControls.h"

namespace givenergy {
namespace {

bool inRange(uint16_t value, uint16_t minimum, uint16_t maximum) {
  return value >= minimum && value <= maximum;
}

bool isTime(uint16_t value) {
  return value <= 2359 && (value % 100) < 60;
}

void one(ControlCommand &command, uint16_t address, uint16_t value) {
  command = ControlCommand{};
  command.writes[0] = {address, value};
  command.count = 1;
}

uint16_t hhmm(uint8_t hour, uint8_t minute) {
  return static_cast<uint16_t>(hour * 100U + minute);
}

}  // namespace

ControlValidation makeControlCommand(
    DeviceProfile profile,
    ControlId control,
    uint16_t value,
    ControlCommand &command) {
  command = ControlCommand{};

  if (profile == DeviceProfile::SinglePhase ||
      profile == DeviceProfile::Gateway) {
    switch (control) {
      case ControlId::EnableCharge:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        one(command, 96, value);
        return ControlValidation::Ok;
      case ControlId::EnableDischarge:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        one(command, 59, value);
        return ControlValidation::Ok;
      case ControlId::EnableChargeTarget:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        one(command, 20, value);
        return ControlValidation::Ok;
      case ControlId::ChargeTargetSoc:
        if (!inRange(value, 4, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        command.writes[0] = {96, 1};
        command.writes[1] = {20, static_cast<uint16_t>(value < 100 ? 1 : 0)};
        command.writes[2] = {116, value};
        command.count = 3;
        return ControlValidation::Ok;
      case ControlId::ChargeTargetSocOnly:
        if (!inRange(value, 4, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 116, value);
        return ControlValidation::Ok;
      case ControlId::BatterySocReserve:
        if (!inRange(value, 4, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 110, value);
        return ControlValidation::Ok;
      case ControlId::BatteryPowerReserve:
        if (!inRange(value, 4, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 114, value);
        return ControlValidation::Ok;
      case ControlId::BatteryChargeLimit:
        if (value > 50) return ControlValidation::ValueOutOfRange;
        one(command, 111, value);
        return ControlValidation::Ok;
      case ControlId::BatteryDischargeLimit:
        if (value > 50) return ControlValidation::ValueOutOfRange;
        one(command, 112, value);
        return ControlValidation::Ok;
      case ControlId::BatteryChargeLimitAc:
        if (!inRange(value, 1, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 313, value);
        return ControlValidation::Ok;
      case ControlId::BatteryDischargeLimitAc:
        if (!inRange(value, 1, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 314, value);
        return ControlValidation::Ok;
      case ControlId::EcoMode:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        one(command, 27, value);
        return ControlValidation::Ok;
      case ControlId::ActivePowerRate:
        if (value > 100) return ControlValidation::ValueOutOfRange;
        one(command, 50, value);
        return ControlValidation::Ok;
      case ControlId::BatteryPauseMode:
        if (value > 3) return ControlValidation::ValueOutOfRange;
        one(command, 318, value);
        return ControlValidation::Ok;
      case ControlId::ForceCharge:
      case ControlId::ForceDischarge:
      case ControlId::AcCharge:
        return ControlValidation::UnsupportedForProfile;
//      case ControlId::WakeBattery:
//        if (profile != DeviceProfile::SinglePhase) {
//          return ControlValidation::UnsupportedForProfile;
//        }
//        if (value > 1) {
//          return ControlValidation::ValueOutOfRange;
//        }
//        one(command, 174, value);
//        return ControlValidation::Ok;
    }
  }

  if (profile == DeviceProfile::ThreePhase) {
    switch (control) {
      case ControlId::EnableCharge:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        command.writes[0] = {1123, value};
        command.writes[1] = {1112, value};
        command.count = 2;
        return ControlValidation::Ok;
      case ControlId::EnableDischarge:
      case ControlId::ForceDischarge:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        one(command, 1122, value);
        return ControlValidation::Ok;
      case ControlId::ForceCharge:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        one(command, 1123, value);
        return ControlValidation::Ok;
      case ControlId::AcCharge:
        if (value > 1) return ControlValidation::ValueOutOfRange;
        one(command, 1112, value);
        return ControlValidation::Ok;
      case ControlId::ChargeTargetSoc:
      case ControlId::ChargeTargetSocOnly:
        if (!inRange(value, 4, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 1111, value);
        return ControlValidation::Ok;
      case ControlId::BatterySocReserve:
        if (!inRange(value, 4, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 1109, value);
        return ControlValidation::Ok;
      case ControlId::BatteryPowerReserve:
        if (!inRange(value, 4, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 1078, value);
        return ControlValidation::Ok;
      case ControlId::BatteryChargeLimitAc:
        if (!inRange(value, 1, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 1110, value);
        return ControlValidation::Ok;
      case ControlId::BatteryDischargeLimitAc:
        if (!inRange(value, 1, 100)) {
          return ControlValidation::ValueOutOfRange;
        }
        one(command, 1108, value);
        return ControlValidation::Ok;
      case ControlId::EnableChargeTarget:
      case ControlId::BatteryChargeLimit:
      case ControlId::BatteryDischargeLimit:
      case ControlId::EcoMode:
      case ControlId::ActivePowerRate:
      case ControlId::BatteryPauseMode:
        return ControlValidation::UnsupportedForProfile;
    }
  }

  return ControlValidation::UnsupportedForProfile;
}

ControlValidation makeTimeSlotCommand(
    DeviceProfile profile,
    bool discharge,
    uint8_t slot,
    uint8_t startHour,
    uint8_t startMinute,
    uint8_t endHour,
    uint8_t endMinute,
    ControlCommand &command) {
  command = ControlCommand{};
  if (startHour > 23 || endHour > 23 ||
      startMinute > 59 || endMinute > 59 ||
      slot < 1 || slot > 10) {
    return ControlValidation::ValueOutOfRange;
  }

  uint16_t startRegister = 0;
  uint16_t endRegister = 0;
  if (profile == DeviceProfile::SinglePhase ||
      profile == DeviceProfile::Gateway) {
    if (slot == 1) {
      startRegister = discharge ? 56 : 94;
      endRegister = discharge ? 57 : 95;
    } else if (slot == 2) {
      startRegister = discharge ? 44 : 243;
      endRegister = discharge ? 45 : 244;
    } else {
      const uint16_t base =
          static_cast<uint16_t>((discharge ? 267 : 237) + slot * 3);
      startRegister = base;
      endRegister = static_cast<uint16_t>(base + 1);
    }
  } else if (profile == DeviceProfile::ThreePhase) {
    if (slot == 1) {
      startRegister = discharge ? 1118 : 1113;
      endRegister = discharge ? 1119 : 1114;
    } else if (slot == 2) {
      startRegister = discharge ? 1120 : 1115;
      endRegister = discharge ? 1121 : 1116;
    } else {
      const uint16_t base =
          static_cast<uint16_t>((discharge ? 267 : 237) + slot * 3);
      startRegister = base;
      endRegister = static_cast<uint16_t>(base + 1);
    }
  } else if (profile == DeviceProfile::Ems) {
    if (slot > 3) {
      return ControlValidation::UnsupportedForProfile;
    }
    startRegister = static_cast<uint16_t>(
        (discharge ? 2041 : 2050) + slot * 3);
    endRegister = static_cast<uint16_t>(startRegister + 1);
  } else {
    return ControlValidation::UnsupportedForProfile;
  }

  command.writes[0] = {startRegister, hhmm(startHour, startMinute)};
  command.writes[1] = {endRegister, hhmm(endHour, endMinute)};
  command.count = 2;
  return ControlValidation::Ok;
}


bool isWhitelistedWrite(
    DeviceProfile profile,
    uint16_t address,
    uint16_t value) {
  if (isTime(value)) {
    ControlCommand slot;
    for (uint8_t index = 1; index <= 10; ++index) {
      if (makeTimeSlotCommand(
              profile, false, index, 0, 0, 0, 0, slot) ==
          ControlValidation::Ok) {
        for (uint8_t i = 0; i < slot.count; ++i) {
          if (slot.writes[i].address == address) return true;
        }
      }
      if (makeTimeSlotCommand(
              profile, true, index, 0, 0, 0, 0, slot) ==
          ControlValidation::Ok) {
        for (uint8_t i = 0; i < slot.count; ++i) {
          if (slot.writes[i].address == address) return true;
        }
      }
    }
  }

  for (uint8_t id = static_cast<uint8_t>(ControlId::EnableCharge);
       id <= static_cast<uint8_t>(ControlId::AcCharge);
       ++id) {
    ControlCommand command;
    if (makeControlCommand(
            profile, static_cast<ControlId>(id), value, command) !=
        ControlValidation::Ok) {
      continue;
    }
    for (uint8_t i = 0; i < command.count; ++i) {
      if (command.writes[i].address == address &&
          command.writes[i].value == value) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace givenergy
