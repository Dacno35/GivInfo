# LVGL integration

Keep inverter networking outside page-construction functions. Create one
`GivEnergyClient` for each inverter and call `update()` on every pass through
Arduino `loop()`.

```cpp
static givenergy::GivEnergyClient inverter;

void setup() {
    // Initialise Wi-Fi, the display, LVGL, and your pages first.
    inverter.begin(
        IPAddress(192, 168, 1, 100),
        givenergy::DeviceProfile::SinglePhase
    );
}

void loop() {
    inverter.update();
    lv_timer_handler();
    delay(2);
}
```

Update labels at a lower rate. A 500–1000 ms LVGL timer is plenty; the
inverter poll interval remains independent.

```cpp
static void updateEnergyLabels(lv_timer_t *) {
    givenergy::LiveData data;
    if (!inverter.liveData(data)) {
        lv_label_set_text(connectionLabel, "Waiting for inverter");
        return;
    }

    lv_label_set_text_fmt(pvLabel, "%ld W", static_cast<long>(data.pvPowerW));
    lv_label_set_text_fmt(
        loadLabel,
        "%ld W",
        static_cast<long>(data.loadPowerW)
    );
    lv_label_set_text_fmt(socLabel, "%u%%", data.batterySocPercent);
}

void createHomePage() {
    // Create labels...
    lv_timer_create(updateEnergyLabels, 750, nullptr);
}
```

A button callback should queue a named control. It should not write a raw
register.

```cpp
static void setTargetEvent(lv_event_t *) {
    const auto result = inverter.control(
        givenergy::ControlId::ChargeTargetSoc,
        85
    );
    if (result != givenergy::ControlValidation::Ok) {
        lv_label_set_text(controlStatusLabel, inverter.lastError());
    }
}
```

Show `lastControlStatus()` in the UI. A control is only complete when its
status becomes `ControlStatus::Verified`; the library reads the holding
register back after the inverter acknowledges the write.

Do not run `GivEnergyScanner` while a `GivEnergyClient` is connected. Stop the
client, run one user-initiated scan, select the discovered inverter, then
restart the client with that address.
