#include "GivEnergyBatteries.h"

#include <ctype.h>
#include <string.h>

namespace givenergy {
namespace {

float milli(uint16_t value) {
  return value / 1000.0F;
}

float deci(uint16_t value) {
  return static_cast<int16_t>(value) / 10.0F;
}

float centi32(uint16_t high, uint16_t low) {
  const uint32_t value =
      (static_cast<uint32_t>(high) << 16U) | low;
  return value / 100.0F;
}

float milli32(uint16_t high, uint16_t low) {
  const uint32_t value =
      (static_cast<uint32_t>(high) << 16U) | low;
  return value / 1000.0F;
}

void decodeSerial(
    const uint16_t *registers,
    size_t first,
    char *serial) {
  size_t output = 0;
  for (size_t i = 0; i < 5; ++i) {
    const uint16_t value = registers[first + i];
    serial[output++] = static_cast<char>(value >> 8U);
    serial[output++] = static_cast<char>(value & 0xFFU);
  }
  serial[kBatterySerialLength] = '\0';
}

bool isUsableSerial(const char *serial) {
  size_t alphanumeric = 0;
  bool hasNonFiller = false;
  for (size_t i = 0; i < kBatterySerialLength; ++i) {
    const unsigned char character =
        static_cast<unsigned char>(serial[i]);
    if (character == '\0') {
      break;
    }
    if (isalnum(character) != 0) {
      ++alphanumeric;
    }
    if (character != '0' &&
        character != '*' &&
        character != ' ') {
      hasNonFiller = true;
    }
  }
  return alphanumeric >= 6 && hasNonFiller;
}

bool plausibleCellVoltage(uint16_t millivolts) {
  return millivolts >= 1500U && millivolts <= 5000U;
}

void splitBytes(uint16_t value, uint8_t &high, uint8_t &low) {
  high = static_cast<uint8_t>(value >> 8U);
  low = static_cast<uint8_t>(value & 0xFFU);
}

bool decodeLowVoltage(
    uint8_t slaveAddress,
    uint16_t baseRegister,
    const uint16_t *registers,
    size_t registerCount,
    BatteryData &battery) {
  if (registerCount < 56) {
    return false;
  }

  battery.format = BatteryTelemetryFormat::LowVoltage16;
  battery.slaveAddress = slaveAddress;
  battery.baseRegister = baseRegister;
  battery.cellCount = 16;

  size_t plausibleCells = 0;
  for (size_t i = 0; i < 16; ++i) {
    CellData &cell = battery.cells[i];
    cell.voltageValid = plausibleCellVoltage(registers[i]);
    cell.voltageV = milli(registers[i]);
    if (cell.voltageValid) {
      ++plausibleCells;
    }

    const size_t group = i / 4U;
    cell.temperatureValid = true;
    cell.temperatureIsGrouped = true;
    cell.temperatureGroup = static_cast<uint8_t>(group + 1U);
    cell.temperatureC = deci(registers[16U + group]);
  }

  battery.packVoltageValid = true;
  battery.packVoltageV = milli(registers[20]);
  battery.bmsTemperatureValid = true;
  battery.bmsTemperatureC = deci(registers[21]);
  battery.outputVoltageValid = true;
  battery.outputVoltageV =
      milli32(registers[22], registers[23]);
  battery.calibratedCapacityValid = true;
  battery.calibratedCapacityAh =
      centi32(registers[24], registers[25]);
  battery.designCapacityValid = true;
  battery.designCapacityAh =
      centi32(registers[26], registers[27]);
  battery.remainingCapacityValid = true;
  battery.remainingCapacityAh =
      centi32(registers[28], registers[29]);

  splitBytes(
      registers[30],
      battery.status[0],
      battery.status[1]);
  splitBytes(
      registers[31],
      battery.status[2],
      battery.status[3]);
  splitBytes(
      registers[32],
      battery.status[4],
      battery.status[5]);
  battery.status[6] =
      static_cast<uint8_t>(registers[33] >> 8U);
  battery.statusValid = true;
  splitBytes(
      registers[34],
      battery.warning1,
      battery.warning2);
  battery.warningsValid = true;
  battery.cycles = registers[36];
  battery.cyclesValid = true;

  const uint16_t reportedCells = registers[37];
  if (reportedCells > 0 && reportedCells <= 16) {
    battery.cellCount =
        static_cast<uint8_t>(reportedCells);
  }
  battery.firmwareVersion = registers[38];
  battery.firmwareValid = true;
  battery.socPercent = registers[40];
  battery.socValid = battery.socPercent <= 100;
  battery.secondaryDesignCapacityValid = true;
  battery.secondaryDesignCapacityAh =
      centi32(registers[41], registers[42]);
  battery.maximumTemperatureC = deci(registers[43]);
  battery.maximumTemperatureValid = true;
  battery.minimumTemperatureC = deci(registers[44]);
  battery.minimumTemperatureValid = true;
  battery.dischargeEnergyTotalValid = true;
  battery.dischargeEnergyTotalKWh =
      registers[45] / 10.0F;
  battery.chargeEnergyTotalValid = true;
  battery.chargeEnergyTotalKWh =
      registers[46] / 10.0F;
  decodeSerial(registers, 50, battery.serial);
  battery.usbDevice = registers[55];
  battery.usbDeviceValid = true;

  battery.valid =
      isUsableSerial(battery.serial) ||
      plausibleCells >= 8;
  if (!isUsableSerial(battery.serial)) {
    strncpy(
        battery.serial,
        "Unknown",
        kBatterySerialLength);
    battery.serial[kBatterySerialLength] = '\0';
  }
  return battery.valid;
}

bool decodeHighVoltage(
    uint8_t slaveAddress,
    uint16_t baseRegister,
    const uint16_t *registers,
    size_t registerCount,
    BatteryData &battery) {
  if (registerCount < 59) {
    return false;
  }

  battery.format = BatteryTelemetryFormat::HighVoltage24;
  battery.slaveAddress = slaveAddress;
  battery.baseRegister = baseRegister;
  battery.cellCount = 24;

  size_t plausibleCells = 0;
  for (size_t i = 0; i < 24; ++i) {
    CellData &cell = battery.cells[i];
    cell.voltageValid = plausibleCellVoltage(registers[i]);
    cell.voltageV = milli(registers[i]);
    if (cell.voltageValid) {
      ++plausibleCells;
    }

    cell.temperatureValid = true;
    cell.temperatureIsGrouped = false;
    cell.temperatureGroup = 0;
    cell.temperatureC = deci(registers[30U + i]);
  }

  decodeSerial(registers, 54, battery.serial);
  battery.valid =
      isUsableSerial(battery.serial) ||
      plausibleCells >= 12;
  if (!isUsableSerial(battery.serial)) {
    strncpy(
        battery.serial,
        "Unknown",
        kBatterySerialLength);
    battery.serial[kBatterySerialLength] = '\0';
  }
  return battery.valid;
}

}  // namespace

bool decodeBatteryData(
    BatteryTelemetryFormat format,
    uint8_t slaveAddress,
    uint16_t baseRegister,
    const uint16_t *registers,
    size_t registerCount,
    BatteryData &battery) {
  battery = BatteryData{};
  if (registers == nullptr || baseRegister < 60) {
    return false;
  }

  if (format == BatteryTelemetryFormat::LowVoltage16) {
    return decodeLowVoltage(
        slaveAddress,
        baseRegister,
        registers,
        registerCount,
        battery);
  }
  return decodeHighVoltage(
      slaveAddress,
      baseRegister,
      registers,
      registerCount,
      battery);
}

const char *batteryTelemetryFormatName(
    BatteryTelemetryFormat format) {
  return format == BatteryTelemetryFormat::LowVoltage16
             ? "16-cell LV"
             : "24-cell HV";
}

const char *batteryUsbDeviceName(uint16_t value) {
  switch (value) {
    case 0:
      return "None";
    case 1:
      return "Wi-Fi adapter";
    case 8:
      return "USB disk";
    default:
      return "Unknown device";
  }
}

}  // namespace givenergy
