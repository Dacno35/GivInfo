# ESP32 GivEnergy

This is an ESP32-native client for direct local communication with a
GivEnergy inverter on TCP port 8899. It provides:

- a long-lived TCP connection, heartbeat replies, retries, and reconnects;
- live register polling for single-phase, three-phase, EMS, and gateway
  profiles;
- decoded headline values for an LVGL dashboard;
- typed battery-module telemetry, including cell voltage and temperature
  data;
- a `/24` scanner that reads identity registers and selects the matching
  polling profile;
- named, range-checked controls with a strict holding-register whitelist; and
- write acknowledgement followed by holding-register readback verification.

It does not run Python, Docker, Redis, MQTT, Home Assistant discovery, the
GivTCP web API, or GivTCP's web UI. Those server features do not belong on a
microcontroller. The reusable part implemented here is the local inverter
protocol and a deliberately small control surface.

## Important safety rules

An inverter is mains-connected equipment. Before enabling controls:

1. Select the correct `DeviceProfile`; do not guess between single-phase,
   three-phase, EMS, and gateway.
2. Start with monitoring only and confirm the returned serial number and
   readings.
3. Do not let GivTCP, the cloud app, and the ESP32 issue writes at the same
   time.
4. Never expose raw register/value entry fields in the UI.
5. Treat `ControlStatus::Verified` as success. `Queued` or `Sent` is not
   success.
6. Check the inverter's own limits, firmware, warranty, and electrical
   installation requirements.

The reboot register and protection/grid-code registers are intentionally not
writeable through this library.

## Add it to your existing display project

Copy this entire folder to:

```text
your-project/lib/ESP32GivEnergy
```

Then include it:

```cpp
#include <ESP32GivEnergy.h>
```

Do not add another copy of `WiFi`, LVGL, or ESP32 Display Panel to
`lib_deps`. This library only depends on the Arduino ESP32 framework.

Create the client after Wi-Fi is connected:

```cpp
static givenergy::GivEnergyClient inverter;

void setup() {
    // WiFi.begin(...), wait for WL_CONNECTED
    inverter.begin(
        IPAddress(192, 168, 1, 100),
        givenergy::DeviceProfile::SinglePhase,
        0x11
    );
}

void loop() {
    inverter.update();
    lv_timer_handler();
    delay(2);
}
```

`update()` must run frequently. It does not perform a full subnet scan and it
keeps one TCP connection open, so port 8899 is not repeatedly grabbed and
released.

## Real-time values

```cpp
givenergy::LiveData live;
if (inverter.liveData(live)) {
    Serial.printf(
        "PV %ld W, load %ld W, grid %ld W, battery %ld W, SOC %u%%\n",
        static_cast<long>(live.pvPowerW),
        static_cast<long>(live.loadPowerW),
        static_cast<long>(live.gridPowerW),
        static_cast<long>(live.batteryPowerW),
        live.batterySocPercent
    );
}
```

The default fast poll is 5 seconds, which is close to real-time without
hammering the inverter. Change it with:

```cpp
inverter.setPollIntervals(5000, 60000);
```

The first number is the live-data interval. The second is for slower settings
and identity registers.

## Battery cells

Battery telemetry is requested separately from the normal inverter poll. A
rediscovery checks all supported module addresses for the selected profile:

```cpp
// Start a battery-address scan after the client is online.
inverter.requestBatteryRefresh(true);

// Keep calling inverter.update() from loop().
if (!inverter.batteryRefreshInProgress()) {
    for (size_t batteryIndex = 0;
            batteryIndex < inverter.batteryCount();
            ++batteryIndex) {
        const givenergy::BatteryData *battery =
            inverter.battery(batteryIndex);
        if (battery == nullptr) {
            continue;
        }

        Serial.printf(
            "%s: %u cells\n",
            battery->serial,
            battery->cellCount);

        for (size_t cellIndex = 0;
                cellIndex < battery->cellCount;
                ++cellIndex) {
            const givenergy::CellData &cell =
                battery->cells[cellIndex];
            Serial.printf(
                "Cell %u: %.3f V, %.1f C\n",
                static_cast<unsigned>(cellIndex + 1),
                cell.voltageV,
                cell.temperatureC);
        }
    }
}
```

After discovery, `requestBatteryRefresh(false)` refreshes only the modules
that replied. The GivInfo Cells tab does this every 60 seconds while visible
and also provides a manual Refresh button.

For low-voltage battery modules, the GivEnergy register block exposes 16
individual cell voltages but four temperature values: cells 1-4 share T1,
5-8 share T2, 9-12 share T3, and 13-16 share T4. `CellData` therefore marks
those readings with `temperatureIsGrouped` and `temperatureGroup`; it does
not pretend that four grouped sensors are 16 independent measurements.
Supported three-phase high-voltage BMU blocks expose up to 24 individual cell
voltages and temperatures.

`BatteryData` also includes every documented human-readable field in the
low-voltage module block: SOC, cell-sum/output voltage, BMS and
minimum/maximum temperature, calibrated/design/secondary-design/remaining
capacity, total charged and discharged energy, cycle count, firmware, USB
device state, warnings, and the seven manufacturer status bytes. Fields not
supplied by a battery format have their matching `...Valid` flag set to
false. The manufacturer has not published meanings for the status and
warning bits, so the UI shows those values as raw hexadecimal rather than
inventing labels.

The battery block does not expose per-cell current, SOC, internal resistance,
health, balancing flags, or separate warnings. The Cells tab labels its
lowest/highest/spread values as derived measurements and does not present
them as BMS registers.

The high-voltage BMU block supplies up to 24 individual voltages,
temperatures, and its serial number. Pack-wide high-voltage BCU values are a
different register block and are not guessed from BMU data.

## Safe controls

Controls are queued and serialized on the same TCP connection:

```cpp
inverter.control(givenergy::ControlId::ChargeTargetSoc, 85);
inverter.control(givenergy::ControlId::BatterySocReserve, 20);
inverter.control(givenergy::ControlId::EnableCharge, 1);
inverter.control(givenergy::ControlId::EnableDischarge, 1);
inverter.control(givenergy::ControlId::EcoMode, 1);
```

Time slots use numeric hour/minute fields, so invalid values such as 12:75
are rejected:

```cpp
inverter.setTimeSlot(
    false,  // false = charge; true = discharge
    1,      // slot number
    1, 30,  // start 01:30
    4, 30   // end 04:30
);
```

Check the result:

```cpp
if (inverter.lastControlStatus() == givenergy::ControlStatus::Verified) {
    // The inverter echoed the write and the following read matched it.
}
```

Only one control command is accepted at a time. Queued background reads are
discarded before a control so the write follows the read already in flight.
Every write must receive an acknowledgement and pass a holding-register
read-back check. A Modbus exception, mismatched acknowledgement, mismatched
read-back, TCP failure, or timeout changes the status to
`ControlStatus::Failed` and cancels the remaining writes in that command.
If Wi-Fi or TCP drops while a write or its read-back is active, that control
is explicitly failed so a higher-level workflow can reconnect and restore
saved settings instead of remaining stuck in a busy state.

Supported controls vary by profile. Out-of-range and unsupported requests are
rejected before any packet is sent.

The included GivInfo `main.cpp` adds verified timed workflows for recognised
single-phase and three-phase inverters, with Charge Now, Discharge Now, and
Stop buttons.

For single-phase equipment it follows GivTCP's slot-based method: it snapshots
charge/discharge slot 1, schedule enables, target/reserve, DC power limits,
Eco mode, and the optional battery-pause mode. Charge Now temporarily sets
charge slot 1, its target and schedule. Discharge Now temporarily sets
discharge slot 1, timed-export mode, its SOC reserve and schedule. The slot is
calculated from the inverter's own local clock, including a duration that
crosses midnight. A 1-100% UI power setting is converted to the inverter's
0-50 DC limit.

Three-phase equipment continues to use its direct AC-charge, force-charge,
and force-discharge registers. Every change in either workflow is verified
separately. At the requested time or SOC limit, all settings changed by that
workflow are restored to their saved values.

The recovery snapshot is also stored in ESP32 NVS before the first write. If
the display reboots or loses power during a timed operation, selecting the
same inverter after Wi-Fi reconnects automatically starts restoration. A new
control cannot start while that safety recovery remains unresolved.

## Finding and identifying the inverter

The scanner probes the ESP32's own `/24` subnet. Every device that accepts a
TCP connection on port 8899 is returned. On that same connection it requests
holding registers 0-59 from slave addresses `0x11` and `0x31`. This follows
the lightweight identity approach used by GivTCP-compatible clients:

- HR0 supplies the device/model code;
- HR21 distinguishes hybrid generations where required;
- HR13-17 supply the ten-character serial number; and
- the model determines the single-phase, three-phase, EMS, or gateway polling
  profile.

An open port that does not return a valid identity block remains in the
results with serial `Unknown`, slave `0`, and an unknown profile. Do not start
monitoring or controls for such a result. The connection is reused between
the two slave-address requests whenever possible.
The scanner allows up to 1 second for the TCP connection and up to 5 seconds
for each slave-address reply so busy Wi-Fi adapters have more time to respond.

```cpp
static givenergy::GivEnergyScanner scanner;

scanner.start();  // call once from a Scan button

// In loop():
scanner.update();

if (scanner.state() == givenergy::GivEnergyScanner::State::Finished) {
    for (size_t i = 0; i < scanner.foundCount(); ++i) {
        Serial.println(scanner.foundAddress(i));
        Serial.println(scanner.foundSerial(i));
        Serial.printf(
            "Slave 0x%02X\n",
            scanner.foundSlaveAddress(i));
        if (scanner.foundProfileKnown(i)) {
            Serial.printf(
                "Model %s, profile %s, code 0x%04X\n",
                givenergy::deviceModelName(
                    scanner.foundModel(i)),
                givenergy::deviceProfileName(
                    scanner.foundProfile(i)),
                scanner.foundModelCode(i));
        }
    }
}
```

To test one address entered by the user instead of scanning the full subnet:

```cpp
IPAddress enteredAddress;
if (enteredAddress.fromString("192.168.1.133")) {
    scanner.startAddress(enteredAddress);
}
```

`startAddress()` follows exactly the same read-only port, slave, identity, and
profile checks as the subnet scan. Its result is available through the same
`found...()` methods.

Scanning still opens a short connection to each candidate, so make it a
user-initiated setup action. Do not scan continuously and do not scan while a
monitoring client is active. After selection, pass the scanner's
`foundProfile()` and `foundSlaveAddress()` to `GivEnergyClient::begin()`;
never replace them with a site-wide hard-coded profile.

## Raw reads and additional devices

Raw reads are allowed because they cannot alter the inverter:

```cpp
inverter.requestRegisters(
    givenergy::RegisterBank::Input,
    60,
    60,
    0x32
);
```

That example requests a low-voltage battery register page. Prefer
`requestBatteryRefresh()` and the typed `BatteryData` view for cell telemetry.
Raw writes are not part of the public API.

`docs/register_manifest.json` contains every named `Def(...)` register entry
and control constant extracted from the pinned GivTCP model sources. The
fixed register store also accepts unnamed/reserved addresses below 5000.
Headline `LiveData` decoding is implemented for the four profiles above, and
LV battery/HV BMU cell blocks have the dedicated typed view described above.
Other specialist BCU and meter fields can still be read from
`inverter.registers()`.

## Example project

`examples/SerialMonitorControl` is a complete PlatformIO project for the
Waveshare ESP32-S3 7-inch board. Edit the Wi-Fi credentials, inverter IP, and
profile at the top of `src/main.cpp`. No write occurs automatically at boot.
Controls are only sent after you type a command into PlatformIO Monitor.

See `docs/LVGL_INTEGRATION.md` for wiring the values and controls into your
existing pages.

## Validation and source pin

Run the host protocol tests with:

```sh
test/native/run.sh
```

They check current GivTCP-compatible request vectors, stream framing with
noise and partial packets, response decoding, LV/HV cell telemetry scaling,
control whitelisting, and Arduino-facing source syntax.

Protocol and register behaviour was derived against these pinned upstream
revisions:

- `britkat1980/giv_tcp`: `b6a3ba85c5d81f0acaad0e574fccb902aa23b03c`
- `GivEnergy/givenergy-modbus2`: `c0a1f8337cf5cfc3519e77f088850148a595a56d`

The upstream projects warn that register coverage and interpretation can
change with model and firmware. Test on your exact inverter before relying on
any control.
