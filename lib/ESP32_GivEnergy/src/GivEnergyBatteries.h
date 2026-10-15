#pragma once

#include <stddef.h>
#include <stdint.h>

namespace givenergy {

constexpr size_t kMaximumBatteries = 6;
constexpr size_t kMaximumCellsPerBattery = 24;
constexpr size_t kBatterySerialLength = 10;

enum class BatteryTelemetryFormat : uint8_t {
  LowVoltage16,
  HighVoltage24,
};

struct CellData {
  bool voltageValid = false;
  bool temperatureValid = false;
  bool temperatureIsGrouped = false;
  uint8_t temperatureGroup = 0;
  float voltageV = 0;
  float temperatureC = 0;
};

struct BatteryData {
  bool valid = false;
  BatteryTelemetryFormat format =
      BatteryTelemetryFormat::LowVoltage16;
  uint8_t slaveAddress = 0;
  uint16_t baseRegister = 0;
  char serial[kBatterySerialLength + 1]{};
  uint8_t cellCount = 0;
  CellData cells[kMaximumCellsPerBattery]{};

  bool socValid = false;
  uint16_t socPercent = 0;
  bool packVoltageValid = false;
  float packVoltageV = 0;
  bool outputVoltageValid = false;
  float outputVoltageV = 0;
  bool bmsTemperatureValid = false;
  float bmsTemperatureC = 0;
  bool minimumTemperatureValid = false;
  float minimumTemperatureC = 0;
  bool maximumTemperatureValid = false;
  float maximumTemperatureC = 0;
  bool calibratedCapacityValid = false;
  float calibratedCapacityAh = 0;
  bool designCapacityValid = false;
  float designCapacityAh = 0;
  bool secondaryDesignCapacityValid = false;
  float secondaryDesignCapacityAh = 0;
  bool remainingCapacityValid = false;
  float remainingCapacityAh = 0;
  bool cyclesValid = false;
  uint16_t cycles = 0;
  bool firmwareValid = false;
  uint16_t firmwareVersion = 0;
  bool dischargeEnergyTotalValid = false;
  float dischargeEnergyTotalKWh = 0;
  bool chargeEnergyTotalValid = false;
  float chargeEnergyTotalKWh = 0;
  bool usbDeviceValid = false;
  uint16_t usbDevice = 0;
  bool warningsValid = false;
  uint8_t warning1 = 0;
  uint8_t warning2 = 0;
  bool statusValid = false;
  uint8_t status[7]{};
};

bool decodeBatteryData(
    BatteryTelemetryFormat format,
    uint8_t slaveAddress,
    uint16_t baseRegister,
    const uint16_t *registers,
    size_t registerCount,
    BatteryData &battery);

const char *batteryTelemetryFormatName(
    BatteryTelemetryFormat format);

const char *batteryUsbDeviceName(uint16_t value);

}  // namespace givenergy
