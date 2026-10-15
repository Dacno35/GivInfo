#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "GivEnergyBatteries.h"
#include "GivEnergyControls.h"
#include "GivEnergyIdentity.h"
#include "GivEnergyProtocol.h"
#include "GivEnergyRegisters.h"
#include "GivEnergyScanner.h"

using namespace givenergy;

static bool nearFloat(float actual, float expected) {
  return fabsf(actual - expected) < 0.001F;
}

static void setSerial(
    uint16_t *registers,
    size_t first,
    const char *serial) {
  for (size_t i = 0; i < 5; ++i) {
    registers[first + i] =
        static_cast<uint16_t>(
            (static_cast<uint16_t>(
                 static_cast<uint8_t>(serial[i * 2]))
             << 8U) |
            static_cast<uint8_t>(serial[i * 2 + 1]));
  }
}

static size_t fromHex(const char *hex, uint8_t *output, size_t capacity) {
  size_t count = 0;
  int high = -1;
  for (const char *p = hex; *p != '\0'; ++p) {
    int value = -1;
    if (*p >= '0' && *p <= '9') value = *p - '0';
    if (*p >= 'a' && *p <= 'f') value = *p - 'a' + 10;
    if (*p >= 'A' && *p <= 'F') value = *p - 'A' + 10;
    if (value < 0) continue;
    if (high < 0) {
      high = value;
    } else {
      assert(count < capacity);
      output[count++] = static_cast<uint8_t>((high << 4) | value);
      high = -1;
    }
  }
  assert(high < 0);
  return count;
}

static void testRequestVectors() {
  uint8_t actual[64]{};
  uint8_t expected[64]{};

  size_t length =
      buildReadRequest(0x04, 0x0010, 6, 0x32, "AB1234G567", actual, sizeof(actual));
  size_t expectedLength = fromHex(
      "59590001001c0102"
      "41423132333447353637"
      "0000000000000008"
      "320400100006740e",
      expected,
      sizeof(expected));
  assert(length == expectedLength);
  assert(memcmp(actual, expected, length) == 0);

  length =
      buildReadRequest(0x04, 0, 60, 0x11, "AB1234G567", actual, sizeof(actual));
  assert(length == 34);
  assert(actual[26] == 0x11);
  assert(actual[27] == 0x04);
  assert(actual[28] == 0x00 && actual[29] == 0x00);
  assert(actual[30] == 0x00 && actual[31] == 0x3C);

  length =
      buildReadRequest(0x04, 0, 60, 0x31, "AB1234G567", actual, sizeof(actual));
  assert(length == 34);
  assert(actual[26] == 0x31);
  assert(actual[30] == 0x00 && actual[31] == 0x3C);

  length =
      buildReadRequest(0x03, 0, 60, 0x11, "AB1234G567", actual, sizeof(actual));
  assert(length == 34);
  assert(actual[26] == 0x11);
  assert(actual[27] == 0x03);

  length =
      buildWriteRequest(20, 1, 0x32, "AB1234G567", actual, sizeof(actual));
  expectedLength = fromHex(
      "59590001001c0102"
      "41423132333447353637"
      "0000000000000008"
      "3206001400010dcd",
      expected,
      sizeof(expected));
  assert(length == expectedLength);
  assert(memcmp(actual, expected, length) == 0);

  length =
      buildHeartbeatResponse("AB1234G567", 0x20, actual, sizeof(actual));
  expectedLength = fromHex(
      "59590001000d0101"
      "41423132333447353637"
      "20",
      expected,
      sizeof(expected));
  assert(length == expectedLength);
  assert(memcmp(actual, expected, length) == 0);
}

static void testResponseAndFramer() {
  uint8_t frame[128]{};
  const size_t length = fromHex(
      "5959000100320102"
      "57463132333447353637"
      "000000000000008a"
      "3204"
      "53413132333445353637"
      "00000006"
      "00010bee0bd50f2900000955"
      "b5d2",
      frame,
      sizeof(frame));

  Message decoded;
  assert(decodeFrame(frame, length, decoded));
  assert(decoded.type == MessageType::ReadInput);
  assert(decoded.slaveAddress == 0x32);
  assert(decoded.baseRegister == 0);
  assert(decoded.registerCount == 6);
  assert(decoded.values[0] == 1);
  assert(decoded.values[1] == 3054);
  assert(decoded.values[5] == 2389);
  assert(strcmp(decoded.inverterSerial, "SA1234E567") == 0);

  StreamFramer framer;
  const uint8_t noise[] = {0x01, 0x02, 'x'};
  framer.push(noise, sizeof(noise));
  framer.push(frame, 17);
  assert(!framer.pop(decoded));
  framer.push(frame + 17, length - 17);
  assert(framer.pop(decoded));
  assert(decoded.values[3] == 3881);
}

static void testRegistersAndControls() {
  RegisterStore store;
  store.clear();
  store.set(RegisterBank::Input, 0, 1);
  store.set(RegisterBank::Input, 1, 3054);
  store.set(RegisterBank::Input, 2, 3029);
  store.set(RegisterBank::Input, 5, 2389);
  store.set(RegisterBank::Input, 13, 5000);
  store.set(RegisterBank::Input, 18, 1200);
  store.set(RegisterBank::Input, 20, 800);
  store.set(RegisterBank::Input, 24, 1500);
  store.set(RegisterBank::Input, 30, static_cast<uint16_t>(-250));
  store.set(RegisterBank::Input, 42, 1750);
  store.set(RegisterBank::Input, 50, 5120);
  store.set(RegisterBank::Input, 52, static_cast<uint16_t>(-500));
  store.set(RegisterBank::Input, 56, 245);
  store.set(RegisterBank::Input, 59, 63);

  LiveData live;
  assert(decodeLiveData(DeviceProfile::SinglePhase, store, live));
  assert(live.valid);
  assert(live.pvPowerW == 2000);
  assert(live.gridPowerW == -250);
  assert(live.batteryPowerW == -500);
  assert(live.batterySocPercent == 63);

  ControlCommand command;
  assert(
      makeControlCommand(
          DeviceProfile::SinglePhase,
          ControlId::ChargeTargetSoc,
          85,
          command) == ControlValidation::Ok);
  assert(command.count == 3);
  assert(command.writes[2].address == 116);
  assert(command.writes[2].value == 85);
  assert(isWhitelistedWrite(DeviceProfile::SinglePhase, 116, 85));
  assert(!isWhitelistedWrite(DeviceProfile::SinglePhase, 163, 100));

  assert(
      makeControlCommand(
          DeviceProfile::SinglePhase,
          ControlId::ChargeTargetSocOnly,
          90,
          command) == ControlValidation::Ok);
  assert(command.count == 1);
  assert(command.writes[0].address == 116);
  assert(command.writes[0].value == 90);

  assert(
      makeControlCommand(
          DeviceProfile::SinglePhase,
          ControlId::BatteryPowerReserve,
          20,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 114);
  assert(command.writes[0].value == 20);

  assert(
      makeControlCommand(
          DeviceProfile::SinglePhase,
          ControlId::BatteryChargeLimitAc,
          75,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 313);

  assert(
      makeControlCommand(
          DeviceProfile::SinglePhase,
          ControlId::BatteryDischargeLimitAc,
          80,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 314);

  assert(
      makeControlCommand(
          DeviceProfile::SinglePhase,
          ControlId::ActivePowerRate,
          90,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 50);

  assert(
      makeControlCommand(
          DeviceProfile::ThreePhase,
          ControlId::ChargeTargetSoc,
          85,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 1111);

  assert(
      makeControlCommand(
          DeviceProfile::ThreePhase,
          ControlId::ForceCharge,
          1,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 1123);

  assert(
      makeControlCommand(
          DeviceProfile::ThreePhase,
          ControlId::ForceDischarge,
          1,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 1122);

  assert(
      makeControlCommand(
          DeviceProfile::Ems,
          ControlId::ChargeTargetSoc,
          80,
          command) ==
      ControlValidation::UnsupportedForProfile);

  assert(
      makeTimeSlotCommand(
          DeviceProfile::ThreePhase,
          false,
          1,
          1,
          30,
          4,
          45,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 1113);
  assert(command.writes[0].value == 130);
  assert(command.writes[1].value == 445);

  assert(
      makeTimeSlotCommand(
          DeviceProfile::SinglePhase,
          true,
          1,
          23,
          50,
          0,
          20,
          command) == ControlValidation::Ok);
  assert(command.writes[0].address == 56);
  assert(command.writes[0].value == 2350);
  assert(command.writes[1].address == 57);
  assert(command.writes[1].value == 20);
}

static void testLowVoltageBatteryData() {
  uint16_t registers[60]{};
  for (size_t i = 0; i < 16; ++i) {
    registers[i] = static_cast<uint16_t>(3250 + i);
  }
  registers[16] = 201;
  registers[17] = 207;
  registers[18] = 213;
  registers[19] = 219;
  registers[20] = 52120;
  registers[21] = 232;
  registers[22] = 0;
  registers[23] = 51965;
  registers[24] = 0;
  registers[25] = 16720;
  registers[26] = 0;
  registers[27] = 16000;
  registers[28] = 0;
  registers[29] = 6683;
  registers[34] = 0x0102;
  registers[36] = 1168;
  registers[37] = 16;
  registers[38] = 3017;
  registers[40] = 40;
  registers[41] = 0;
  registers[42] = 16000;
  registers[43] = 219;
  registers[44] = 201;
  registers[45] = 1234;
  registers[46] = 567;
  setSerial(registers, 50, "BG2045G040");
  registers[55] = 1;

  BatteryData battery;
  assert(
      decodeBatteryData(
          BatteryTelemetryFormat::LowVoltage16,
          0x32,
          60,
          registers,
          60,
          battery));
  assert(battery.valid);
  assert(battery.cellCount == 16);
  assert(strcmp(battery.serial, "BG2045G040") == 0);
  assert(nearFloat(battery.cells[0].voltageV, 3.250F));
  assert(nearFloat(battery.cells[15].voltageV, 3.265F));
  assert(nearFloat(battery.cells[0].temperatureC, 20.1F));
  assert(nearFloat(battery.cells[4].temperatureC, 20.7F));
  assert(battery.cells[0].temperatureIsGrouped);
  assert(battery.cells[0].temperatureGroup == 1);
  assert(battery.cells[15].temperatureGroup == 4);
  assert(battery.socPercent == 40);
  assert(battery.cycles == 1168);
  assert(battery.warning1 == 1);
  assert(battery.warning2 == 2);
  assert(nearFloat(battery.packVoltageV, 52.120F));
  assert(nearFloat(battery.outputVoltageV, 51.965F));
  assert(nearFloat(
      battery.secondaryDesignCapacityAh,
      160.0F));
  assert(nearFloat(
      battery.dischargeEnergyTotalKWh,
      123.4F));
  assert(nearFloat(
      battery.chargeEnergyTotalKWh,
      56.7F));
  assert(strcmp(
      batteryUsbDeviceName(battery.usbDevice),
      "Wi-Fi adapter") == 0);
}

static void testHighVoltageBatteryData() {
  uint16_t registers[60]{};
  for (size_t i = 0; i < 24; ++i) {
    registers[i] = static_cast<uint16_t>(3300 + i);
    registers[30 + i] = static_cast<uint16_t>(210 + i);
  }
  setSerial(registers, 54, "HV2501G001");

  BatteryData battery;
  assert(
      decodeBatteryData(
          BatteryTelemetryFormat::HighVoltage24,
          0x50,
          60,
          registers,
          60,
          battery));
  assert(battery.valid);
  assert(battery.cellCount == 24);
  assert(strcmp(battery.serial, "HV2501G001") == 0);
  assert(nearFloat(battery.cells[0].voltageV, 3.300F));
  assert(nearFloat(battery.cells[23].voltageV, 3.323F));
  assert(nearFloat(battery.cells[0].temperatureC, 21.0F));
  assert(nearFloat(battery.cells[15].temperatureC, 22.5F));
  assert(!battery.cells[15].temperatureIsGrouped);
}

static void testIdentityDetection() {
  assert(
      detectDeviceModel(0x2001, 300) ==
      DeviceModel::HybridGen3);
  assert(
      detectDeviceModel(0x2001, 900) ==
      DeviceModel::HybridGen2);
  assert(
      detectDeviceModel(0x4001, 0) ==
      DeviceModel::HybridThreePhase);
  assert(
      detectDeviceModel(0x5001, 0) ==
      DeviceModel::Ems);
  assert(
      detectDeviceModel(0x7001, 0) ==
      DeviceModel::Gateway);
  assert(
      detectDeviceModel(0x6001, 0) ==
      DeviceModel::Unknown);

  DeviceProfile profile = DeviceProfile::Gateway;
  assert(deviceProfileForModel(DeviceModel::HybridGen3, profile));
  assert(profile == DeviceProfile::SinglePhase);
  assert(
      deviceProfileForModel(
          DeviceModel::HybridThreePhase,
          profile));
  assert(profile == DeviceProfile::ThreePhase);
  assert(!deviceProfileForModel(DeviceModel::Unknown, profile));

  Message message;
  message.type = MessageType::ReadHolding;
  message.slaveAddress = 0x11;
  message.baseRegister = 0;
  message.registerCount = 60;
  message.values[0] = 0x2001;
  message.values[13] = 0x544A;
  message.values[14] = 0x3235;
  message.values[15] = 0x3135;
  message.values[16] = 0x4730;
  message.values[17] = 0x3139;
  message.values[21] = 300;

  DeviceIdentity identity;
  assert(decodeDeviceIdentity(message, identity));
  assert(identity.identityValid);
  assert(identity.profileKnown);
  assert(identity.slaveAddress == 0x11);
  assert(identity.modelCode == 0x2001);
  assert(identity.model == DeviceModel::HybridGen3);
  assert(identity.profile == DeviceProfile::SinglePhase);
  assert(strcmp(identity.serial, "TJ2515G019") == 0);
}

static void testSingleAddressScannerSetup() {
  GivEnergyScanner scanner;
  scanner.startAddress(IPAddress(192, 168, 1, 169));

  assert(scanner.state() == GivEnergyScanner::State::Scanning);
  assert(scanner.currentHost() == 169);
  assert(scanner.currentSlaveAddress() == 0x11);

  const IPAddress current = scanner.currentAddress();
  assert(current[0] == 192);
  assert(current[1] == 168);
  assert(current[2] == 1);
  assert(current[3] == 169);

  scanner.cancel();
  assert(scanner.state() == GivEnergyScanner::State::Idle);
}

int main() {
  testRequestVectors();
  testResponseAndFramer();
  testRegistersAndControls();
  testLowVoltageBatteryData();
  testHighVoltageBatteryData();
  testIdentityDetection();
  testSingleAddressScannerSetup();
  puts("All native tests passed");
  return 0;
}
