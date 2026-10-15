#include <Arduino.h>
#include <WiFi.h>

#include <ESP32GivEnergy.h>

using namespace givenergy;

// Change these four settings before uploading.
static constexpr char WIFI_SSID[] = "YOUR_WIFI_NAME";
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
static const IPAddress INVERTER_IP(192, 168, 1, 100);
static constexpr DeviceProfile INVERTER_PROFILE =
    DeviceProfile::SinglePhase;

static GivEnergyClient inverter;
static String serialLine;

static const char *controlStatusName(ControlStatus status) {
  switch (status) {
    case ControlStatus::Idle:
      return "idle";
    case ControlStatus::Queued:
      return "queued";
    case ControlStatus::Sent:
      return "sent";
    case ControlStatus::Verified:
      return "verified";
    case ControlStatus::Rejected:
      return "rejected";
    case ControlStatus::Failed:
      return "failed";
  }
  return "unknown";
}

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  target 85       Set charge target SOC to 85%");
  Serial.println("  reserve 20      Keep at least 20% battery SOC");
  Serial.println("  charge on       Enable scheduled charging");
  Serial.println("  charge off      Disable scheduled charging");
  Serial.println("  discharge on    Enable scheduled discharging");
  Serial.println("  discharge off   Disable scheduled discharging");
  Serial.println("  eco on          Match household demand");
  Serial.println("  eco off         Maximum-power discharge mode");
  Serial.println("  refresh         Request fresh monitoring data");
  Serial.println("  status          Show connection/control state");
  Serial.println("  help            Show this list");
  Serial.println();
}

static bool onOffValue(const String &text, uint16_t &value) {
  if (text == "on") {
    value = 1;
    return true;
  }
  if (text == "off") {
    value = 0;
    return true;
  }
  return false;
}

static void showControlResult(ControlValidation validation) {
  if (validation == ControlValidation::Ok) {
    Serial.println("Control queued; wait for status=verified.");
  } else if (validation == ControlValidation::ValueOutOfRange) {
    Serial.println("Rejected: value is outside the safe range.");
  } else {
    Serial.println("Rejected: unsupported for the selected profile.");
  }
}

static void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  const int separator = line.indexOf(' ');
  const String command =
      separator < 0 ? line : line.substring(0, separator);
  const String argument =
      separator < 0 ? "" : line.substring(separator + 1);

  if (command == "target") {
    showControlResult(inverter.control(
        ControlId::ChargeTargetSoc,
        static_cast<uint16_t>(argument.toInt())));
  } else if (command == "reserve") {
    showControlResult(inverter.control(
        ControlId::BatterySocReserve,
        static_cast<uint16_t>(argument.toInt())));
  } else if (command == "charge" || command == "discharge" ||
             command == "eco") {
    uint16_t value = 0;
    if (!onOffValue(argument, value)) {
      Serial.println("Use on or off.");
      return;
    }
    const ControlId id =
        command == "charge"
            ? ControlId::EnableCharge
            : (command == "discharge" ? ControlId::EnableDischarge
                                       : ControlId::EcoMode);
    showControlResult(inverter.control(id, value));
  } else if (command == "refresh") {
    Serial.println(inverter.requestRefresh() ? "Refresh queued."
                                             : inverter.lastError());
  } else if (command == "status") {
    Serial.printf(
        "TCP=%s control=%s register=%u value=%u error=%s\n",
        inverter.connected() ? "connected" : "disconnected",
        controlStatusName(inverter.lastControlStatus()),
        inverter.lastControlRegister(),
        inverter.lastControlValue(),
        inverter.lastError());
  } else if (command == "help") {
    printHelp();
  } else if (!command.isEmpty()) {
    Serial.println("Unknown command. Type help.");
  }
}

static void readSerialCommands() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r') {
      continue;
    }
    if (character == '\n') {
      handleCommand(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 80) {
      serialLine += character;
    }
  }
}

static void printLiveData() {
  static uint32_t lastPrintMs = 0;
  if (millis() - lastPrintMs < 2000) {
    return;
  }
  lastPrintMs = millis();

  LiveData live;
  if (!inverter.liveData(live)) {
    Serial.printf(
        "Waiting for inverter data. TCP=%s error=%s\n",
        inverter.connected() ? "connected" : "disconnected",
        inverter.lastError());
    return;
  }

  Serial.printf(
      "PV=%ldW Load=%ldW Grid=%ldW Battery=%ldW SOC=%u%% "
      "Grid=%.1fV Control=%s\n",
      static_cast<long>(live.pvPowerW),
      static_cast<long>(live.loadPowerW),
      static_cast<long>(live.gridPowerW),
      static_cast<long>(live.batteryPowerW),
      live.batterySocPercent,
      live.gridVoltageV,
      controlStatusName(inverter.lastControlStatus()));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nESP32 GivEnergy direct monitor/control");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(250);
  }
  Serial.printf("\nESP32 address: %s\n", WiFi.localIP().toString().c_str());

  // The normal inverter identity/data slave is 0x11. Keep the
  // connection open and let update() serialize all reads/writes.
  inverter.begin(INVERTER_IP, INVERTER_PROFILE, 0x11);
  inverter.setPollIntervals(5000, 60000);
  printHelp();
}

void loop() {
  inverter.update();
  readSerialCommands();
  printLiveData();
  delay(2);
}
