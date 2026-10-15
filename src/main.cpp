#include <Arduino.h>
#include <cstdint>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <Preferences.h>
#include <ESP32GivEnergy.h>
#include "connectivity.h"
#include "lvgl_v8_port.h"



using namespace esp_panel::board;
using namespace esp_panel::drivers;
//organises the code into namespaces
//the :: operator means inside this namespace or type
//They're called board and drivers because it is named that
// esp_panel::board means board value inside esp_panel
static Board *board = nullptr;
//this creates a cglobal pointer that will eventually point to the Waveshare board object
static Connectivity connectivity;
static givenergy::GivEnergyScanner inverterScanner;
static givenergy::GivEnergyClient selectedInverterClient;

// Only one automatic inverter scan is allowed per boot. This prevents a
// reconnecting Wi-Fi network from repeatedly opening port 8899.
// Set this to false if GivTCP or another local Modbus client is already using
// the inverter. There is no risk-free way to probe 8899 while another client
// owns the inverter's single local socket.
static constexpr bool AUTO_SCAN_INVERTERS = true;
static bool inverterScanTriggered = false;
static bool inverterScanResultsHandled = false;
static volatile bool inverterRescanRequested = false;
static volatile int requestedInverterSelection = -1;
static bool selectedInverterConfigured = false;
static bool selectedInverterClientStarted = false;
static IPAddress selectedInverterIP;
static String selectedInverterSerial;
static uint8_t selectedInverterSlaveAddress = 0x11;
static bool selectedInverterProfileKnown = false;
static uint16_t selectedInverterModelCode = 0;
static givenergy::DeviceModel selectedInverterModel =
    givenergy::DeviceModel::Unknown;
static givenergy::DeviceProfile selectedInverterProfile =
    givenergy::DeviceProfile::SinglePhase;

static lv_obj_t *wifiDropdown = nullptr;
static lv_obj_t *wifiPasswordField = nullptr;
static lv_obj_t *wifiKeyboard = nullptr;
static lv_obj_t *wifiStatusLabel = nullptr;
static lv_obj_t *topWifiStatusLabel = nullptr;
static lv_obj_t *wifiRefreshLabel = nullptr;
static lv_obj_t *wifiRefreshButton = nullptr;
static lv_obj_t *inverterScanAddressLabel = nullptr;
static lv_obj_t *inverterIpField = nullptr;
static lv_obj_t *inverterIpKeyboard = nullptr;
static lv_obj_t *inverterIpStatusLabel = nullptr;
static lv_obj_t *inverterIpAddButton = nullptr;
static lv_obj_t *batteryIdentityLabel = nullptr;
static lv_obj_t *batteryConnectionLabel = nullptr;
static lv_obj_t *batteryUpdatedLabel = nullptr;
static lv_obj_t *cellsBatteryDropdown = nullptr;
static lv_obj_t *cellsStatusLabel = nullptr;
static lv_obj_t *cellsSummaryLabel = nullptr;
static lv_obj_t *cellsDetailsLabel = nullptr;
static lv_obj_t *cellRows[givenergy::kMaximumCellsPerBattery] = {};
static lv_obj_t *cellVoltageLabels[givenergy::kMaximumCellsPerBattery] = {};
static lv_obj_t *cellTemperatureLabels[givenergy::kMaximumCellsPerBattery] = {};
static lv_obj_t *cellDeltaLabels[givenergy::kMaximumCellsPerBattery] = {};
static lv_obj_t *cellStateLabels[givenergy::kMaximumCellsPerBattery] = {};
static size_t selectedBatteryIndex = 0;
static uint32_t displayedBatteryGeneration = UINT32_MAX;
static volatile bool batteryRediscoveryRequested = false;
static constexpr uint32_t BATTERY_REFRESH_INTERVAL_MS = 60000;

static volatile bool wifiScanRequested = false;
static uint32_t displayedWifiScanGeneration = 0;
static uint8_t displayedInverterHost = 0;
static uint8_t displayedInverterSlaveAddress = 0;

enum class InverterScanMode : uint8_t {
    None,
    Subnet,
    ManualAddress
};

static InverterScanMode inverterScanMode =
    InverterScanMode::None;
static volatile bool inverterIpProbeRequested = false;
static volatile uint32_t requestedInverterIp = 0;
static String inverterIpText;
static String inverterIpStatus =
    "Enter an inverter IPv4 address";

enum class Page : uint8_t {
    Home, // 0
    Inverters, // 1
    Battery, // 2
    Cells, // 3
    Controls, // 4
    Faults, // 5
    Wifi, // 6
    Settings, // 7
    Count // 8
};
static Page currentPage = Page::Home;
//these define all the valid interface pages
//each value is internally numbered
static constexpr size_t PAGE_COUNT =
    static_cast<size_t>(Page::Count);
//constexpr means the value is calculated during compilation and cannot be changed during runtime
//static just means its only accessible in this cpp file
static lv_obj_t *contentArea = nullptr;
static lv_obj_t *pageTitle = nullptr;

static constexpr size_t MAX_INVERTERS = 20;

static IPAddress inverterIPs[MAX_INVERTERS] = {};
static bool inverterStatuses[MAX_INVERTERS] = {};
static String inverterSerials[MAX_INVERTERS] = {};
static uint8_t inverterSlaveAddresses[MAX_INVERTERS] = {};
static bool inverterIdentityValid[MAX_INVERTERS] = {};
static bool inverterProfileKnown[MAX_INVERTERS] = {};
static uint16_t inverterModelCodes[MAX_INVERTERS] = {};
static givenergy::DeviceModel inverterModels[MAX_INVERTERS] = {};
static givenergy::DeviceProfile inverterProfiles[MAX_INVERTERS] = {};

static size_t inverterCount = 0;

enum class BatteryMetric : uint8_t {
    State,
    Soc,
    BatteryPower,
    BatteryVoltage,
    BatteryTemperature,
    InverterPower,
    PvPower,
    LoadPower,
    GridPower,
    Pv1Voltage,
    Pv2Voltage,
    GridVoltage,
    GridFrequency,
    PvEnergyToday,
    ImportEnergyToday,
    ExportEnergyToday,
    Count
};

static constexpr size_t BATTERY_METRIC_COUNT =
    static_cast<size_t>(BatteryMetric::Count);

static lv_obj_t *batteryMetricLabels[BATTERY_METRIC_COUNT] = {};

enum class ControlUiTone : uint8_t {
    Neutral,
    Busy,
    Success,
    Failure
};

struct ControlUiBinding {
    givenergy::ControlId id =
        givenergy::ControlId::ChargeTargetSoc;
    lv_obj_t *input = nullptr;
    bool isSwitch = false;
};

static constexpr size_t MAX_CONTROL_BINDINGS = 16;
static ControlUiBinding
    controlBindings[MAX_CONTROL_BINDINGS] = {};
static size_t controlBindingCount = 0;
static lv_obj_t *controlStatusLabel = nullptr;
static volatile bool controlRequestPending = false;
static volatile uint8_t requestedControlId = 0;
static volatile uint16_t requestedControlValue = 0;
static bool controlWorkflowActive = false;
static givenergy::ControlId activeControlId =
    givenergy::ControlId::ChargeTargetSoc;
static uint32_t controlWorkflowStartedMs = 0;
static IPAddress controlWorkflowInverterIP;
static String controlStatusText =
    "Select a detected inverter before changing settings";
static ControlUiTone controlStatusTone =
    ControlUiTone::Neutral;

enum class TimedControlMode : uint8_t {
    None,
    Charge,
    Discharge,
    Stop
};

enum class TimedControlPhase : uint8_t {
    Idle,
    WaitingForSettings,
    Applying,
    Running,
    Restoring
};

struct TimedControlSnapshot {
    bool valid = false;

    givenergy::DeviceProfile profile = givenergy::DeviceProfile::SinglePhase;

    TimedControlMode workflowMode = TimedControlMode::None;
    //used by both profiles
    bool batteryPauseModeSupported = false;
    uint16_t batteryPauseMode = 0;

    uint16_t chargeTargetSoc = 80;
    uint16_t socReserve = 4;

    // single phase settings.
    uint16_t enableChargeTarget = 0;
    uint16_t ecoMode = 1;
    uint16_t inverterHour = 0;
    uint16_t inverterMinute = 0;

    uint16_t dischargeSlotStart = 0;
    uint16_t dischargeSlotEnd = 0;
    uint16_t dischargeEnabled = 0;

    uint16_t chargeSlotStart = 0;     // HR94
    uint16_t chargeSlotEnd = 0;       // HR95
    uint16_t chargeEnabled = 0;       // HR96

    uint16_t chargeLimit = 50;        // HR111
    uint16_t dischargeLimit = 50;     // HR112
    
    //Three-phase settings.
    uint16_t chargeLimitAc = 100;
    uint16_t dischargeLimitAc = 100;
    uint16_t acCharge = 0;
    uint16_t forceDischarge = 0;
    uint16_t forceCharge = 0;
};

enum class TimedControlStepKind : uint8_t {
    Control,
    TimeSlot
};

struct TimedControlStep {
    TimedControlStepKind kind = TimedControlStepKind::Control;
    givenergy::ControlId id =givenergy::ControlId::EnableCharge;
    uint16_t value = 0;

    //Only used when kind is TimeSLot
    bool discharge = false;
    uint8_t slot = 1;
    uint16_t startHhmm = 0;
    uint16_t endHhmm = 0;
};
static constexpr uint8_t TIMED_SNAPSHOT_VERSION = 3;
static constexpr size_t MAX_TIMED_CONTROL_STEPS = 20;
static lv_obj_t *timedDurationSlider = nullptr;
static lv_obj_t *timedTargetSlider = nullptr;
static lv_obj_t *timedPowerSlider = nullptr;
static lv_obj_t *timedStatusLabel = nullptr;
static volatile bool timedControlRequestPending = false;
static volatile bool timedControlStopRequested = false;
static volatile uint8_t requestedTimedControlMode = 0;
static volatile uint16_t requestedTimedDurationMinutes = 30;
static volatile uint16_t requestedTimedTargetSoc = 80;
static volatile uint16_t requestedTimedPowerPercent = 100;
static uint8_t timedControlSettingsReadStage = 0;
static TimedControlMode timedControlMode =
    TimedControlMode::None;
static TimedControlPhase timedControlPhase =
    TimedControlPhase::Idle;
static TimedControlSnapshot timedControlSnapshot;
static TimedControlStep
    timedControlSteps[MAX_TIMED_CONTROL_STEPS] = {};
static size_t timedControlStepCount = 0;
static size_t timedControlStepIndex = 0;
static bool timedControlStepPending = false;
static uint8_t timedControlStepRetries = 0;
static bool timedControlSettingsReadQueued = false;
static bool timedControlWaitForPreviousCommand = false;
static bool timedControlRestoreFailed = false;
static bool timedControlStartFailed = false;
static uint16_t timedControlDurationMinutes = 30;
static uint16_t timedControlTargetSoc = 80;
static uint16_t timedControlPowerPercent = 100;
static uint32_t timedControlRequestedMs = 0;
static uint32_t timedControlEndMs = 0;
static uint32_t timedControlLastUiMs = 0;
static uint32_t timedControlSettingsReadGeneration = 0;


// Used to check whether the battery physically started moving power.
static uint32_t timedControlPowerCheckStartedMs = 0;
static bool timedControlPowerSeen = false;

// Ignore very small standby readings.
static constexpr int32_t MINIMUM_ACTIVE_POWER_W = 200;

// Give the inverter 30 seconds to react to the register writes.
static constexpr uint32_t POWER_START_TIMEOUT_MS = 30000;
static uint32_t timedControlStepStartedMs = 0;
static IPAddress timedControlInverterIP;
static bool timedControlRecoveryPending = false;
static bool timedControlRecoveryActive = false;
static bool timedControlRecoveryAttempted = false;
static IPAddress timedControlRecoveryIP;
static TimedControlSnapshot timedControlRecoverySnapshot;
static String timedControlStatusText =
    "Set a duration, SOC target and power limit";
static ControlUiTone timedControlStatusTone =
    ControlUiTone::Neutral;

//These will point to permanent parts of the interface
//The 

static lv_obj_t *navButtons[PAGE_COUNT] = {};

static const char *PAGE_NAMES[PAGE_COUNT] = {
    "Home",
    "Inverters",
    "Battery",
    "Cells",
    "Controls",
    "Faults",
    "Wi-Fi",
    "Settings"
};

static void inverterRefreshEvent(lv_event_t *event);
static void inverterSelectEvent(lv_event_t *event);
static void inverterIpFieldEvent(lv_event_t *event);
static void inverterIpKeyboardEvent(lv_event_t *event);
static void inverterIpAddEvent(lv_event_t *event);
static void controlSetEvent(lv_event_t *event);
static void wifiRefreshEvent(lv_event_t *event);
static void updateBatteryPage(lv_timer_t *timer);
static void updateCellsPage(lv_timer_t *timer);
static void updateCellTelemetry();
static void updateControlWorkflow();
static void updateTimedControlWorkflow();
static bool timedControlBusy();
static void loadTimedControlRecovery();

static lv_color_t colourBackground()
{
    return lv_color_hex(0x0B1220);
}

static lv_color_t colourSidebar()
{
    return lv_color_hex(0x111B2E);
}

static lv_color_t colourPanel()
{
    return lv_color_hex(0x18243A);
}

static lv_color_t colourAccent()
{
    return lv_color_hex(0x2563EB);
}

static lv_color_t colourText()
{
    return lv_color_hex(0xF8FAFC);
}

static lv_color_t colourMuted()
{
    return lv_color_hex(0x94A3B8);
}

static void configureContainer(lv_obj_t *object, lv_color_t colour)
{
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(object, colour, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

static lv_obj_t *createText(
    lv_obj_t *parent,
    const char *text,
    int x,
    int y,
    lv_color_t colour)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, colour, 0);

    return label;
}

static void createMetricCard(
    lv_obj_t *parent,
    int x,
    const char *name,
    const char *value)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, x, 52);
    lv_obj_set_size(card, 145, 92);

    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, colourPanel(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);

    createText(card, name, 12, 10, colourMuted());
    createText(card, value, 12, 44, colourText());
}

static void createHomePage()
{
    createText(
        contentArea,
        "Site overview",
        18,
        15,
        colourText());

    createMetricCard(contentArea, 15,  "Solar",   "11.2 kW");
    createMetricCard(contentArea, 170, "Load",    "8.4 kW");
    createMetricCard(contentArea, 325, "Grid",    "1.3 kW");
    createMetricCard(contentArea, 480, "Battery", "68%");

    lv_obj_t *flowPanel = lv_obj_create(contentArea);

    lv_obj_set_pos(flowPanel, 15, 163);
    lv_obj_set_size(flowPanel, 610, 90);

    lv_obj_clear_flag(flowPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(flowPanel, colourPanel(), 0);
    lv_obj_set_style_bg_opa(flowPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(flowPanel, 0, 0);
    lv_obj_set_style_radius(flowPanel, 12, 0);

    lv_obj_t *flowLabel = lv_label_create(flowPanel);
    lv_label_set_text(
        flowLabel,
        "SOLAR  11.2 kW   ->   SITE  8.4 kW   ->   BATTERY  1.5 kW");

    lv_obj_set_style_text_color(flowLabel, colourText(), 0);
    lv_obj_center(flowLabel);

    createText(
        contentArea,
        "",
        18,
        280,
        lv_color_hex(0x22C55E));

    createText(
        contentArea,
        "",
        18,
        315,
        colourMuted());
}

static void createInverterRow(
    size_t inverterIndex,
    int rowNumber,
    const char *name,
    const char *address,
    bool online)
{
    const int y = 105 + (rowNumber * 57);

    lv_obj_t *row = lv_obj_create(contentArea);

    lv_obj_set_pos(row, 15, y);
    lv_obj_set_size(row, 610, 49);

    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(row, colourPanel(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);

    const bool selected =
        selectedInverterConfigured &&
        inverterIPs[inverterIndex] == selectedInverterIP;

    if (selected) {
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, colourAccent(), 0);
    }

    createText(row, name, 12, 14, colourText());
    createText(row, address, 175, 14, colourMuted());
    createText(
        row,
        online ? "Online" : "Offline",
        405,
        14,
        online ? lv_color_hex(0x22C55E)
               : lv_color_hex(0xEF4444));

    lv_obj_t *selectButton = lv_btn_create(row);
    lv_obj_set_pos(selectButton, 500, 7);
    lv_obj_set_size(selectButton, 95, 35);
    lv_obj_set_style_bg_color(
        selectButton,
        selected ? lv_color_hex(0x16A34A) : colourAccent(),
        0);

    lv_obj_add_event_cb(
        selectButton,
        inverterSelectEvent,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(
            static_cast<uintptr_t>(inverterIndex)));

    lv_obj_t *selectLabel = lv_label_create(selectButton);
    lv_label_set_text(selectLabel, selected ? "Selected" : "Add");
    lv_obj_center(selectLabel);
}   //you can use a ? after to decide true or false if you put : thats so cool

static void createInvertersPage()
{ //size_t is an unsigned integer type designed for array indexes, object counts
    //memory sizes, results from sizeof() etc
    //basically int but it can't be negative 
    createText(
        contentArea,
        "Detected devices",
        18,
        15,
        colourText());

    lv_obj_t *refreshButton = lv_btn_create(contentArea);
    lv_obj_set_pos(refreshButton, 500, 8);
    lv_obj_set_size(refreshButton, 125, 38);
    lv_obj_add_event_cb(
        refreshButton,
        inverterRefreshEvent,
        LV_EVENT_CLICKED,
        nullptr);

    lv_obj_t *refreshLabel = lv_label_create(refreshButton);
    lv_label_set_text(refreshLabel, "Refresh");
    lv_obj_center(refreshLabel);

    inverterIpField = lv_textarea_create(contentArea);
    lv_obj_set_pos(inverterIpField, 18, 52);
    lv_obj_set_size(inverterIpField, 205, 42);
    lv_textarea_set_one_line(inverterIpField, true);
    lv_textarea_set_max_length(inverterIpField, 15);
    lv_textarea_set_accepted_chars(
        inverterIpField,
        "0123456789.");
    lv_textarea_set_placeholder_text(
        inverterIpField,
        "192.168.1.133");

    if (!inverterIpText.isEmpty()) {
        lv_textarea_set_text(
            inverterIpField,
            inverterIpText.c_str());
    }

    lv_obj_add_event_cb(
        inverterIpField,
        inverterIpFieldEvent,
        LV_EVENT_ALL,
        nullptr);

    inverterIpAddButton = lv_btn_create(contentArea);
    lv_obj_set_pos(inverterIpAddButton, 235, 52);
    lv_obj_set_size(inverterIpAddButton, 105, 42);
    lv_obj_add_event_cb(
        inverterIpAddButton,
        inverterIpAddEvent,
        LV_EVENT_CLICKED,
        nullptr);

    lv_obj_t *addIpLabel =
        lv_label_create(inverterIpAddButton);
    lv_label_set_text(addIpLabel, "Add IP");
    lv_obj_center(addIpLabel);

    inverterIpStatusLabel = createText(
        contentArea,
        inverterIpStatus.c_str(),
        355,
        64,
        colourMuted());
    lv_obj_set_width(inverterIpStatusLabel, 270);
    lv_label_set_long_mode(
        inverterIpStatusLabel,
        LV_LABEL_LONG_DOT);

    inverterIpKeyboard = lv_keyboard_create(contentArea);
    lv_obj_set_size(inverterIpKeyboard, 440, 220);
    lv_obj_align(
        inverterIpKeyboard,
        LV_ALIGN_BOTTOM_RIGHT,
        0,
        0);
    lv_keyboard_set_mode(
        inverterIpKeyboard,
        LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(
        inverterIpKeyboard,
        inverterIpField);
    lv_obj_add_event_cb(
        inverterIpKeyboard,
        inverterIpKeyboardEvent,
        LV_EVENT_ALL,
        nullptr);
    lv_obj_add_flag(
        inverterIpKeyboard,
        LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(
        inverterIpKeyboard,
        LV_OBJ_FLAG_FLOATING);

    if (connectivity.getState() !=
            Connectivity::WifiState::Connected) {
        lv_obj_add_state(refreshButton, LV_STATE_DISABLED);
        lv_obj_add_state(
            inverterIpField,
            LV_STATE_DISABLED);
        lv_obj_add_state(
            inverterIpAddButton,
            LV_STATE_DISABLED);

        createText(
            contentArea,
            "Connect to Wi-Fi before scanning",
            18,
            108,
            colourMuted());
        return;
    }

    const givenergy::GivEnergyScanner::State scanState =
        inverterScanner.state();

    if (!inverterScanTriggered) {
        createText(
            contentArea,
            "Waiting to start network scan...",
            18,
            108,
            colourMuted());
        return;
    }

    if (scanState ==
            givenergy::GivEnergyScanner::State::Scanning) {
        lv_obj_add_state(refreshButton, LV_STATE_DISABLED);
        lv_obj_add_state(
            inverterIpField,
            LV_STATE_DISABLED);
        lv_obj_add_state(
            inverterIpAddButton,
            LV_STATE_DISABLED);

        const IPAddress candidate =
            inverterScanner.currentAddress();

        createText(
            contentArea,
            inverterScanMode ==
                    InverterScanMode::ManualAddress
                ? "Testing entered inverter address..."
                : "Scanning for GivEnergy inverters...",
            18,
            108,
            lv_color_hex(0xF59E0B));

        const String checking =
            "Checking " +
            candidate.toString() +
            ":8899  slave 0x" +
            String(
                inverterScanner.currentSlaveAddress(),
                HEX);

        inverterScanAddressLabel = createText(
            contentArea,
            checking.c_str(),
            18,
            138,
            colourMuted());

        displayedInverterHost = inverterScanner.currentHost();
        displayedInverterSlaveAddress =
            inverterScanner.currentSlaveAddress();
        return;
    }

    if (scanState ==
            givenergy::GivEnergyScanner::State::Failed) {
        createText(
            contentArea,
            "Inverter scan failed",
            18,
            108,
            lv_color_hex(0xEF4444));
        return;
    }

    if (inverterCount == 0) {
        createText(
            contentArea,
            "No GivEnergy inverters detected",
            18,
            108,
            colourMuted());
        return;
    }

    if (inverterCount > 5) {
        lv_obj_add_flag(contentArea, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(contentArea, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(contentArea, LV_SCROLLBAR_MODE_AUTO);
    }

    for (size_t i = 0; i < inverterCount; ++i) {
        String name = inverterSerials[i];

        if (name.isEmpty() || name == "Unknown") {
            name = "Unknown inverter";
        }

        String address = inverterIPs[i].toString();
        address += "  |  ";
        address += inverterProfileKnown[i]
            ? givenergy::deviceProfileName(inverterProfiles[i])
            : "profile unknown";

        createInverterRow(
            i,
            static_cast<int>(i),
            name.c_str(),
            address.c_str(),
            inverterStatuses[i]);
    }//static_cast converts i from size_t into an int
}

static lv_obj_t *createBatteryMetricCard(
    lv_obj_t *parent,
    BatteryMetric metric,
    const char *name,
    int column,
    int row)
{
    const int x = column == 0 ? 0 : 305;
    const int y = row * 37;

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, 292, 34);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, colourPanel(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 7, 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    createText(card, name, 9, 9, colourMuted());

    lv_obj_t *value = createText(
        card,
        "--",
        174,
        9,
        colourText());

    lv_obj_set_width(value, 108);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);

    batteryMetricLabels[static_cast<size_t>(metric)] = value;
    return value;
}

static void setBatteryMetricValue(
    BatteryMetric metric,
    const String &value)
{
    lv_obj_t *label =
        batteryMetricLabels[static_cast<size_t>(metric)];

    if (label != nullptr) {
        lv_label_set_text(label, value.c_str());
    }
}

static const char *clientStateText(givenergy::ClientState state)
{
    switch (state) {
        case givenergy::ClientState::Stopped:
            return "Stopped";
        case givenergy::ClientState::WifiUnavailable:
            return "Wi-Fi unavailable";
        case givenergy::ClientState::Connecting:
            return "Connecting";
        case givenergy::ClientState::Online:
            return "Connected";
        case givenergy::ClientState::WaitingForResponse:
            return "Reading registers";
    }

    return "Unknown";
}

static void clearBatteryMetricValues()
{
    for (size_t i = 0; i < BATTERY_METRIC_COUNT; ++i) {
        if (batteryMetricLabels[i] != nullptr) {
            lv_label_set_text(batteryMetricLabels[i], "--");
        }
    }
}

static void createBatteryPage()
{
    createText(
        contentArea,
        "Selected inverter monitoring",
        18,
        12,
        colourText());

    batteryIdentityLabel = createText(
        contentArea,
        selectedInverterConfigured
            ? "Loading selected inverter..."
            : "No inverter selected",
        18,
        38,
        colourMuted());

    batteryConnectionLabel = createText(
        contentArea,
        selectedInverterConfigured
            ? "Preparing connection..."
            : "Open Inverters and press Add",
        18,
        62,
        selectedInverterConfigured
            ? lv_color_hex(0xF59E0B)
            : colourMuted());

    batteryUpdatedLabel = createText(
        contentArea,
        "Waiting for data",
        390,
        62,
        colourMuted());

    lv_obj_t *metricsPanel = lv_obj_create(contentArea);
    lv_obj_set_pos(metricsPanel, 15, 96);
    lv_obj_set_size(metricsPanel, 610, 306);
    lv_obj_clear_flag(metricsPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(metricsPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(metricsPanel, 0, 0);
    lv_obj_set_style_pad_all(metricsPanel, 0, 0);

    createBatteryMetricCard(
        metricsPanel, BatteryMetric::State, "Status register", 0, 0);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::Soc, "Battery SOC", 0, 1);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::BatteryPower, "Battery power", 0, 2);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::BatteryVoltage, "Battery voltage", 0, 3);
    createBatteryMetricCard(
        metricsPanel,
        BatteryMetric::BatteryTemperature,
        "Battery temperature",
        0,
        4);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::InverterPower, "Inverter power", 0, 5);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::PvPower, "PV power", 0, 6);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::LoadPower, "Load power", 0, 7);

    createBatteryMetricCard(
        metricsPanel, BatteryMetric::GridPower, "Grid power", 1, 0);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::Pv1Voltage, "PV1 voltage", 1, 1);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::Pv2Voltage, "PV2 voltage", 1, 2);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::GridVoltage, "Grid voltage", 1, 3);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::GridFrequency, "Grid frequency", 1, 4);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::PvEnergyToday, "PV today", 1, 5);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::ImportEnergyToday, "Imported today", 1, 6);
    createBatteryMetricCard(
        metricsPanel, BatteryMetric::ExportEnergyToday, "Exported today", 1, 7);

    updateBatteryPage(nullptr);
}

static void updateBatteryPage(lv_timer_t *timer)
{
    (void)timer;

    if (currentPage != Page::Battery ||
            batteryIdentityLabel == nullptr ||
            batteryConnectionLabel == nullptr) {
        return;
    }

    if (!selectedInverterConfigured) {
        lv_label_set_text(
            batteryIdentityLabel,
            "No inverter selected");
        lv_label_set_text(
            batteryConnectionLabel,
            "Open Inverters and press Add");
        lv_obj_set_style_text_color(
            batteryConnectionLabel,
            colourMuted(),
            0);
        clearBatteryMetricValues();
        return;
    }

    const String identity =
        (selectedInverterSerial.isEmpty()
             ? String("Unknown inverter")
             : selectedInverterSerial) +
        "  |  " +
        selectedInverterIP.toString() +
        "  |  slave 0x" +
        String(selectedInverterSlaveAddress, HEX) +
        "  |  " +
        givenergy::deviceModelName(selectedInverterModel) +
        " (0x" +
        String(selectedInverterModelCode, HEX) +
        ")";

    lv_label_set_text(batteryIdentityLabel, identity.c_str());

    if (!selectedInverterProfileKnown) {
        lv_label_set_text(
            batteryConnectionLabel,
            "Identity/profile unknown - monitoring disabled");
        lv_obj_set_style_text_color(
            batteryConnectionLabel,
            lv_color_hex(0xF59E0B),
            0);
        if (batteryUpdatedLabel != nullptr) {
            lv_label_set_text(
                batteryUpdatedLabel,
                "Port 8899 answered, but identity registers did not");
        }
        clearBatteryMetricValues();
        return;
    }

    const givenergy::ClientState state =
        selectedInverterClient.state();
    lv_label_set_text(
        batteryConnectionLabel,
        clientStateText(state));

    const bool online =
        state == givenergy::ClientState::Online ||
        state == givenergy::ClientState::WaitingForResponse;

    lv_obj_set_style_text_color(
        batteryConnectionLabel,
        online ? lv_color_hex(0x22C55E)
               : lv_color_hex(0xF59E0B),
        0);

    givenergy::LiveData live;
    if (!selectedInverterClient.liveData(live) || !live.valid) {
        clearBatteryMetricValues();

        if (batteryUpdatedLabel != nullptr) {
            const char *error = selectedInverterClient.lastError();
            lv_label_set_text(
                batteryUpdatedLabel,
                error != nullptr && error[0] != '\0'
                    ? error
                    : "Waiting for first register response");
        }
        return;
    }

    setBatteryMetricValue(
        BatteryMetric::State,
        String(live.status));
    setBatteryMetricValue(
        BatteryMetric::Soc,
        String(live.batterySocPercent) + "%");
    setBatteryMetricValue(
        BatteryMetric::BatteryPower,
        String(live.batteryPowerW) + " W");
    setBatteryMetricValue(
        BatteryMetric::BatteryVoltage,
        String(live.batteryVoltageV, 1) + " V");
    setBatteryMetricValue(
        BatteryMetric::BatteryTemperature,
        String(live.batteryTemperatureC, 1) + " C");
    setBatteryMetricValue(
        BatteryMetric::InverterPower,
        String(live.inverterPowerW) + " W");
    setBatteryMetricValue(
        BatteryMetric::PvPower,
        String(live.pvPowerW) + " W");
    setBatteryMetricValue(
        BatteryMetric::LoadPower,
        String(live.loadPowerW) + " W");
    setBatteryMetricValue(
        BatteryMetric::GridPower,
        String(live.gridPowerW) + " W");
    setBatteryMetricValue(
        BatteryMetric::Pv1Voltage,
        String(live.pv1VoltageV, 1) + " V");
    setBatteryMetricValue(
        BatteryMetric::Pv2Voltage,
        String(live.pv2VoltageV, 1) + " V");
    setBatteryMetricValue(
        BatteryMetric::GridVoltage,
        String(live.gridVoltageV, 1) + " V");
    setBatteryMetricValue(
        BatteryMetric::GridFrequency,
        String(live.gridFrequencyHz, 2) + " Hz");
    setBatteryMetricValue(
        BatteryMetric::PvEnergyToday,
        String(live.pvEnergyTodayKWh, 1) + " kWh");
    setBatteryMetricValue(
        BatteryMetric::ImportEnergyToday,
        String(live.importEnergyTodayKWh, 1) + " kWh");
    setBatteryMetricValue(
        BatteryMetric::ExportEnergyToday,
        String(live.exportEnergyTodayKWh, 1) + " kWh");

    if (batteryUpdatedLabel != nullptr) {
        const uint32_t responseMs =
            selectedInverterClient.lastSuccessfulResponseMs();
        const uint32_t ageSeconds =
            responseMs == 0 ? 0 : (millis() - responseMs) / 1000;
        const String updated =
            "Updated " + String(ageSeconds) + "s ago";
        lv_label_set_text(batteryUpdatedLabel, updated.c_str());
    }
}

static void hideAllCellRows()
{
    for (size_t i = 0;
            i < givenergy::kMaximumCellsPerBattery;
            ++i) {
        if (cellRows[i] != nullptr) {
            lv_obj_add_flag(
                cellRows[i],
                LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void updateCellsDropdown()
{
    if (cellsBatteryDropdown == nullptr) {
        return;
    }

    const size_t count =
        selectedInverterClient.batteryCount();
    if (count == 0) {
        lv_dropdown_set_options(
            cellsBatteryDropdown,
            "No battery modules found");
        lv_dropdown_set_selected(
            cellsBatteryDropdown,
            0);
        selectedBatteryIndex = 0;
        return;
    }

    String options;
    for (size_t i = 0; i < count; ++i) {
        const givenergy::BatteryData *battery =
            selectedInverterClient.battery(i);
        if (battery == nullptr) {
            continue;
        }

        if (!options.isEmpty()) {
            options += "\n";
        }
        options += "Battery ";
        options += String(i + 1);
        options += ": ";
        options += battery->serial;
        options += " (0x";
        options += String(battery->slaveAddress, HEX);
        options += ")";
    }

    lv_dropdown_set_options(
        cellsBatteryDropdown,
        options.c_str());

    if (selectedBatteryIndex >= count) {
        selectedBatteryIndex = 0;
    }
    lv_dropdown_set_selected(
        cellsBatteryDropdown,
        static_cast<uint16_t>(selectedBatteryIndex));
}

static void cellsBatterySelectedEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_obj_t *dropdown = lv_event_get_target(event);
    selectedBatteryIndex =
        lv_dropdown_get_selected(dropdown);
    updateCellsPage(nullptr);
}

static void cellsRefreshEvent(lv_event_t *event)
{
    (void)event;
    batteryRediscoveryRequested = true;

    if (cellsStatusLabel != nullptr) {
        lv_label_set_text(
            cellsStatusLabel,
            "Battery scan requested...");
        lv_obj_set_style_text_color(
            cellsStatusLabel,
            lv_color_hex(0xF59E0B),
            0);
    }
}

static lv_obj_t *createCellColumnLabel(
    lv_obj_t *parent,
    const char *text,
    int x,
    int width,
    lv_color_t colour)
{
    lv_obj_t *label =
        createText(parent, text, x, 11, colour);
    lv_obj_set_width(label, width);
    return label;
}

static void createCellsPage()
{
    lv_obj_add_flag(
        contentArea,
        LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(
        contentArea,
        LV_DIR_VER);
    lv_obj_set_scrollbar_mode(
        contentArea,
        LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(
        contentArea,
        16,
        0);

    createText(
        contentArea,
        "Battery-module cell telemetry",
        15,
        11,
        colourText());

    lv_obj_t *refreshButton =
        lv_btn_create(contentArea);
    lv_obj_set_pos(refreshButton, 517, 7);
    lv_obj_set_size(refreshButton, 108, 36);
    lv_obj_set_style_bg_color(
        refreshButton,
        colourAccent(),
        0);
    lv_obj_set_style_radius(
        refreshButton,
        7,
        0);
    lv_obj_add_event_cb(
        refreshButton,
        cellsRefreshEvent,
        LV_EVENT_CLICKED,
        nullptr);

    lv_obj_t *refreshLabel =
        lv_label_create(refreshButton);
    lv_label_set_text(
        refreshLabel,
        "Refresh");
    lv_obj_center(refreshLabel);

    cellsBatteryDropdown =
        lv_dropdown_create(contentArea);
    lv_obj_set_pos(
        cellsBatteryDropdown,
        15,
        52);
    lv_obj_set_size(
        cellsBatteryDropdown,
        295,
        40);
    lv_obj_add_event_cb(
        cellsBatteryDropdown,
        cellsBatterySelectedEvent,
        LV_EVENT_VALUE_CHANGED,
        nullptr);

    cellsStatusLabel = createText(
        contentArea,
        "Waiting for battery data",
        330,
        64,
        colourMuted());
    lv_obj_set_width(
        cellsStatusLabel,
        295);
    lv_label_set_long_mode(
        cellsStatusLabel,
        LV_LABEL_LONG_CLIP);

    lv_obj_t *summaryCard =
        lv_obj_create(contentArea);
    lv_obj_set_pos(summaryCard, 15, 104);
    lv_obj_set_size(summaryCard, 610, 130);
    lv_obj_clear_flag(
        summaryCard,
        LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(
        summaryCard,
        colourPanel(),
        0);
    lv_obj_set_style_bg_opa(
        summaryCard,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_border_width(
        summaryCard,
        0,
        0);
    lv_obj_set_style_radius(
        summaryCard,
        8,
        0);
    lv_obj_set_style_pad_all(
        summaryCard,
        0,
        0);

    cellsSummaryLabel = createText(
        summaryCard,
        "No cell telemetry received",
        12,
        10,
        colourMuted());
    lv_obj_set_width(
        cellsSummaryLabel,
        586);
    lv_label_set_long_mode(
        cellsSummaryLabel,
        LV_LABEL_LONG_WRAP);

    lv_obj_t *detailsCard =
        lv_obj_create(contentArea);
    lv_obj_set_pos(detailsCard, 15, 246);
    lv_obj_set_size(detailsCard, 610, 205);
    lv_obj_clear_flag(
        detailsCard,
        LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(
        detailsCard,
        colourSidebar(),
        0);
    lv_obj_set_style_bg_opa(
        detailsCard,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_border_width(
        detailsCard,
        0,
        0);
    lv_obj_set_style_radius(
        detailsCard,
        8,
        0);
    lv_obj_set_style_pad_all(
        detailsCard,
        0,
        0);

    cellsDetailsLabel = createText(
        detailsCard,
        "Additional module data will appear here.",
        12,
        10,
        colourMuted());
    lv_obj_set_width(
        cellsDetailsLabel,
        586);
    lv_label_set_long_mode(
        cellsDetailsLabel,
        LV_LABEL_LONG_WRAP);

    lv_obj_t *header =
        lv_obj_create(contentArea);
    lv_obj_set_pos(header, 15, 463);
    lv_obj_set_size(header, 610, 38);
    lv_obj_clear_flag(
        header,
        LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(
        header,
        colourSidebar(),
        0);
    lv_obj_set_style_bg_opa(
        header,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_border_width(
        header,
        0,
        0);
    lv_obj_set_style_radius(
        header,
        7,
        0);
    lv_obj_set_style_pad_all(
        header,
        0,
        0);

    createCellColumnLabel(
        header, "Cell", 10, 58, colourMuted());
    createCellColumnLabel(
        header, "Voltage", 76, 90, colourMuted());
    createCellColumnLabel(
        header, "Temperature", 180, 220, colourMuted());
    createCellColumnLabel(
        header, "Delta", 422, 75, colourMuted());
    createCellColumnLabel(
        header, "State", 520, 78, colourMuted());

    for (size_t i = 0;
            i < givenergy::kMaximumCellsPerBattery;
            ++i) {
        lv_obj_t *row =
            lv_obj_create(contentArea);
        cellRows[i] = row;
        lv_obj_set_pos(
            row,
            15,
            508 + static_cast<int>(i) * 44);
        lv_obj_set_size(row, 610, 39);
        lv_obj_clear_flag(
            row,
            LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(
            row,
            (i % 2U) == 0
                ? colourPanel()
                : colourSidebar(),
            0);
        lv_obj_set_style_bg_opa(
            row,
            LV_OPA_COVER,
            0);
        lv_obj_set_style_border_width(
            row,
            0,
            0);
        lv_obj_set_style_radius(
            row,
            6,
            0);
        lv_obj_set_style_pad_all(
            row,
            0,
            0);

        const String cellName =
            "Cell " + String(i + 1);
        createCellColumnLabel(
            row,
            cellName.c_str(),
            10,
            58,
            colourText());
        cellVoltageLabels[i] =
            createCellColumnLabel(
                row,
                "--",
                76,
                90,
                colourText());
        cellTemperatureLabels[i] =
            createCellColumnLabel(
                row,
                "--",
                180,
                220,
                colourText());
        cellDeltaLabels[i] =
            createCellColumnLabel(
                row,
                "--",
                422,
                75,
                colourText());
        cellStateLabels[i] =
            createCellColumnLabel(
                row,
                "--",
                520,
                78,
                colourText());
        lv_obj_add_flag(
            row,
            LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *temperatureNote = createText(
        contentArea,
        "Only measurements actually exposed by the BMS are shown. LV modules "
        "provide 16 voltages but four grouped temperatures; they do not "
        "provide per-cell current, SOC, resistance or health.",
        15,
        1575,
        colourMuted());
    lv_obj_set_width(
        temperatureNote,
        610);
    lv_label_set_long_mode(
        temperatureNote,
        LV_LABEL_LONG_WRAP);

    updateCellsDropdown();
    displayedBatteryGeneration =
        selectedInverterClient.batteryGeneration();
    batteryRediscoveryRequested = true;
    updateCellsPage(nullptr);
}

static void updateCellsPage(lv_timer_t *timer)
{
    (void)timer;

    if (currentPage != Page::Cells ||
            cellsBatteryDropdown == nullptr ||
            cellsStatusLabel == nullptr ||
            cellsSummaryLabel == nullptr ||
            cellsDetailsLabel == nullptr) {
        return;
    }

    const uint32_t generation =
        selectedInverterClient.batteryGeneration();
    if (displayedBatteryGeneration != generation) {
        displayedBatteryGeneration = generation;
        updateCellsDropdown();
    }

    if (!selectedInverterConfigured) {
        lv_label_set_text(
            cellsStatusLabel,
            "No inverter selected");
        lv_label_set_text(
            cellsSummaryLabel,
            "Open Inverters and add an inverter first.");
        lv_label_set_text(cellsDetailsLabel, "");
        hideAllCellRows();
        return;
    }

    if (!selectedInverterProfileKnown) {
        lv_label_set_text(
            cellsStatusLabel,
            "Inverter profile unknown");
        lv_label_set_text(
            cellsSummaryLabel,
            "Cell polling requires a recognised inverter profile.");
        lv_label_set_text(cellsDetailsLabel, "");
        hideAllCellRows();
        return;
    }

    if (selectedInverterClient.batteryRefreshInProgress()) {
        lv_label_set_text(
            cellsStatusLabel,
            "Reading battery modules...");
        lv_obj_set_style_text_color(
            cellsStatusLabel,
            lv_color_hex(0xF59E0B),
            0);
    } else if (selectedInverterClient.batteryCount() == 0) {
        lv_label_set_text(
            cellsStatusLabel,
            "No battery modules replied");
        lv_obj_set_style_text_color(
            cellsStatusLabel,
            colourMuted(),
            0);
        lv_label_set_text(
            cellsSummaryLabel,
            "No supported battery cell-data block was returned. "
            "Press Refresh to scan the battery addresses again.");
        lv_label_set_text(cellsDetailsLabel, "");
        hideAllCellRows();
        return;
    } else {
        const uint32_t refreshedMs =
            selectedInverterClient.lastBatteryRefreshMs();
        const uint32_t ageSeconds =
            refreshedMs == 0
                ? 0
                : (millis() - refreshedMs) / 1000U;
        const String status =
            "Updated " + String(ageSeconds) + "s ago";
        lv_label_set_text(
            cellsStatusLabel,
            status.c_str());
        lv_obj_set_style_text_color(
            cellsStatusLabel,
            lv_color_hex(0x22C55E),
            0);
    }

    const givenergy::BatteryData *battery =
        selectedInverterClient.battery(
            selectedBatteryIndex);
    if (battery == nullptr || !battery->valid) {
        lv_label_set_text(
            cellsSummaryLabel,
            "Waiting for valid cell telemetry.");
        lv_label_set_text(cellsDetailsLabel, "");
        hideAllCellRows();
        return;
    }

    bool haveMinimumVoltage = false;
    float minimumVoltage = 0;
    float maximumVoltage = 0;
    float voltageTotal = 0;
    size_t validVoltageCount = 0;
    size_t minimumVoltageCell = 0;
    size_t maximumVoltageCell = 0;
    float temperatureTotal = 0;
    size_t validTemperatureCount = 0;
    for (size_t i = 0; i < battery->cellCount; ++i) {
        const givenergy::CellData &cell =
            battery->cells[i];
        if (cell.temperatureValid) {
            temperatureTotal += cell.temperatureC;
            ++validTemperatureCount;
        }
        if (cell.voltageValid) {
            voltageTotal += cell.voltageV;
            ++validVoltageCount;
            if (!haveMinimumVoltage ||
                cell.voltageV < minimumVoltage) {
                minimumVoltage = cell.voltageV;
                minimumVoltageCell = i;
            }
            if (!haveMinimumVoltage ||
                cell.voltageV > maximumVoltage) {
                maximumVoltage = cell.voltageV;
                maximumVoltageCell = i;
            }
            haveMinimumVoltage = true;
        }
    }

    String summary =
        String(battery->serial) +
        "  |  " +
        givenergy::batteryTelemetryFormatName(
            battery->format) +
        "  |  slave 0x" +
        String(battery->slaveAddress, HEX) +
        "\n";

    summary += "Cells: ";
    summary += String(battery->cellCount);
    summary += "   Average: ";
    summary += validVoltageCount > 0
        ? String(
              voltageTotal /
                  static_cast<float>(validVoltageCount),
              3) +
              " V"
        : String("--");
    summary += "   Temperature average: ";
    summary += validTemperatureCount > 0
        ? String(
              temperatureTotal /
                  static_cast<float>(validTemperatureCount),
              1) +
              " C"
        : String("--");
    summary += "\nLowest/highest: ";
    if (haveMinimumVoltage) {
        summary += "Cell ";
        summary += String(minimumVoltageCell + 1);
        summary += " ";
        summary += String(minimumVoltage, 3);
        summary += " V / Cell ";
        summary += String(maximumVoltageCell + 1);
        summary += " ";
        summary += String(maximumVoltage, 3);
        summary += " V   Spread: ";
        summary += String(
            static_cast<int>(
                (maximumVoltage - minimumVoltage) *
                1000.0F +
                0.5F));
        summary += " mV";
    } else {
        summary += "--";
    }
    summary += "\n";

    String details = "SOC: ";
    details += battery->socValid
        ? String(battery->socPercent) + "%"
        : String("--");
    details += "   Cell sum: ";
    details += battery->packVoltageValid
        ? String(battery->packVoltageV, 3) + " V"
        : String("--");
    details += "   Output: ";
    details += battery->outputVoltageValid
        ? String(battery->outputVoltageV, 3) + " V"
        : String("--");
    details += "   BMS MOSFET: ";
    details += battery->bmsTemperatureValid
        ? String(battery->bmsTemperatureC, 1) + " C"
        : String("--");
    details += "\nTemperature min/max: ";
    details += battery->minimumTemperatureValid
        ? String(battery->minimumTemperatureC, 1) + " C"
        : String("--");
    details += " / ";
    details += battery->maximumTemperatureValid
        ? String(battery->maximumTemperatureC, 1) + " C"
        : String("--");
    details += "\nCapacity calibrated/remaining/design/secondary: ";
    details += battery->calibratedCapacityValid
        ? String(battery->calibratedCapacityAh, 2) + " Ah"
        : String("--");
    details += " / ";
    details += battery->remainingCapacityValid
        ? String(battery->remainingCapacityAh, 2) + " Ah"
        : String("--");
    details += " / ";
    details += battery->designCapacityValid
        ? String(battery->designCapacityAh, 2) + " Ah"
        : String("--");
    details += " / ";
    details += battery->secondaryDesignCapacityValid
        ? String(battery->secondaryDesignCapacityAh, 2) + " Ah"
        : String("--");
    details += "\nEnergy charged/discharged total: ";
    details += battery->chargeEnergyTotalValid
        ? String(battery->chargeEnergyTotalKWh, 1) + " kWh"
        : String("--");
    details += " / ";
    details += battery->dischargeEnergyTotalValid
        ? String(battery->dischargeEnergyTotalKWh, 1) + " kWh"
        : String("--");
    details += "   Cycles: ";
    details += battery->cyclesValid
        ? String(battery->cycles)
        : String("--");
    details += "\nFirmware: ";
    details += battery->firmwareValid
        ? String(battery->firmwareVersion)
        : String("--");
    details += "   USB: ";
    details += battery->usbDeviceValid
        ? String(givenergy::batteryUsbDeviceName(
              battery->usbDevice))
        : String("--");
    details += "\nWarnings: ";
    if (battery->warningsValid &&
            battery->warning1 == 0 &&
            battery->warning2 == 0) {
        details += "None";
    } else if (battery->warningsValid) {
        details += "Active (raw 0x";
        if (battery->warning1 < 0x10) {
            details += "0";
        }
        details += String(battery->warning1, HEX);
        if (battery->warning2 < 0x10) {
            details += "0";
        }
        details += String(battery->warning2, HEX);
        details += "; manufacturer bit meanings undocumented)";
    } else {
        details += "--";
    }
    details += "\nManufacturer status bytes: ";
    if (battery->statusValid) {
        for (size_t i = 0; i < 7; ++i) {
            if (i != 0) {
                details += " ";
            }
            if (battery->status[i] < 0x10) {
                details += "0";
            }
            details += String(battery->status[i], HEX);
        }
    } else {
        details += "--";
    }

    /*
     * The HV BMU cell block contains individual voltage, temperature and
     * serial values only. Pack-wide BCU fields therefore remain "--" instead
     * of being guessed from unrelated inverter registers.
     */

    lv_label_set_text(
        cellsDetailsLabel,
        details.c_str());
    lv_obj_set_style_text_color(
        cellsDetailsLabel,
        colourText(),
        0);

    lv_label_set_text(
        cellsSummaryLabel,
        summary.c_str());
    lv_obj_set_style_text_color(
        cellsSummaryLabel,
        colourText(),
        0);

    for (size_t i = 0;
            i < givenergy::kMaximumCellsPerBattery;
            ++i) {
        if (cellRows[i] == nullptr) {
            continue;
        }

        if (i >= battery->cellCount) {
            lv_obj_add_flag(
                cellRows[i],
                LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(
            cellRows[i],
            LV_OBJ_FLAG_HIDDEN);
        const givenergy::CellData &cell =
            battery->cells[i];

        const String voltage =
            cell.voltageValid
                ? String(cell.voltageV, 3) + " V"
                : String("--");
        lv_label_set_text(
            cellVoltageLabels[i],
            voltage.c_str());

        String temperature = "--";
        if (cell.temperatureValid) {
            temperature =
                String(cell.temperatureC, 1) +
                " C";
            if (cell.temperatureIsGrouped) {
                const size_t first =
                    (static_cast<size_t>(
                        cell.temperatureGroup) -
                     1U) *
                        4U +
                    1U;
                temperature += " (T";
                temperature += String(
                    cell.temperatureGroup);
                temperature += ": ";
                temperature += String(first);
                temperature += "-";
                temperature += String(first + 3U);
                temperature += ")";
            }
        }
        lv_label_set_text(
            cellTemperatureLabels[i],
            temperature.c_str());

        const String delta =
            cell.voltageValid && haveMinimumVoltage
                ? String(
                      static_cast<int>(
                          (cell.voltageV -
                           minimumVoltage) *
                              1000.0F +
                          0.5F)) +
                      " mV"
                : String("--");
        lv_label_set_text(
            cellDeltaLabels[i],
            delta.c_str());

        String state = "--";
        lv_color_t stateColour = colourMuted();
        if (cell.voltageValid && haveMinimumVoltage) {
            const int spreadMv =
                static_cast<int>(
                    (maximumVoltage - minimumVoltage) *
                        1000.0F +
                    0.5F);
            if (spreadMv <= 1) {
                state = "Balanced";
                stateColour = lv_color_hex(0x22C55E);
            } else if (i == minimumVoltageCell) {
                state = "Lowest";
                stateColour = lv_color_hex(0xF59E0B);
            } else if (i == maximumVoltageCell) {
                state = "Highest";
                stateColour = lv_color_hex(0xF59E0B);
            } else {
                state = "Normal";
                stateColour = lv_color_hex(0x22C55E);
            }
        }
        lv_label_set_text(
            cellStateLabels[i],
            state.c_str());
        lv_obj_set_style_text_color(
            cellStateLabels[i],
            stateColour,
            0);
    }
}

static void sliderChanged(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);

    lv_obj_t *valueLabel =
        static_cast<lv_obj_t *>(lv_event_get_user_data(event));

    lv_label_set_text_fmt(
        valueLabel,
        "%d%%",
        static_cast<int>(lv_slider_get_value(slider)));
}

static lv_color_t controlToneColour(ControlUiTone tone)
{
    switch (tone) {
        case ControlUiTone::Busy:
            return lv_color_hex(0xF59E0B);
        case ControlUiTone::Success:
            return lv_color_hex(0x22C55E);
        case ControlUiTone::Failure:
            return lv_color_hex(0xEF4444);
        case ControlUiTone::Neutral:
            return colourMuted();
    }

    return colourMuted();
}

static const char *controlName(givenergy::ControlId id)
{
    switch (id) {
        case givenergy::ControlId::EnableCharge:
            return "Scheduled charging";
        case givenergy::ControlId::EnableDischarge:
            return "Scheduled discharging";
        case givenergy::ControlId::EnableChargeTarget:
            return "Charge target";
        case givenergy::ControlId::ChargeTargetSoc:
            return "Charge target SOC";
        case givenergy::ControlId::BatterySocReserve:
            return "Minimum SOC reserve";
        case givenergy::ControlId::BatteryPowerReserve:
            return "Battery power reserve";
        case givenergy::ControlId::BatteryChargeLimit:
            return "DC charge limit";
        case givenergy::ControlId::BatteryDischargeLimit:
            return "DC discharge limit";
        case givenergy::ControlId::BatteryChargeLimitAc:
            return "AC charge limit";
        case givenergy::ControlId::BatteryDischargeLimitAc:
            return "AC discharge limit";
        case givenergy::ControlId::EcoMode:
            return "Eco mode";
        case givenergy::ControlId::ActivePowerRate:
            return "Active power rate";
        case givenergy::ControlId::BatteryPauseMode:
            return "Battery pause mode";
        case givenergy::ControlId::ForceCharge:
            return "Force charge";
        case givenergy::ControlId::ForceDischarge:
            return "Force discharge";
        case givenergy::ControlId::AcCharge:
            return "AC charging";
    }

    return "Battery control";
}

static uint16_t currentHoldingValue(
    uint16_t address,
    uint16_t minimum,
    uint16_t maximum,
    uint16_t fallback)
{
    uint16_t value = 0;
    if (selectedInverterClient.registers().get(
            givenergy::RegisterBank::Holding,
            address,
            value) &&
            value >= minimum &&
            value <= maximum) {
        return value;
    }

    return fallback;
}

static bool currentHoldingEnabled(
    uint16_t address,
    bool fallback = false)
{
    uint16_t value = 0;
    if (selectedInverterClient.registers().get(
            givenergy::RegisterBank::Holding,
            address,
            value)) {
        return value != 0;
    }

    return fallback;
}

static bool timedControlBusy()
{
    return timedControlRequestPending ||
        timedControlPhase != TimedControlPhase::Idle;
}

static void timedSliderChanged(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    lv_obj_t *valueLabel =
        static_cast<lv_obj_t *>(
            lv_event_get_user_data(event));

    if (valueLabel == nullptr) {
        return;
    }

    const int value =
        static_cast<int>(lv_slider_get_value(slider));
    if (slider == timedDurationSlider) {
        lv_label_set_text_fmt(valueLabel, "%d min", value);
    } else {
        lv_label_set_text_fmt(valueLabel, "%d%%", value);
    }
}

static void timedControlButtonEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const TimedControlMode mode =
        static_cast<TimedControlMode>(
            reinterpret_cast<uintptr_t>(
                lv_event_get_user_data(event)));

    if (mode == TimedControlMode::Stop) {
        timedControlStopRequested = true;
        timedControlStatusText =
            timedControlBusy()
                ? "Stop requested; restoring saved settings"
                : "Emergency stop queued";
        timedControlStatusTone = ControlUiTone::Busy;
    } else if (timedControlBusy() ||
            controlRequestPending ||
            controlWorkflowActive) {
        timedControlStatusText =
            "Wait for the current battery control";
        timedControlStatusTone = ControlUiTone::Busy;
    } else if (timedDurationSlider == nullptr ||
            timedTargetSlider == nullptr ||
            timedPowerSlider == nullptr) {
        return;
    } else {
        requestedTimedControlMode =
            static_cast<uint8_t>(mode);
        requestedTimedDurationMinutes =
            static_cast<uint16_t>(
                lv_slider_get_value(
                    timedDurationSlider));
        requestedTimedTargetSoc =
            static_cast<uint16_t>(
                lv_slider_get_value(
                    timedTargetSlider));
        requestedTimedPowerPercent =
            static_cast<uint16_t>(
                lv_slider_get_value(
                    timedPowerSlider));
        timedControlRequestPending = true;
        timedControlStatusText =
            mode == TimedControlMode::Charge
                ? "Charge Now queued"
                : "Discharge Now queued";
        timedControlStatusTone = ControlUiTone::Busy;
    }

    if (timedStatusLabel != nullptr) {
        lv_label_set_text(
            timedStatusLabel,
            timedControlStatusText.c_str());
        lv_obj_set_style_text_color(
            timedStatusLabel,
            controlToneColour(timedControlStatusTone),
            0);
    }
}

static lv_obj_t *createTimedSlider(
    lv_obj_t *parent,
    const char *name,
    int y,
    int minimum,
    int maximum,
    int initialValue,
    bool minutes)
{
    lv_obj_t *nameLabel =
        createText(parent, name, 12, y, colourText());
    lv_obj_set_width(nameLabel, 145);

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, 165, y + 5);
    lv_obj_set_size(slider, 275, 14);
    lv_slider_set_range(slider, minimum, maximum);
    lv_slider_set_value(
        slider,
        initialValue,
        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        slider,
        colourAccent(),
        LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        slider,
        colourAccent(),
        LV_PART_KNOB);

    lv_obj_t *value =
        createText(parent, "", 458, y, colourText());
    lv_obj_set_width(value, 120);
    lv_obj_set_style_text_align(
        value,
        LV_TEXT_ALIGN_RIGHT,
        0);
    if (minutes) {
        lv_label_set_text_fmt(value, "%d min", initialValue);
    } else {
        lv_label_set_text_fmt(value, "%d%%", initialValue);
    }
    lv_obj_add_event_cb(
        slider,
        timedSliderChanged,
        LV_EVENT_VALUE_CHANGED,
        value);
    return slider;
}

static void createTimedControlButton(
    lv_obj_t *parent,
    const char *text,
    int x,
    int width,
    lv_color_t colour,
    TimedControlMode mode)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, 158);
    lv_obj_set_size(button, width, 42);
    lv_obj_set_style_bg_color(button, colour, 0);
    lv_obj_add_event_cb(
        button,
        timedControlButtonEvent,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(
            static_cast<uintptr_t>(mode)));

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static int createTimedControlsPanel(int y)
{
    lv_obj_t *panel = lv_obj_create(contentArea);
    lv_obj_set_pos(panel, 15, y);
    lv_obj_set_size(panel, 610, 258);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, colourPanel(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    createText(
        panel,
        "Timed direct battery control",
        12,
        10,
        colourText());
    createText(
        panel,
        "Single/three-phase only; verified writes and automatic restoration",
        290,
        10,
        colourMuted());

    timedDurationSlider = createTimedSlider(
        panel, "Duration", 42, 1, 240, 30, true);
    timedTargetSlider = createTimedSlider(
        panel, "Target / stop SOC", 80, 4, 100, 80, false);
    timedPowerSlider = createTimedSlider(
        panel, "Power limit", 118, 1, 100, 100, false);

    createTimedControlButton(
        panel,
        "Charge Now",
        12,
        170,
        lv_color_hex(0x16A34A),
        TimedControlMode::Charge);
    createTimedControlButton(
        panel,
        "Discharge Now",
        194,
        190,
        lv_color_hex(0xEA580C),
        TimedControlMode::Discharge);
    createTimedControlButton(
        panel,
        "Stop",
        396,
        100,
        lv_color_hex(0xDC2626),
        TimedControlMode::Stop);

    timedStatusLabel = createText(
        panel,
        timedControlStatusText.c_str(),
        12,
        214,
        controlToneColour(timedControlStatusTone));
    lv_obj_set_width(timedStatusLabel, 580);
    lv_label_set_long_mode(
        timedStatusLabel,
        LV_LABEL_LONG_WRAP);

    return y + 270;
}

static size_t addControlBinding(
    givenergy::ControlId id,
    lv_obj_t *input,
    bool isSwitch)
{
    if (controlBindingCount >= MAX_CONTROL_BINDINGS) {
        return MAX_CONTROL_BINDINGS;
    }

    const size_t index = controlBindingCount++;
    controlBindings[index].id = id;
    controlBindings[index].input = input;
    controlBindings[index].isSwitch = isSwitch;
    return index;
}

static lv_obj_t *createControlRow(int y)
{
    lv_obj_t *row = lv_obj_create(contentArea);
    lv_obj_set_pos(row, 15, y);
    lv_obj_set_size(row, 610, 52);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(row, colourPanel(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    return row;
}

static void createControlSetButton(
    lv_obj_t *row,
    size_t bindingIndex)
{
    lv_obj_t *button = lv_btn_create(row);
    lv_obj_set_pos(button, 520, 8);
    lv_obj_set_size(button, 76, 36);
    lv_obj_add_event_cb(
        button,
        controlSetEvent,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(
            static_cast<uintptr_t>(bindingIndex)));

    if (bindingIndex >= MAX_CONTROL_BINDINGS ||
            controlWorkflowActive ||
            controlRequestPending ||
            timedControlBusy() ||
            timedControlRecoveryPending) {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "Set");
    lv_obj_center(label);
}

static void createControlSlider(
    givenergy::ControlId id,
    const char *name,
    int y,
    int minimum,
    int maximum,
    int initialValue)
{
    lv_obj_t *row = createControlRow(y);
    lv_obj_t *nameLabel =
        createText(row, name, 12, 18, colourText());
    lv_obj_set_width(nameLabel, 165);
    lv_label_set_long_mode(
        nameLabel,
        LV_LABEL_LONG_DOT);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_pos(slider, 185, 19);
    lv_obj_set_size(slider, 245, 14);
    lv_slider_set_range(slider, minimum, maximum);
    lv_slider_set_value(
        slider,
        initialValue,
        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        slider,
        colourAccent(),
        LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        slider,
        colourAccent(),
        LV_PART_KNOB);

    lv_obj_t *value =
        createText(row, "", 442, 18, colourText());
    lv_obj_set_width(value, 64);
    lv_obj_set_style_text_align(
        value,
        LV_TEXT_ALIGN_RIGHT,
        0);
    lv_label_set_text_fmt(value, "%d%%", initialValue);
    lv_obj_add_event_cb(
        slider,
        sliderChanged,
        LV_EVENT_VALUE_CHANGED,
        value);

    const size_t bindingIndex =
        addControlBinding(id, slider, false);
    createControlSetButton(row, bindingIndex);
}

static void createControlSwitch(
    givenergy::ControlId id,
    const char *name,
    int y,
    bool enabled)
{
    lv_obj_t *row = createControlRow(y);
    lv_obj_t *nameLabel =
        createText(row, name, 12, 18, colourText());
    lv_obj_set_width(nameLabel, 330);
    lv_label_set_long_mode(
        nameLabel,
        LV_LABEL_LONG_DOT);

    lv_obj_t *toggle = lv_switch_create(row);
    lv_obj_set_pos(toggle, 405, 10);
    lv_obj_set_size(toggle, 64, 32);
    if (enabled) {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }

    const size_t bindingIndex =
        addControlBinding(id, toggle, true);
    createControlSetButton(row, bindingIndex);
}

static void createControlsPage()
{
    controlBindingCount = 0;

    createText(
        contentArea,
        "Verified battery and inverter controls",
        18,
        12,
        colourText());

    controlStatusLabel = createText(
        contentArea,
        controlStatusText.c_str(),
        18,
        38,
        controlToneColour(controlStatusTone));
    lv_obj_set_width(controlStatusLabel, 600);
    lv_label_set_long_mode(
        controlStatusLabel,
        LV_LABEL_LONG_DOT);

    if (!selectedInverterConfigured) {
        createText(
            contentArea,
            "Open Inverters and select a device first.",
            18,
            78,
            colourMuted());
        return;
    }

    if (!selectedInverterProfileKnown) {
        createText(
            contentArea,
            "Controls disabled: inverter profile is unknown.",
            18,
            78,
            lv_color_hex(0xEF4444));
        return;
    }

    if (selectedInverterProfile ==
            givenergy::DeviceProfile::Ems) {
        createText(
            contentArea,
            "EMS scalar controls are not enabled yet.",
            18,
            78,
            colourMuted());
        return;
    }

    lv_obj_add_flag(contentArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(contentArea, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(
        contentArea,
        LV_SCROLLBAR_MODE_AUTO);

    int y = 72;

    if (selectedInverterProfile ==
            givenergy::DeviceProfile::ThreePhase) {
        y = createTimedControlsPanel(y);

        createControlSlider(
            givenergy::ControlId::ChargeTargetSoc,
            "Charge target SOC",
            y,
            4,
            100,
            currentHoldingValue(1111, 4, 100, 80));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatterySocReserve,
            "Minimum SOC reserve",
            y,
            4,
            100,
            currentHoldingValue(1109, 4, 100, 15));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryPowerReserve,
            "Battery power reserve",
            y,
            4,
            100,
            currentHoldingValue(1078, 4, 100, 4));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryChargeLimitAc,
            "AC charge limit",
            y,
            1,
            100,
            currentHoldingValue(1110, 1, 100, 100));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryDischargeLimitAc,
            "AC discharge limit",
            y,
            1,
            100,
            currentHoldingValue(1108, 1, 100, 100));
        y += 58;
        createControlSwitch(
            givenergy::ControlId::AcCharge,
            "AC charging",
            y,
            currentHoldingEnabled(1112));
    } else {
if(selectedInverterProfile == givenergy::DeviceProfile::SinglePhase){
    y = createTimedControlsPanel(y);
}   else {



        lv_obj_t *notice = createText(
            contentArea,
            "Timed Charge Now / Discharge Now is disabled for this profile "
            "because its safe force-mode sequence has not been verified.",
            18,
            y,
            lv_color_hex(0xF59E0B));
        lv_obj_set_width(notice, 590);
        lv_label_set_long_mode(
            notice,
            LV_LABEL_LONG_WRAP);
        y += 58;
        }
        createControlSlider(
            givenergy::ControlId::ChargeTargetSoc,
            "Charge target SOC",
            y,
            4,
            100,
            currentHoldingValue(116, 4, 100, 80));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatterySocReserve,
            "Minimum SOC reserve",
            y,
            4,
            100,
            currentHoldingValue(110, 4, 100, 15));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryPowerReserve,
            "Battery power reserve",
            y,
            4,
            100,
            currentHoldingValue(114, 4, 100, 4));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryChargeLimit,
            "DC charge limit",
            y,
            0,
            50,
            currentHoldingValue(111, 0, 50, 50));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryDischargeLimit,
            "DC discharge limit",
            y,
            0,
            50,
            currentHoldingValue(112, 0, 50, 50));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryChargeLimitAc,
            "AC charge limit",
            y,
            1,
            100,
            currentHoldingValue(313, 1, 100, 100));
        y += 58;
        createControlSlider(
            givenergy::ControlId::BatteryDischargeLimitAc,
            "AC discharge limit",
            y,
            1,
            100,
            currentHoldingValue(314, 1, 100, 100));
        y += 58;
        createControlSlider(
            givenergy::ControlId::ActivePowerRate,
            "Active power rate",
            y,
            0,
            100,
            currentHoldingValue(50, 0, 100, 100));
        y += 58;
        createControlSwitch(
            givenergy::ControlId::EnableCharge,
            "Scheduled charging",
            y,
            currentHoldingEnabled(96));
        y += 58;
        createControlSwitch(
            givenergy::ControlId::EnableDischarge,
            "Scheduled discharging",
            y,
            currentHoldingEnabled(59));
        y += 58;
        createControlSwitch(
            givenergy::ControlId::EcoMode,
            "Eco mode",
            y,
            currentHoldingEnabled(27));
    }
}

static void createPlaceholderPage(const char *message)
{
    lv_obj_t *label = lv_label_create(contentArea);

    lv_label_set_text(label, message);
    lv_obj_set_style_text_color(label, colourMuted(), 0);
    lv_obj_center(label);
}

static void createWifiControls(lv_obj_t *parent);
static void updateWifiDropdown();
static void updateWifiStatus(lv_timer_t *timer);
static void updateWifiScan();
static void updateInverterScanner();
static void updateInverterScanAddress();
static void refreshInvertersPage();
static void processInverterSelection();
static void updateSelectedInverterClient();
static void showPage(Page page);

static void navigationClicked(lv_event_t *event)
{
    uintptr_t pageNumber =
        reinterpret_cast<uintptr_t>(
            lv_event_get_user_data(event));

    showPage(static_cast<Page>(pageNumber));
}

static void showPage(Page page)
{
    // Wi-Fi page objects are deleted by lv_obj_clean(). Clear their
    // pointers before removing the old page.
    if (currentPage == Page::Wifi) {
        wifiDropdown = nullptr;
        wifiPasswordField = nullptr;
        wifiKeyboard = nullptr;
        wifiStatusLabel = nullptr;
        wifiRefreshLabel = nullptr;
        wifiRefreshButton = nullptr;
    }

    if (currentPage == Page::Inverters) {
        inverterScanAddressLabel = nullptr;
        inverterIpField = nullptr;
        inverterIpKeyboard = nullptr;
        inverterIpStatusLabel = nullptr;
        inverterIpAddButton = nullptr;
    }

    if (currentPage == Page::Controls) {
        controlStatusLabel = nullptr;
        controlBindingCount = 0;
        timedDurationSlider = nullptr;
        timedTargetSlider = nullptr;
        timedPowerSlider = nullptr;
        timedStatusLabel = nullptr;
    }

    if (currentPage == Page::Battery) {
        batteryIdentityLabel = nullptr;
        batteryConnectionLabel = nullptr;
        batteryUpdatedLabel = nullptr;

        for (size_t i = 0; i < BATTERY_METRIC_COUNT; ++i) {
            batteryMetricLabels[i] = nullptr;
        }
    }

    if (currentPage == Page::Cells) {
        cellsBatteryDropdown = nullptr;
        cellsStatusLabel = nullptr;
        cellsSummaryLabel = nullptr;
        cellsDetailsLabel = nullptr;

        for (size_t i = 0;
                i < givenergy::kMaximumCellsPerBattery;
                ++i) {
            cellRows[i] = nullptr;
            cellVoltageLabels[i] = nullptr;
            cellTemperatureLabels[i] = nullptr;
            cellDeltaLabels[i] = nullptr;
            cellStateLabels[i] = nullptr;
        }
    }

    currentPage = page;

    const size_t selected = static_cast<size_t>(page);

    for (size_t i = 0; i < PAGE_COUNT; ++i) {
        lv_obj_clear_state(navButtons[i], LV_STATE_CHECKED);
    }

    lv_obj_add_state(
        navButtons[selected],
        LV_STATE_CHECKED);

    lv_label_set_text(pageTitle, PAGE_NAMES[selected]);

    // Remove the widgets belonging to the old page.
    lv_obj_clean(contentArea);
    lv_obj_scroll_to(
        contentArea,
        0,
        0,
        LV_ANIM_OFF);
    lv_obj_clear_flag(contentArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(contentArea, LV_DIR_NONE);
    lv_obj_set_style_pad_bottom(contentArea, 0, 0);

    switch (page) {
        case Page::Home:
            createHomePage();
            break;

        case Page::Inverters:
            createInvertersPage();
            break;

        case Page::Controls:
            createControlsPage();
            break;

        case Page::Battery:
            createBatteryPage();
            break;

        case Page::Cells:
            createCellsPage();
            break;

        case Page::Faults:
            createPlaceholderPage("No active faults");
            break;

        case Page::Wifi:
            createWifiControls(contentArea);
            break;

        case Page::Settings:
            createPlaceholderPage("Application settings");
            break;

        default:
            break;
    }
}

static void createNavigationButton(
    Page page,
    int y)
{
    const size_t index = static_cast<size_t>(page);

    lv_obj_t *button = lv_btn_create(lv_scr_act());
    navButtons[index] = button;

    lv_obj_set_pos(button, 10, y);
    lv_obj_set_size(button, 130, 42);

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_TRANSP,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(
        button,
        colourAccent(),
        LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);

    lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, PAGE_NAMES[index]);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_add_event_cb(
        button,
        navigationClicked,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(
            static_cast<uintptr_t>(page)));
}

static void createUserInterface()
{
    lv_obj_t *screen = lv_scr_act();

    configureContainer(screen, colourBackground());

    // Sidebar background
    lv_obj_t *sidebar = lv_obj_create(screen);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, 150, 480);
    configureContainer(sidebar, colourSidebar());

    createText(screen, "GivInfo", 24, 18, colourText());

    createNavigationButton(Page::Home,       55);
    createNavigationButton(Page::Inverters, 101);
    createNavigationButton(Page::Battery,   147);
    createNavigationButton(Page::Cells,     193);
    createNavigationButton(Page::Controls,  239);
    createNavigationButton(Page::Faults,    285);
    createNavigationButton(Page::Wifi,      331);
    createNavigationButton(Page::Settings,  377);

    // Permanent top status bar
    lv_obj_t *topBar = lv_obj_create(screen);
    lv_obj_set_pos(topBar, 150, 0);
    lv_obj_set_size(topBar, 650, 52);
    configureContainer(topBar, colourSidebar());

    pageTitle = createText(
        topBar,
        "Home",
        18,
        17,
        colourText());

    topWifiStatusLabel = createText(
        topBar,
        "Wi-Fi: Disconnected",
        440,
        17,
        colourMuted());

    lv_obj_set_width(topWifiStatusLabel, 200);
    lv_label_set_long_mode(
        topWifiStatusLabel,
        LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Area replaced when navigation buttons are pressed
    contentArea = lv_obj_create(screen);
    lv_obj_set_pos(contentArea, 150, 52);
    lv_obj_set_size(contentArea, 650, 428);
    configureContainer(contentArea, colourBackground());


    lv_timer_create(
        updateWifiStatus,
        250,
        nullptr);

    lv_timer_create(
        updateBatteryPage,
        500,
        nullptr);

    lv_timer_create(
        updateCellsPage,
        500,
        nullptr);

    showPage(Page::Home);
}

void setup()
{

    Serial.begin(115200);
    delay(500);
    loadTimedControlRecovery();

    Serial.println("Creating board");

    board = new Board();

    if ((board == nullptr) || !board->init()) {
        Serial.println("ERROR: board init failed");
        while (true) {
            delay(1000);
        }
    }

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
// hashtags mean pre processing so that it'll actually run before the code
    lcd->configFrameBufferNumber(
        LVGL_PORT_DISP_BUFFER_NUM);

#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcdBus = lcd->getBus();

    if (lcdBus->getBasicAttributes().type ==
            ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcdBus)
            ->configRGB_BounceBufferSize(
                lcd->getFrameWidth() * 10);
    }
#endif
#endif

    Serial.println("Starting board");

    if (!board->begin()) {
        Serial.println("ERROR: board begin failed");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("Starting LVGL");

    if (!lvgl_port_init(
        
            board->getLCD(),
            board->getTouch())) {
        Serial.println("ERROR: LVGL initialization failed");
        while (true) {
            delay(1000);
        }
    }

    // LVGL APIs must be protected because LVGL runs in another task.
    lvgl_port_lock(-1);
    createUserInterface();
    lvgl_port_unlock();

    Serial.println("UI ready");

    connectivity.scan();
}

static void updateWifiStatus(lv_timer_t *timer)
{
    (void)timer;

    const Connectivity::WifiState state =
        connectivity.getState();

    String topText;
    String pageText;
    lv_color_t statusColour = colourMuted();

    switch (state) {
        case Connectivity::WifiState::Disconnected:
            topText = "Wi-Fi: Disconnected";
            pageText =
                "Not connected\n"
                "Select a network";
            break;

        case Connectivity::WifiState::Connecting: {
            topText = "Wi-Fi: Connecting...";
            statusColour = lv_color_hex(0xF59E0B);

            String dots;
            const int dotCount =
                static_cast<int>((millis() / 500) % 4);

            for (int i = 0; i < dotCount; ++i) {
                dots += ".";
            }

            pageText =
                "Connecting to\n" +
                connectivity.getSelectedSSID() +
                dots;
            break;
        }

        case Connectivity::WifiState::Connected:
            // The permanent top-right label shows the connected SSID.
            topText =
                "Wi-Fi: " +
                connectivity.getConnectedSSID();

            pageText =
                "Connected\n"
                "Network: " +
                connectivity.getConnectedSSID() +
                "\nIP: " +
                connectivity.getLocalIP().toString();

            statusColour = lv_color_hex(0x22C55E);
            break;

        case Connectivity::WifiState::Failed:
            topText = "Wi-Fi: Failed";
            pageText =
                "Connection failed\n"
                "Check the password";
            statusColour = lv_color_hex(0xEF4444);
            break;
    }

    if (topWifiStatusLabel != nullptr) {
        lv_label_set_text(
            topWifiStatusLabel,
            topText.c_str());

        lv_obj_set_style_text_color(
            topWifiStatusLabel,
            statusColour,
            0);
    }

    // wifiStatusLabel only exists while the Wi-Fi page is active.
    if (currentPage == Page::Wifi &&
            wifiStatusLabel != nullptr) {
        lv_label_set_text(
            wifiStatusLabel,
            pageText.c_str());

        lv_obj_set_style_text_color(
            wifiStatusLabel,
            statusColour,
            0);
    }
}

static void refreshInvertersPage()
{
    // Arduino loop() and the LVGL task are separate tasks. Lock LVGL before
    // rebuilding the Inverters page from loop().
    lvgl_port_lock(-1);

    if (currentPage == Page::Inverters) {
        showPage(Page::Inverters);
    }

    lvgl_port_unlock();
}

static void hideInverterIpKeyboard()
{
    if (inverterIpKeyboard != nullptr) {
        lv_obj_add_flag(
            inverterIpKeyboard,
            LV_OBJ_FLAG_HIDDEN);
    }

    if (inverterIpField != nullptr) {
        lv_obj_clear_state(
            inverterIpField,
            LV_STATE_FOCUSED);
    }
}

static void queueInverterIpProbe()
{
    if (inverterIpField == nullptr ||
            inverterIpStatusLabel == nullptr) {
        return;
    }

    const char *enteredText =
        lv_textarea_get_text(inverterIpField);
    IPAddress address;

    if (connectivity.getState() !=
            Connectivity::WifiState::Connected) {
        inverterIpStatus =
            "Connect to Wi-Fi first";
        lv_label_set_text(
            inverterIpStatusLabel,
            inverterIpStatus.c_str());
        hideInverterIpKeyboard();
        return;
    }

    if (timedControlBusy()) {
        inverterIpStatus =
            "Stop timed battery control before probing another inverter";
        lv_label_set_text(
            inverterIpStatusLabel,
            inverterIpStatus.c_str());
        hideInverterIpKeyboard();
        return;
    }

    if (inverterIpProbeRequested) {
        hideInverterIpKeyboard();
        return;
    }

    if (inverterScanner.state() ==
            givenergy::GivEnergyScanner::State::Scanning) {
        inverterIpStatus =
            "Wait for the current scan";
        lv_label_set_text(
            inverterIpStatusLabel,
            inverterIpStatus.c_str());
        hideInverterIpKeyboard();
        return;
    }

    if (enteredText == nullptr ||
            !address.fromString(enteredText) ||
            address[0] == 0 ||
            address[0] >= 224 ||
            address[3] == 0 ||
            address[3] == 255) {
        inverterIpStatus =
            "Enter a valid device IPv4 address";
        lv_label_set_text(
            inverterIpStatusLabel,
            inverterIpStatus.c_str());
        lv_obj_set_style_text_color(
            inverterIpStatusLabel,
            lv_color_hex(0xEF4444),
            0);
        return;
    }

    inverterIpText = enteredText;
    requestedInverterIp =
        (static_cast<uint32_t>(address[0]) << 24U) |
        (static_cast<uint32_t>(address[1]) << 16U) |
        (static_cast<uint32_t>(address[2]) << 8U) |
        static_cast<uint32_t>(address[3]);
    inverterIpProbeRequested = true;
    inverterIpStatus =
        "Waiting to test " + address.toString();

    lv_label_set_text(
        inverterIpStatusLabel,
        inverterIpStatus.c_str());
    lv_obj_set_style_text_color(
        inverterIpStatusLabel,
        lv_color_hex(0xF59E0B),
        0);
    hideInverterIpKeyboard();
}

static void inverterIpFieldEvent(lv_event_t *event)
{
    const lv_event_code_t code =
        lv_event_get_code(event);

    if (code == LV_EVENT_READY) {
        queueInverterIpProbe();
    } else if (code == LV_EVENT_CANCEL) {
        hideInverterIpKeyboard();
    } else if (code == LV_EVENT_FOCUSED &&
            inverterIpKeyboard != nullptr) {
        lv_keyboard_set_textarea(
            inverterIpKeyboard,
            inverterIpField);
        lv_obj_clear_flag(
            inverterIpKeyboard,
            LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(inverterIpKeyboard);
    } else if (code == LV_EVENT_DEFOCUSED) {
        hideInverterIpKeyboard();
    }
}

static void inverterIpKeyboardEvent(lv_event_t *event)
{
    const lv_event_code_t code =
        lv_event_get_code(event);

    if (code == LV_EVENT_READY) {
        queueInverterIpProbe();
    } else if (code == LV_EVENT_CANCEL) {
        hideInverterIpKeyboard();
    }
}

static void inverterIpAddEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) ==
            LV_EVENT_CLICKED) {
        queueInverterIpProbe();
    }
}

static void controlSetEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) !=
            LV_EVENT_CLICKED) {
        return;
    }

    const size_t bindingIndex =
        static_cast<size_t>(
            reinterpret_cast<uintptr_t>(
                lv_event_get_user_data(event)));

    if (bindingIndex >= controlBindingCount ||
            bindingIndex >= MAX_CONTROL_BINDINGS ||
            controlBindings[bindingIndex].input == nullptr) {
        return;
    }

    if (controlRequestPending ||
            controlWorkflowActive ||
            timedControlBusy() ||
            timedControlRecoveryPending) {
        controlStatusText =
            timedControlRecoveryPending
                ? "Complete the pending safety recovery first"
                : "Wait for the current battery control";
        controlStatusTone = ControlUiTone::Busy;
    } else {
        const ControlUiBinding &binding =
            controlBindings[bindingIndex];
        const uint16_t value =
            binding.isSwitch
                ? static_cast<uint16_t>(
                      lv_obj_has_state(
                          binding.input,
                          LV_STATE_CHECKED)
                          ? 1
                          : 0)
                : static_cast<uint16_t>(
                      lv_slider_get_value(
                          binding.input));

        requestedControlId =
            static_cast<uint8_t>(binding.id);
        requestedControlValue = value;
        controlRequestPending = true;
        controlStatusText =
            String("Queued: ") +
            controlName(binding.id);
        controlStatusTone = ControlUiTone::Busy;
    }

    if (controlStatusLabel != nullptr) {
        lv_label_set_text(
            controlStatusLabel,
            controlStatusText.c_str());
        lv_obj_set_style_text_color(
            controlStatusLabel,
            controlToneColour(controlStatusTone),
            0);
    }
}

static void inverterRefreshEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (timedControlBusy()) {
        timedControlStatusText =
            "Stop timed battery control before scanning";
        timedControlStatusTone = ControlUiTone::Failure;
        return;
    }

    if (connectivity.getState() !=
                Connectivity::WifiState::Connected ||
            inverterScanner.state() ==
                givenergy::GivEnergyScanner::State::Scanning) {
        return;
    }

    // The callback runs in LVGL's task. Let loop() start the network
    // operation so UI and networking work are not mixed across tasks.
    inverterRescanRequested = true;
}

static void inverterSelectEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const size_t index =
        static_cast<size_t>(
            reinterpret_cast<uintptr_t>(
                lv_event_get_user_data(event)));

    if (index >= inverterCount) {
        return;
    }

    // The LVGL callback records the request. Networking is started from
    // Arduino loop(), not from LVGL's task.
    requestedInverterSelection = static_cast<int>(index);
}

static void processInverterSelection()
{
    const int requested = requestedInverterSelection;

    if (requested < 0) {
        return;
    }

    requestedInverterSelection = -1;

    if (static_cast<size_t>(requested) >= inverterCount) {
        return;
    }

    if (timedControlBusy()) {
        timedControlStatusText =
            "Stop timed battery control before changing inverter";
        timedControlStatusTone = ControlUiTone::Failure;
        Serial.println(timedControlStatusText);
        return;
    }

    const bool interruptedControl =
        controlRequestPending ||
        controlWorkflowActive;
    controlRequestPending = false;
    controlWorkflowActive = false;

    selectedInverterClient.stop();
    selectedInverterClientStarted = false;

    selectedInverterIP = inverterIPs[requested];
    selectedInverterSerial = inverterSerials[requested];
    selectedInverterSlaveAddress =
        inverterSlaveAddresses[requested] == 0
            ? 0x11
            : inverterSlaveAddresses[requested];
    selectedInverterProfileKnown =
        inverterProfileKnown[requested];
    selectedInverterModelCode =
        inverterModelCodes[requested];
    selectedInverterModel =
        inverterModels[requested];
    selectedInverterProfile =
        inverterProfiles[requested];
    selectedInverterConfigured = true;

    if (interruptedControl) {
        controlStatusText =
            "Battery control failed: the selected inverter changed";
        controlStatusTone = ControlUiTone::Failure;
    } else if (selectedInverterProfileKnown) {
        controlStatusText =
            String("Ready: ") +
            selectedInverterSerial +
            " (" +
            givenergy::deviceProfileName(
                selectedInverterProfile) +
            ")";
        controlStatusTone = ControlUiTone::Neutral;
    } else {
        controlStatusText =
            "Controls disabled: inverter profile is unknown";
        controlStatusTone = ControlUiTone::Failure;
    }

    Serial.print("Selected inverter ");
    Serial.print(selectedInverterIP);
    Serial.print(" serial ");
    Serial.print(
        selectedInverterSerial.isEmpty()
            ? "Unknown"
            : selectedInverterSerial);
    Serial.print(" slave 0x");
    Serial.print(selectedInverterSlaveAddress, HEX);
    Serial.print(" model ");
    Serial.print(
        givenergy::deviceModelName(selectedInverterModel));
    Serial.print(" profile ");
    Serial.println(
        selectedInverterProfileKnown
            ? givenergy::deviceProfileName(
                  selectedInverterProfile)
            : "unknown");

    lvgl_port_lock(-1);
    showPage(Page::Battery);
    lvgl_port_unlock();
}

static void updateSelectedInverterClient()
{
    if (!selectedInverterConfigured) {
        return;
    }

    if (!selectedInverterProfileKnown) {
        if (selectedInverterClientStarted) {
            selectedInverterClient.stop();
            selectedInverterClientStarted = false;
        }
        return;
    }

    // The scanner and monitoring client must never own port 8899 at the same
    // time. Monitoring is restarted automatically when scanning finishes.
    if (inverterScanner.state() ==
            givenergy::GivEnergyScanner::State::Scanning) {
        if (selectedInverterClientStarted) {
            selectedInverterClient.stop();
            selectedInverterClientStarted = false;
        }
        return;
    }

    const bool wifiConnected =
        connectivity.getState() ==
        Connectivity::WifiState::Connected;

    if (!selectedInverterClientStarted) {
        if (!wifiConnected) {
            return;
        }

        selectedInverterClient.setPollIntervals(5000, 60000);
        selectedInverterClient.setResponseTimeout(5000);
        selectedInverterClient.begin(
            selectedInverterIP,
            selectedInverterProfile,
            selectedInverterSlaveAddress);
        selectedInverterClientStarted = true;
    }

    selectedInverterClient.update();
}

static void updateCellTelemetry()
{
    if (currentPage != Page::Cells ||
            !selectedInverterConfigured ||
            !selectedInverterProfileKnown ||
            !selectedInverterClientStarted ||
            controlWorkflowActive ||
            timedControlBusy() ||
            selectedInverterClient.batteryRefreshInProgress() ||
            inverterScanner.state() ==
                givenergy::GivEnergyScanner::State::Scanning) {
        return;
    }

    if (!selectedInverterClient.connected()) {
        return;
    }

    if (batteryRediscoveryRequested) {
        if (selectedInverterClient.requestBatteryRefresh(true)) {
            batteryRediscoveryRequested = false;
        }
        return;
    }

    if (selectedInverterClient.batteryCount() == 0) {
        return;
    }

    const uint32_t lastRefresh =
        selectedInverterClient.lastBatteryRefreshMs();
    if (lastRefresh == 0 ||
            millis() - lastRefresh >=
                BATTERY_REFRESH_INTERVAL_MS) {
        selectedInverterClient.requestBatteryRefresh(false);
    }
}

static void setControlStatusMessage(
    const String &message,
    ControlUiTone tone)
{
    controlStatusText = message;
    controlStatusTone = tone;

    lvgl_port_lock(-1);

    if (currentPage == Page::Controls &&
            controlStatusLabel != nullptr) {
        lv_label_set_text(
            controlStatusLabel,
            controlStatusText.c_str());
        lv_obj_set_style_text_color(
            controlStatusLabel,
            controlToneColour(controlStatusTone),
            0);
    }

    lvgl_port_unlock();
}

static void setTimedControlStatusMessage(
    const String &message,
    ControlUiTone tone)
{
    timedControlStatusText = message;
    timedControlStatusTone = tone;

    lvgl_port_lock(-1);

    if (currentPage == Page::Controls &&
            timedStatusLabel != nullptr) {
        lv_label_set_text(
            timedStatusLabel,
            timedControlStatusText.c_str());
        lv_obj_set_style_text_color(
            timedStatusLabel,
            controlToneColour(timedControlStatusTone),
            0);
    }

    lvgl_port_unlock();
}


static bool timedControlProfileSupported(givenergy::DeviceProfile profile)
{
    return profile == givenergy::DeviceProfile::SinglePhase || profile == givenergy::DeviceProfile::ThreePhase;
}

static bool validHhmm(uint16_t value)
{
    return value<=2359U && (value % 100U) < 60U;
}
//validHhmm ensures values such as 1265 are rejected because 65 is not a valid minute
static uint16_t minutesToHhmm(uint16_t minutes)
{
    minutes %= 1440U;

    const uint16_t hour = minutes/60U;
    const uint16_t minute = minutes % 60U;

    return static_cast<uint16_t>(hour * 100U + minute);
}
//minutes to Hhmm converts minutes since mdnight into the format stored byt he inverter
static void clearTimedControlSteps()
{
    timedControlStepCount = 0;
    timedControlStepIndex = 0;
    timedControlStepPending = false;
    timedControlStepRetries = 0;
    timedControlStepStartedMs = 0;
}

static bool appendTimedControlStep(
    givenergy::ControlId id,
    uint16_t value)
{
    if (timedControlStepCount >=
            MAX_TIMED_CONTROL_STEPS) {
        return false;
    }

    TimedControlStep &step = timedControlSteps[timedControlStepCount++];

    step = TimedControlStep{};
    step.kind = TimedControlStepKind::Control;
    step.id = id;
    step.value = value;

    return true;
}


static bool appendTimedSlotStep(bool discharge, uint8_t slot, uint16_t startHhmm, uint16_t endHhmm)
{
    if (timedControlStepCount >= MAX_TIMED_CONTROL_STEPS || !validHhmm(startHhmm) || !validHhmm(endHhmm))
    {
        return false;
    }

    TimedControlStep &step = timedControlSteps[timedControlStepCount++];

    step = TimedControlStep{};
    step.kind = TimedControlStepKind::TimeSlot;
    step.discharge = discharge;
    step.slot = slot;
    step.startHhmm = startHhmm;
    step.endHhmm = endHhmm;

    return true;
}

static bool readTimedControlSnapshot()
{
    const givenergy::RegisterStore &registers =
        selectedInverterClient.registers();

    TimedControlSnapshot snapshot;
    snapshot.profile = selectedInverterProfile;
    snapshot.workflowMode = timedControlMode;

    bool complete = false;

    if (snapshot.profile ==
                givenergy::DeviceProfile::SinglePhase) {
            snapshot.batteryPauseModeSupported = registers.get(
                givenergy::RegisterBank::Holding,
                318,
                snapshot.batteryPauseMode);

            if (snapshot.batteryPauseModeSupported && snapshot.batteryPauseMode > 3){
                    return false;
                }
        
            complete = registers.get(
                givenergy::RegisterBank::Holding,
                20,
                snapshot.enableChargeTarget) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                27,
                snapshot.ecoMode) &&
            
            registers.get(
                givenergy::RegisterBank::Holding,
                38,
                snapshot.inverterHour) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                39,
                snapshot.inverterMinute) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                56,
                snapshot.dischargeSlotStart) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                57,
                snapshot.dischargeSlotEnd) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                59,
                snapshot.dischargeEnabled) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                94,
                snapshot.chargeSlotStart) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                95,
                snapshot.chargeSlotEnd) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                96,
                snapshot.chargeEnabled) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                110,
                snapshot.socReserve) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                111,
                snapshot.chargeLimit) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                112,
                snapshot.dischargeLimit) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                116,
                snapshot.chargeTargetSoc);

        if (!complete ||
            snapshot.enableChargeTarget > 1 ||
            snapshot.ecoMode > 1 ||
            snapshot.inverterHour > 23 ||
            snapshot.inverterMinute > 59 ||
            !validHhmm(snapshot.dischargeSlotStart) ||
            !validHhmm(snapshot.dischargeSlotEnd) ||
            !validHhmm(snapshot.chargeSlotStart) ||
            !validHhmm(snapshot.chargeSlotEnd) ||
            snapshot.dischargeEnabled > 1 ||
            snapshot.chargeEnabled > 1 ||
            snapshot.socReserve < 4 ||
            snapshot.socReserve > 100 ||
            snapshot.chargeTargetSoc < 4 ||
            snapshot.chargeTargetSoc > 100 ||
            snapshot.chargeLimit > 50 ||
            snapshot.dischargeLimit > 50) {
            return false;
        }
    } else if (snapshot.profile ==
                   givenergy::DeviceProfile::ThreePhase) {
        complete =
            registers.get(
                givenergy::RegisterBank::Holding,
                1111,
                snapshot.chargeTargetSoc) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                1110,
                snapshot.chargeLimitAc) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                1108,
                snapshot.dischargeLimitAc) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                1109,
                snapshot.socReserve) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                1112,
                snapshot.acCharge) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                1122,
                snapshot.forceDischarge) &&
            registers.get(
                givenergy::RegisterBank::Holding,
                1123,
                snapshot.forceCharge);

        if (!complete ||
            snapshot.chargeTargetSoc < 4 ||
            snapshot.chargeTargetSoc > 100 ||
            snapshot.chargeLimitAc < 1 ||
            snapshot.chargeLimitAc > 100 ||
            snapshot.dischargeLimitAc < 1 ||
            snapshot.dischargeLimitAc > 100 ||
            snapshot.socReserve < 4 ||
            snapshot.socReserve > 100 ||
            snapshot.acCharge > 1 ||
            snapshot.forceDischarge > 1 ||
            snapshot.forceCharge > 1) {
            return false;
        }
    } else {
        return false;
    }

    snapshot.valid = true;
    timedControlSnapshot = snapshot;
    return true;
}

static void timedSettingsReadRange(uint16_t &base, uint16_t &count)
{
    if (selectedInverterProfile == givenergy::DeviceProfile::SinglePhase){
        if (timedControlSettingsReadStage == 0){
            base = 0;
            count = 60;
        } else {
            base = 60;
            count = 60;
        }
        return;
    }
    base = 1080;
    count = 45;
}

static bool queueTimedSettingsRead()
{
    uint16_t base = 0;
    uint16_t count = 0;
    timedSettingsReadRange(base, count);

    timedControlSettingsReadGeneration = selectedInverterClient.readGeneration();

    timedControlSettingsReadQueued = selectedInverterClient.requestRegisters(givenergy::RegisterBank::Holding, base, count, selectedInverterSlaveAddress);

    return timedControlSettingsReadQueued;
}

static uint32_t packIpAddress(const IPAddress &address)
{
    return
        (static_cast<uint32_t>(address[0]) << 24U) |
        (static_cast<uint32_t>(address[1]) << 16U) |
        (static_cast<uint32_t>(address[2]) << 8U) |
        static_cast<uint32_t>(address[3]);
}

static IPAddress unpackIpAddress(uint32_t packed)
{
    return IPAddress(
        static_cast<uint8_t>(packed >> 24U),
        static_cast<uint8_t>(packed >> 16U),
        static_cast<uint8_t>(packed >> 8U),
        static_cast<uint8_t>(packed));
}

static bool saveTimedControlRecovery()
{
    if (!timedControlSnapshot.valid) {
        return false;
    }

    Preferences preferences;
    if (!preferences.begin("givinfoctl", false)) {
        return false;
    }

    preferences.clear();

    bool saved =
        preferences.putUChar(
            "ver",
            TIMED_SNAPSHOT_VERSION) > 0;

    saved =
        preferences.putUInt(
            "ip",
            packIpAddress(
                timedControlInverterIP)) > 0 &&
        saved;

    saved =
        preferences.putBytes(
            "snapshot",
            &timedControlSnapshot,
            sizeof(timedControlSnapshot)) ==
            sizeof(timedControlSnapshot) &&
        saved;

    if (saved) {
        saved =
            preferences.putBool(
                "active",
                true) > 0;
    } else {
        preferences.putBool("active", false);
    }

    preferences.end();
    return saved;
}

static bool clearTimedControlRecovery()
{
    Preferences preferences;
    if (!preferences.begin("givinfoctl", false)) {
        return false;
    }
    const bool cleared = preferences.clear();
    preferences.end();
    if (cleared) {
        timedControlRecoveryPending = false;
        timedControlRecoveryActive = false;
        timedControlRecoveryAttempted = false;
        timedControlRecoverySnapshot =
            TimedControlSnapshot{};
    }
    return cleared;
}

static void loadTimedControlRecovery()
{
    Preferences preferences;
    if (!preferences.begin("givinfoctl", true)) {
        return;
    }
    if (!preferences.getBool("active", false)) {
        preferences.end();
        return;
    }
    const bool active =
        preferences.getBool("active", false);

    const uint8_t version =
        preferences.getUChar("ver", 0);

    const size_t storedSize =
        preferences.getBytesLength("snapshot");

    if (!active ||
        version != TIMED_SNAPSHOT_VERSION ||
        storedSize != sizeof(TimedControlSnapshot)) {
        preferences.end();
        return;
    }

    TimedControlSnapshot snapshot;

    const size_t loaded =
        preferences.getBytes(
            "snapshot",
            &snapshot,
            sizeof(snapshot));

    const uint32_t packedIp =
        preferences.getUInt("ip", 0);

    preferences.end();

    if (loaded != sizeof(snapshot) ||
        packedIp == 0 ||
        !snapshot.valid ||
        !timedControlProfileSupported(
            snapshot.profile)) {
        return;
    }

    timedControlRecoverySnapshot = snapshot;
    timedControlRecoveryIP =
        unpackIpAddress(packedIp);
    timedControlRecoveryPending = true;
    timedControlRecoveryAttempted = false;

    timedControlStatusText =
        "Safety recovery pending for " +
        timedControlRecoveryIP.toString();

    timedControlStatusTone =
        ControlUiTone::Failure;
}

static bool buildTimedStartSteps()
{
    clearTimedControlSteps();
//removes commands left from any previous charge or discharge operation
    const givenergy::DeviceProfile profile =
        timedControlSnapshot.profile;
//This copies the profile that was saved in the snapshot
//e.g. device profile::SinglePhase, DeviceProfile::ThreePhase
    if (profile ==
            givenergy::DeviceProfile::SinglePhase) {
        const uint16_t startMinutes =
            static_cast<uint16_t>(
                timedControlSnapshot.inverterHour * 60U +
                timedControlSnapshot.inverterMinute);
//Converts the inverters current time into minutes since midnight.
        
        if (timedControlSnapshot.batteryPauseModeSupported &&
            !appendTimedControlStep(
                givenergy::ControlId::BatteryPauseMode,
                0)) {
            return false;
}//This function does not immediately write registers. It constructs the ordered list of commands
//that will be sent and verified. Putting HR318 here ensures pause mode is disabled before charging or dischargin is enabled

        const uint16_t endMinutes =
            static_cast<uint16_t>(
                (startMinutes +
                 timedControlDurationMinutes) %
                1440U);
//There are 1440 minutes in one day, so % 1440U makes the time wrap around midnight
        const uint16_t startHhmm =
            minutesToHhmm(startMinutes);

        const uint16_t endHhmm =
            minutesToHhmm(endMinutes);
//This part converts the results ino theinverter's HHMM format

        // The GUI uses 1–100%, while HR111/112 use 0–50.
        const uint16_t dcLimit =
            static_cast<uint16_t>(
                (timedControlPowerPercent + 1U) /
                2U);
//This section runs when the user pressed Charge now
        if (timedControlMode ==
                TimedControlMode::Charge) {
            if (!appendTimedControlStep(
                    givenergy::ControlId::EnableDischarge,
                    0) ||
//disable discharging
                !appendTimedControlStep(
                    givenergy::ControlId::ChargeTargetSocOnly,
                    timedControlTargetSoc) ||
                !appendTimedControlStep(
                    givenergy::ControlId::EnableChargeTarget,
                    timedControlTargetSoc < 100 ? 1 : 0) ||
                    //less than 100. If true then it'll be 1 if not it'll be 0 
                !appendTimedSlotStep(
                    false,
                    1,
                    startHhmm,
                    endHhmm) ||
                !appendTimedControlStep(
                    givenergy::ControlId::BatteryChargeLimit,
                    dcLimit) ||
                !appendTimedControlStep(
                    givenergy::ControlId::EnableCharge,
                    1)) {
                return false;
            }

            return true;
        }

        if (timedControlMode ==
                TimedControlMode::Discharge) {
            if (!appendTimedControlStep(
                    givenergy::ControlId::EnableCharge,
                    0) ||
                !appendTimedControlStep(
                    givenergy::ControlId::BatterySocReserve,
                    timedControlTargetSoc) ||

                // 0 means maximum discharge/export.
                // Use 1 here instead if it should only match demand.
                !appendTimedControlStep(
                    givenergy::ControlId::EcoMode,
                    0) ||

                !appendTimedSlotStep(
                    true,
                    1,
                    startHhmm,
                    endHhmm) ||
                !appendTimedControlStep(
                    givenergy::ControlId::BatteryDischargeLimit,
                    dcLimit) ||
                !appendTimedControlStep(
                    givenergy::ControlId::EnableDischarge,
                    1)) {
                return false;
            }

            return true;
        }

        return false;
    }

    if (profile ==
            givenergy::DeviceProfile::ThreePhase) {
        if (timedControlMode ==
                TimedControlMode::Charge) {
            return
                appendTimedControlStep(
                    givenergy::ControlId::ForceDischarge,
                    0) &&
                appendTimedControlStep(
                    givenergy::ControlId::ChargeTargetSoc,
                    timedControlTargetSoc) &&
                appendTimedControlStep(
                    givenergy::ControlId::BatteryChargeLimitAc,
                    timedControlPowerPercent) &&
                appendTimedControlStep(
                    givenergy::ControlId::AcCharge,
                    1) &&
                appendTimedControlStep(
                    givenergy::ControlId::ForceCharge,
                    1);
        }

        if (timedControlMode ==
                TimedControlMode::Discharge) {
            return
                appendTimedControlStep(
                    givenergy::ControlId::ForceCharge,
                    0) &&
                appendTimedControlStep(
                    givenergy::ControlId::AcCharge,
                    0) &&
                appendTimedControlStep(
                    givenergy::ControlId::BatterySocReserve,
                    timedControlTargetSoc) &&
                appendTimedControlStep(
                    givenergy::ControlId::BatteryDischargeLimitAc,
                    timedControlPowerPercent) &&
                appendTimedControlStep(
                    givenergy::ControlId::ForceDischarge,
                    1);
        }
    }

    return false;
}

//Logic:
//for charging
//Disable discharge 
//-> set target, Set current charge window, -> set power, -> Enable charge last
static void beginTimedControlRestoration(
    const char *reason,
    bool startFailed)
{
    const givenergy::ControlStatus previousStatus =
        selectedInverterClient.lastControlStatus();
    timedControlWaitForPreviousCommand =
        timedControlStepPending ||
        controlWorkflowActive ||
        previousStatus == givenergy::ControlStatus::Queued ||
        previousStatus == givenergy::ControlStatus::Sent;
    timedControlStartFailed =
        timedControlStartFailed || startFailed;
    clearTimedControlSteps();

const givenergy::DeviceProfile profile =
    timedControlSnapshot.valid
        ? timedControlSnapshot.profile
        : selectedInverterProfile;

if (profile ==
        givenergy::DeviceProfile::SinglePhase) {
    // Stop both schedules before restoring anything.
    appendTimedControlStep(
        givenergy::ControlId::EnableCharge,
        0);
    appendTimedControlStep(
        givenergy::ControlId::EnableDischarge,
        0);

    if (timedControlSnapshot.valid) {
        appendTimedControlStep(
            givenergy::ControlId::ChargeTargetSocOnly,
            timedControlSnapshot.chargeTargetSoc);

        appendTimedControlStep(
            givenergy::ControlId::EnableChargeTarget,
            timedControlSnapshot.enableChargeTarget);

        appendTimedControlStep(
            givenergy::ControlId::BatterySocReserve,
            timedControlSnapshot.socReserve);

        appendTimedControlStep(
            givenergy::ControlId::BatteryChargeLimit,
            timedControlSnapshot.chargeLimit);

        appendTimedControlStep(
            givenergy::ControlId::BatteryDischargeLimit,
            timedControlSnapshot.dischargeLimit);

        appendTimedControlStep(
            givenergy::ControlId::EcoMode,
            timedControlSnapshot.ecoMode);

        appendTimedSlotStep(
            false,
            1,
            timedControlSnapshot.chargeSlotStart,
            timedControlSnapshot.chargeSlotEnd);

        appendTimedSlotStep(
            true,
            1,
            timedControlSnapshot.dischargeSlotStart,
            timedControlSnapshot.dischargeSlotEnd);

        // Restore original enable states last.
        appendTimedControlStep(
            givenergy::ControlId::EnableCharge,
            timedControlSnapshot.chargeEnabled);

        appendTimedControlStep(
            givenergy::ControlId::EnableDischarge,
            timedControlSnapshot.dischargeEnabled);
        
        if (timedControlSnapshot.batteryPauseModeSupported) {
            appendTimedControlStep(givenergy::ControlId::BatteryPauseMode,timedControlSnapshot.batteryPauseMode);
        }
    }
}
    else if (profile == givenergy::DeviceProfile::ThreePhase){
    // Stop both force modes before restoring any saved values.
    appendTimedControlStep(
        givenergy::ControlId::ForceCharge,
        0);
    appendTimedControlStep(
        givenergy::ControlId::ForceDischarge,
        0);
    appendTimedControlStep(
        givenergy::ControlId::AcCharge,
        0);

    if (timedControlSnapshot.valid) {
        appendTimedControlStep(
            givenergy::ControlId::ChargeTargetSoc,
            timedControlSnapshot.chargeTargetSoc);
        appendTimedControlStep(
            givenergy::ControlId::BatterySocReserve,
            timedControlSnapshot.socReserve);
        appendTimedControlStep(
            givenergy::ControlId::BatteryChargeLimitAc,
            timedControlSnapshot.chargeLimitAc);
        appendTimedControlStep(
            givenergy::ControlId::BatteryDischargeLimitAc,
            timedControlSnapshot.dischargeLimitAc);
        appendTimedControlStep(
            givenergy::ControlId::AcCharge,
            timedControlSnapshot.acCharge);
        appendTimedControlStep(
            givenergy::ControlId::ForceDischarge,
            timedControlSnapshot.forceDischarge);
        appendTimedControlStep(
            givenergy::ControlId::ForceCharge,
            timedControlSnapshot.forceCharge);
    }
    }
    timedControlPhase = TimedControlPhase::Restoring;

    String message =
        timedControlSnapshot.valid
            ? "Stopping and restoring saved settings"
            : "Stopping charge and discharge";
    if (reason != nullptr && reason[0] != '\0') {
        message += ": ";
        message += reason;
    }
    setTimedControlStatusMessage(
        message,
        ControlUiTone::Busy);
}

static void finishTimedControlWorkflow()
{
    String message;
    ControlUiTone tone = ControlUiTone::Success;

    if (timedControlRestoreFailed) {
        message =
            "RESTORE FAILED - check the inverter settings";
        tone = ControlUiTone::Failure;
        if (timedControlSnapshot.valid) {
            timedControlRecoverySnapshot =
                timedControlSnapshot;
            timedControlRecoveryIP =
                timedControlInverterIP;
            timedControlRecoveryPending = true;
            timedControlRecoveryActive = false;
            timedControlRecoveryAttempted = true;
        }
    } else if (timedControlStartFailed) {
        message =
            "Battery control failed; original settings restored";
        tone = ControlUiTone::Failure;
    } else if (timedControlRecoveryActive) {
        message =
            "Safety recovery complete; saved settings restored";
    } else if (timedControlMode == TimedControlMode::Stop) {
        message =
            "Battery charge and discharge stopped successfully";
    } else {
        message =
            timedControlMode == TimedControlMode::Charge
                ? "Timed charge finished; original settings restored"
                : "Timed discharge finished; original settings restored";
    }

    if (!timedControlRestoreFailed &&
            timedControlSnapshot.valid &&
            !clearTimedControlRecovery()) {
        message =
            "Settings restored, but the safety record could not be cleared";
        tone = ControlUiTone::Failure;
    }

    timedControlPhase = TimedControlPhase::Idle;
    timedControlMode = TimedControlMode::None;
    timedControlSnapshot = TimedControlSnapshot{};
    timedControlPowerCheckStartedMs = 0;
    timedControlPowerSeen = false;
    timedControlSettingsReadQueued = false;
    timedControlWaitForPreviousCommand = false;
    clearTimedControlSteps();
    setTimedControlStatusMessage(message, tone);
    selectedInverterClient.requestRefresh(true);
}

static bool timedControlConnectionReady()
{
    return
        selectedInverterConfigured &&
        selectedInverterProfileKnown &&
        timedControlProfileSupported(
            selectedInverterProfile) &&
        (!timedControlSnapshot.valid ||
         selectedInverterProfile ==
             timedControlSnapshot.profile) &&
        selectedInverterClientStarted &&
        selectedInverterClient.connected() &&
        inverterScanner.state() !=
            givenergy::GivEnergyScanner::State::Scanning &&
        selectedInverterIP ==
            timedControlInverterIP;
}

static void processTimedControlSteps()
{
    if (timedControlStepIndex >=
        timedControlStepCount) {
    if (timedControlPhase ==
            TimedControlPhase::Applying) {
        timedControlPhase =
            TimedControlPhase::Running;

        timedControlEndMs =
            millis() +
            static_cast<uint32_t>(
                timedControlDurationMinutes) *
                60000U;

        timedControlLastUiMs = 0;

        // The settings have been written, but we have not yet
        // confirmed that battery power has started moving.
        timedControlPowerCheckStartedMs = millis();
        timedControlPowerSeen = false;

        // Request fresh input registers containing battery power.
        selectedInverterClient.requestRefresh(false);

        setTimedControlStatusMessage(
            timedControlMode ==
                    TimedControlMode::Charge
                ? "Charge settings applied; checking battery power"
                : "Discharge settings applied; checking battery power",
            ControlUiTone::Busy);
    } else {
        finishTimedControlWorkflow();
    }

    return;
}

    if (timedControlWaitForPreviousCommand) {
        const givenergy::ControlStatus status =
            selectedInverterClient.lastControlStatus();
        if (controlWorkflowActive ||
                status == givenergy::ControlStatus::Queued ||
                status == givenergy::ControlStatus::Sent) {
            return;
        }
        timedControlWaitForPreviousCommand = false;
    }

    if (!timedControlConnectionReady()) {
        if (timedControlPhase == TimedControlPhase::Applying){
            beginTimedControlRestoration("connection lost while starting battery control", true);
            
        }
        //processTimedControlSteps() sends and verifies the queued commands. If connectivity disappears during Applying,
        //the current code remains in that phase and coul continue enabling he operation after reconnection
        //This change switches it into Restoring. It will wait for reconnection, then restore the saved settings instead of continuing the incomplete
        //start sequence
        const uint32_t now = millis();
        if (timedControlLastUiMs == 0 ||
                now - timedControlLastUiMs >= 1000U) {
            timedControlLastUiMs = now;
            setTimedControlStatusMessage(
                timedControlPhase ==
                        TimedControlPhase::Restoring
                    ? "Waiting to reconnect so saved settings can be restored"
                    : "Connection lost; preparing to restore saved settings",
                ControlUiTone::Failure);
        }
        return;
    }

    if (!timedControlStepPending) {
        const TimedControlStep &step =
            timedControlSteps[timedControlStepIndex];
            givenergy::ControlValidation validation;
            String stepName;

            if (step.kind == TimedControlStepKind::Control) {
                validation = selectedInverterClient.control(step.id,step.value);
                stepName = controlName(step.id);
            }
            
            else{
                validation = selectedInverterClient.setTimeSlot(step.discharge, step.slot, static_cast<uint8_t>(step.startHhmm/100U),
            static_cast<uint8_t>(step.startHhmm % 100U),
            static_cast<uint8_t>(step.endHhmm / 100U),
            static_cast<uint8_t>(step.endHhmm % 100U));
                stepName = step.discharge ? "discharge time slot" : "charge time slot";
            }


        if (validation !=
                givenergy::ControlValidation::Ok) {
            if (timedControlPhase ==
                    TimedControlPhase::Applying) {
                beginTimedControlRestoration(
                    "a start command was rejected",
                    true);
            } else if (timedControlStepRetries < 1) {
                ++timedControlStepRetries;
            } else {
                timedControlRestoreFailed = true;
                timedControlStepRetries = 0;
                ++timedControlStepIndex;
            }
            return;
        }

        timedControlStepPending = true;
        timedControlStepStartedMs = millis();
        setTimedControlStatusMessage(
            String(
                timedControlPhase ==
                        TimedControlPhase::Restoring
                    ? "Restoring: "
                    : "Applying: ") +
                stepName,
            ControlUiTone::Busy);
        return;
    }

    const givenergy::ControlStatus status =
        selectedInverterClient.lastControlStatus();
    if (status == givenergy::ControlStatus::Verified) {
        timedControlStepPending = false;
        timedControlStepRetries = 0;
        ++timedControlStepIndex;
        return;
    }

    const bool failed =
        status == givenergy::ControlStatus::Failed ||
        status == givenergy::ControlStatus::Rejected ||
        millis() - timedControlStepStartedMs >= 45000U;
    if (!failed) {
        return;
    }

    timedControlStepPending = false;
    if (timedControlPhase ==
            TimedControlPhase::Applying) {
        beginTimedControlRestoration(
            selectedInverterClient.lastError(),
            true);
        return;
    }

    if (timedControlStepRetries < 1) {
        ++timedControlStepRetries;
        setTimedControlStatusMessage(
            "Restoration command failed; retrying",
            ControlUiTone::Failure);
        return;
    }

    timedControlRestoreFailed = true;
    timedControlStepRetries = 0;
    ++timedControlStepIndex;
}

static void failTimedControlRequest(const char *reason)
{
    timedControlPhase = TimedControlPhase::Idle;
    timedControlMode = TimedControlMode::None;
    timedControlSnapshot = TimedControlSnapshot{};
    timedControlPowerCheckStartedMs = 0;
    timedControlPowerSeen = false;
    timedControlSettingsReadQueued = false;
    timedControlWaitForPreviousCommand = false;
    clearTimedControlSteps();

    String message = "Battery control failed";
    if (reason != nullptr && reason[0] != '\0') {
        message += ": ";
        message += reason;
    }
    setTimedControlStatusMessage(
        message,
        ControlUiTone::Failure);
}

static void updateTimedControlWorkflow()
{
    if (timedControlRecoveryPending &&
            !timedControlRecoveryAttempted &&
            timedControlPhase ==
                TimedControlPhase::Idle &&
            selectedInverterConfigured &&
            selectedInverterProfileKnown &&
            timedControlProfileSupported(
                selectedInverterProfile) &&
            selectedInverterProfile ==
                timedControlRecoverySnapshot.profile &&
            selectedInverterIP ==
                timedControlRecoveryIP &&
            selectedInverterClientStarted &&
            selectedInverterClient.connected()) {
        timedControlRecoveryAttempted = true;
        timedControlRecoveryActive = true;
        timedControlMode = TimedControlMode::Stop;
        timedControlInverterIP =
            timedControlRecoveryIP;
        timedControlSnapshot =
            timedControlRecoverySnapshot;
        timedControlRestoreFailed = false;
        timedControlStartFailed = false;
        beginTimedControlRestoration(
            "recovering after an interrupted timed control",
            false);
    }

    if (timedControlStopRequested) {
        timedControlStopRequested = false;

        if (timedControlPhase ==
                TimedControlPhase::Restoring) {
            setTimedControlStatusMessage(
                "Stop already in progress; restoring saved settings",
                ControlUiTone::Busy);
        } else if (timedControlPhase !=
                TimedControlPhase::Idle) {
            beginTimedControlRestoration(
                "Stop pressed",
                false);
        } else {
            timedControlRequestPending = false;
            if (!selectedInverterConfigured ||
                    !selectedInverterProfileKnown ||
                    !timedControlProfileSupported(
                    selectedInverterProfile) ||
                    !selectedInverterClientStarted ||
                    !selectedInverterClient.connected()) {
                failTimedControlRequest(
                    "a connected single-phase or three-phase inverter is required");
                return;
            }
            timedControlMode = TimedControlMode::Stop;
            timedControlInverterIP = selectedInverterIP;
            if (timedControlRecoveryPending) {
                if (selectedInverterIP !=
                        timedControlRecoveryIP) {
                    failTimedControlRequest(
                        "select the inverter awaiting safety recovery first");
                    return;
                }
                timedControlRecoveryAttempted = true;
                timedControlRecoveryActive = true;
                timedControlSnapshot =
                    timedControlRecoverySnapshot;
            } else {
                timedControlSnapshot =
                    TimedControlSnapshot{};
            }
            timedControlRestoreFailed = false;
            timedControlStartFailed = false;
            beginTimedControlRestoration(
                "Stop pressed",
                false);
        }
    }

    if (timedControlRequestPending) {
    
        const TimedControlMode requestedMode =
            static_cast<TimedControlMode>(
                requestedTimedControlMode);
        const uint16_t duration =
            requestedTimedDurationMinutes;
        const uint16_t target =
            requestedTimedTargetSoc;
        const uint16_t power =
            requestedTimedPowerPercent;
        timedControlRequestPending = false;

        if (timedControlPhase !=
                TimedControlPhase::Idle ||
                controlWorkflowActive ||
                controlRequestPending) {
            failTimedControlRequest(
                "another battery control is active");
            return;
        }
        if (timedControlRecoveryPending) {
            failTimedControlRequest(
                "restore the interrupted timed control before starting another");
            return;
        }
        if (!selectedInverterConfigured ||
                !selectedInverterProfileKnown ||
                !timedControlProfileSupported(
                    selectedInverterProfile)) {
            failTimedControlRequest(
                "timed control requires a recognised single-phase or three-phase inverter");
            return;
}
        if (!selectedInverterClientStarted ||
                !selectedInverterClient.connected()) {
            failTimedControlRequest(
                "the selected inverter is not connected");
            return;
        }
        if (duration < 1 || duration > 240 ||
                target < 4 || target > 100 ||
                power < 1 || power > 100) {
            failTimedControlRequest(
                "duration, SOC or power is outside its safe range");
            return;
        }
        givenergy::LiveData currentLive;

    if (selectedInverterClient.liveData(currentLive)) {
        if (requestedMode == TimedControlMode::Charge &&
                target <= currentLive.batterySocPercent) {
            failTimedControlRequest(
                "charge target must be above the current SOC");
            return;
        }

    if (requestedMode == TimedControlMode::Discharge &&
            target >= currentLive.batterySocPercent) {
        failTimedControlRequest(
            "discharge target must be below the current SOC");
        return;
    }
}

        timedControlMode = requestedMode;
        timedControlDurationMinutes = duration;
        timedControlTargetSoc = target;
        timedControlPowerPercent = power;
        timedControlInverterIP = selectedInverterIP;
        timedControlSnapshot =
            TimedControlSnapshot{};
        timedControlRestoreFailed = false;
        timedControlStartFailed = false;
        clearTimedControlSteps();
        timedControlPhase =
            TimedControlPhase::WaitingForSettings;
        timedControlRequestedMs = millis();
        timedControlSettingsReadStage = 0;
        timedControlSettingsReadQueued = false;
        queueTimedSettingsRead();
        setTimedControlStatusMessage(
            "Reading current inverter settings before control",
            ControlUiTone::Busy);
    }

    if (timedControlPhase ==
            TimedControlPhase::Idle) {
        return;
    }

    if (timedControlPhase ==
            TimedControlPhase::WaitingForSettings) {
        if (selectedInverterIP !=
                timedControlInverterIP) {
            failTimedControlRequest(
                "the selected inverter changed");
            return;
        }
        if (millis() - timedControlRequestedMs >=
                60000U) {
            failTimedControlRequest(
                "could not read current settings safely");
            return;
        }
        if (!selectedInverterClientStarted ||
                !selectedInverterClient.connected()) {
            return;
        }
        if (!timedControlSettingsReadQueued) {
    queueTimedSettingsRead();
    return;
}

uint16_t expectedBase = 0;
uint16_t expectedCount = 0;
timedSettingsReadRange(
    expectedBase,
    expectedCount);

const bool settingsReplyReceived =
    selectedInverterClient.readGeneration() !=
        timedControlSettingsReadGeneration &&
    selectedInverterClient.lastReadBank() ==
        givenergy::RegisterBank::Holding &&
    selectedInverterClient.lastReadBaseRegister() ==
        expectedBase &&
    selectedInverterClient.lastReadRegisterCount() ==
        expectedCount;

if (!settingsReplyReceived) {
    return;
}

if (selectedInverterProfile ==
        givenergy::DeviceProfile::SinglePhase &&
    timedControlSettingsReadStage < 1) {
    // HR0–59 has arrived. Request HR60–119
    ++timedControlSettingsReadStage;
    timedControlSettingsReadQueued = false;
    return;
}
        if (!readTimedControlSnapshot()) {
            failTimedControlRequest(
                "the inverter returned invalid settings");
            return;
        }
        if (!saveTimedControlRecovery()) {
            failTimedControlRequest(
                "could not save a recovery copy of the current settings");
            return;
        }

        if (!buildTimedStartSteps()) {
            beginTimedControlRestoration(
                "could not build the control sequence",
                true);
            return;
}

        timedControlPhase =
            TimedControlPhase::Applying;
        processTimedControlSteps();
        return;
    }

    if (timedControlPhase ==
            TimedControlPhase::Applying ||
            timedControlPhase ==
                TimedControlPhase::Restoring) {
        processTimedControlSteps();
        return;
    }

    if (timedControlPhase !=
            TimedControlPhase::Running) {
        return;
    }

    const uint32_t now = millis();


givenergy::LiveData live;
const bool haveLive =
    selectedInverterClient.liveData(live);

// Confirm that the battery is physically moving power
// in the requested direction.
if (haveLive && !timedControlPowerSeen) {
    // This project uses:
    // Negative battery power = charging
    // Positive battery power = discharging
    const bool correctDirection =
        timedControlMode == TimedControlMode::Charge
            ? live.batteryPowerW <=
                  -MINIMUM_ACTIVE_POWER_W
            : live.batteryPowerW >=
                  MINIMUM_ACTIVE_POWER_W;

    if (correctDirection) {
        timedControlPowerSeen = true;

        setTimedControlStatusMessage(
            timedControlMode ==
                    TimedControlMode::Charge
                ? "Charging power confirmed"
                : "Discharging power confirmed",
            ControlUiTone::Success);
    }
}

// Registers may have accepted the command even though the
// battery did not actually start.
if (!timedControlPowerSeen &&
        now - timedControlPowerCheckStartedMs >=
            POWER_START_TIMEOUT_MS) {
    beginTimedControlRestoration(
        "settings were accepted but battery power did not start",
        true);
    return;
}

const bool timerFinished =
    static_cast<int32_t>(
        now - timedControlEndMs) >= 0;

const bool socFinished =
    haveLive &&
    ((timedControlMode ==
          TimedControlMode::Charge &&
      live.batterySocPercent >=
          timedControlTargetSoc) ||
     (timedControlMode ==
          TimedControlMode::Discharge &&
      live.batterySocPercent <=
          timedControlTargetSoc));

if (timerFinished || socFinished) {
    beginTimedControlRestoration(
        socFinished
            ? "SOC limit reached"
            : "duration finished",
        false);
    return;
}

       if (timedControlLastUiMs == 0 ||
            now - timedControlLastUiMs >= 1000U) {
        timedControlLastUiMs = now;
        const uint32_t remainingSeconds =
            (timedControlEndMs - now + 999U) /
            1000U;
        String status =
            timedControlMode ==
                    TimedControlMode::Charge
                ? "Charging"
                : "Discharging";
        status += " - ";
        status += String(remainingSeconds / 60U);
        status += "m ";
        status += String(remainingSeconds % 60U);
        status += "s remaining";
        if (haveLive) {
            status += " - SOC ";
            status += String(live.batterySocPercent);
            status += "%";
        }
        if (!selectedInverterClient.connected()) {
            status += " - reconnecting";
        }
        setTimedControlStatusMessage(
            status,
            selectedInverterClient.connected()
                ? ControlUiTone::Success
                : ControlUiTone::Failure);
    }
}

static void failControlWorkflow(const char *reason)
{
    controlWorkflowActive = false;

    String message = "Battery control failed";
    if (reason != nullptr && reason[0] != '\0') {
        message += ": ";
        message += reason;
    }

    setControlStatusMessage(
        message,
        ControlUiTone::Failure);

    Serial.println(message);
}

static void updateControlWorkflow()
{
    if (controlRequestPending) {
        const givenergy::ControlId requestedId =
            static_cast<givenergy::ControlId>(
                requestedControlId);
        const uint16_t requestedValue =
            requestedControlValue;
        controlRequestPending = false;

        if (timedControlBusy()) {
            failControlWorkflow(
                "a timed battery control is active");
            return;
        }

        if (!selectedInverterConfigured) {
            failControlWorkflow(
                "no inverter is selected");
            return;
        }

        if (!selectedInverterProfileKnown) {
            failControlWorkflow(
                "the inverter profile is unknown");
            return;
        }

        if (inverterScanner.state() ==
                givenergy::GivEnergyScanner::State::Scanning) {
            failControlWorkflow(
                "an inverter scan is running");
            return;
        }

        if (!selectedInverterClientStarted ||
                !selectedInverterClient.connected()) {
            failControlWorkflow(
                "the selected inverter is not connected");
            return;
        }

        const givenergy::ControlValidation validation =
            selectedInverterClient.control(
                requestedId,
                requestedValue);

        if (validation !=
                givenergy::ControlValidation::Ok) {
            failControlWorkflow(
                validation ==
                        givenergy::ControlValidation::
                            ValueOutOfRange
                    ? "the requested value is outside its allowed range"
                    : "this control is unsupported for the selected profile");
            return;
        }

        activeControlId = requestedId;
        controlWorkflowStartedMs = millis();
        controlWorkflowInverterIP =
            selectedInverterIP;
        controlWorkflowActive = true;

        setControlStatusMessage(
            String("Applying: ") +
                controlName(activeControlId),
            ControlUiTone::Busy);

        Serial.print("Applying battery control: ");
        Serial.print(controlName(activeControlId));
        Serial.print(" value ");
        Serial.println(requestedValue);
    }

    if (!controlWorkflowActive) {
        return;
    }

    if (!selectedInverterConfigured ||
            selectedInverterIP !=
                controlWorkflowInverterIP) {
        failControlWorkflow(
            "the selected inverter changed");
        return;
    }

    if (inverterScanner.state() ==
            givenergy::GivEnergyScanner::State::Scanning) {
        failControlWorkflow(
            "the control was interrupted by a scan");
        return;
    }

    const givenergy::ControlStatus status =
        selectedInverterClient.lastControlStatus();

    if (status ==
            givenergy::ControlStatus::Verified) {
        controlWorkflowActive = false;
        setControlStatusMessage(
            String("Battery control successful: ") +
                controlName(activeControlId),
            ControlUiTone::Success);
        selectedInverterClient.requestRefresh(true);
        return;
    }

    if (status == givenergy::ControlStatus::Failed ||
            status ==
                givenergy::ControlStatus::Rejected) {
        const char *error =
            selectedInverterClient.lastError();
        failControlWorkflow(
            error != nullptr && error[0] != '\0'
                ? error
                : "the inverter rejected the command");
        return;
    }

    if (!selectedInverterClientStarted ||
            selectedInverterClient.state() ==
                givenergy::ClientState::Stopped ||
            selectedInverterClient.state() ==
                givenergy::ClientState::WifiUnavailable) {
        failControlWorkflow(
            "the inverter connection was lost");
        return;
    }

    if (millis() - controlWorkflowStartedMs >= 45000) {
        selectedInverterClient.stop();
        selectedInverterClientStarted = false;
        failControlWorkflow(
            "verification timed out");
    }
}

static void clearStoredInverters()
{
    inverterCount = 0;

    for (size_t i = 0; i < MAX_INVERTERS; ++i) {
        inverterIPs[i] = IPAddress(0, 0, 0, 0);
        inverterStatuses[i] = false;
        inverterSerials[i] = "";
        inverterSlaveAddresses[i] = 0;
        inverterIdentityValid[i] = false;
        inverterProfileKnown[i] = false;
        inverterModelCodes[i] = 0;
        inverterModels[i] =
            givenergy::DeviceModel::Unknown;
        inverterProfiles[i] =
            givenergy::DeviceProfile::SinglePhase;
    }
}

static void storeScannerResult(
    size_t scannerIndex,
    size_t targetIndex)
{
    if (targetIndex >= MAX_INVERTERS ||
            scannerIndex >= inverterScanner.foundCount()) {
        return;
    }

    inverterIPs[targetIndex] =
        inverterScanner.foundAddress(scannerIndex);
    inverterStatuses[targetIndex] = true;

    const char *serial =
        inverterScanner.foundSerial(scannerIndex);
    inverterSerials[targetIndex] =
        serial == nullptr ? "" : String(serial);
    inverterSlaveAddresses[targetIndex] =
        inverterScanner.foundSlaveAddress(scannerIndex);
    inverterIdentityValid[targetIndex] =
        inverterScanner.foundIdentityValid(scannerIndex);
    inverterProfileKnown[targetIndex] =
        inverterScanner.foundProfileKnown(scannerIndex);
    inverterModelCodes[targetIndex] =
        inverterScanner.foundModelCode(scannerIndex);
    inverterModels[targetIndex] =
        inverterScanner.foundModel(scannerIndex);
    inverterProfiles[targetIndex] =
        inverterScanner.foundProfile(scannerIndex);

    if (selectedInverterConfigured &&
            selectedInverterIP ==
                inverterIPs[targetIndex]) {
        selectedInverterSerial =
            inverterSerials[targetIndex];
        selectedInverterSlaveAddress =
            inverterSlaveAddresses[targetIndex] == 0
                ? 0x11
                : inverterSlaveAddresses[targetIndex];
        selectedInverterProfileKnown =
            inverterProfileKnown[targetIndex];
        selectedInverterModelCode =
            inverterModelCodes[targetIndex];
        selectedInverterModel =
            inverterModels[targetIndex];
        selectedInverterProfile =
            inverterProfiles[targetIndex];
    }
}

static size_t findStoredInverter(IPAddress address)
{
    for (size_t i = 0; i < inverterCount; ++i) {
        if (inverterIPs[i] == address) {
            return i;
        }
    }
    return MAX_INVERTERS;
}

static void printStoredInverter(size_t index)
{
    if (index >= inverterCount) {
        return;
    }

    Serial.print("Inverter ");
    Serial.print(index + 1);
    Serial.print(": ");
    Serial.print(inverterIPs[index]);

    if (!inverterSerials[index].isEmpty()) {
        Serial.print("  Serial: ");
        Serial.print(inverterSerials[index]);
    }

    Serial.print("  Slave: 0x");
    Serial.print(
        inverterSlaveAddresses[index],
        HEX);
    Serial.print("  Identity: ");
    Serial.print(
        inverterIdentityValid[index]
            ? "valid"
            : "unavailable");
    Serial.print("  Model: ");
    Serial.print(
        givenergy::deviceModelName(
            inverterModels[index]));
    Serial.print(" (0x");
    Serial.print(inverterModelCodes[index], HEX);
    Serial.print(")  Profile: ");
    Serial.println(
        inverterProfileKnown[index]
            ? givenergy::deviceProfileName(
                  inverterProfiles[index])
            : "unknown");
}

static void updateInverterScanner()
{
    const bool wifiConnected =
        connectivity.getState() ==
        Connectivity::WifiState::Connected;

    const bool startAddressProbe =
        inverterIpProbeRequested &&
        wifiConnected &&
        !timedControlBusy() &&
        inverterScanner.state() !=
            givenergy::GivEnergyScanner::State::Scanning;

    const bool startAutomaticScan =
        AUTO_SCAN_INVERTERS &&
        wifiConnected &&
        !timedControlBusy() &&
        !inverterScanTriggered &&
        !startAddressProbe;

    const bool startSubnetRefresh =
        inverterRescanRequested &&
        wifiConnected &&
        !timedControlBusy() &&
        !startAddressProbe &&
        inverterScanner.state() !=
            givenergy::GivEnergyScanner::State::Scanning;

    inverterRescanRequested = false;

    if (startAddressProbe ||
            startAutomaticScan ||
            startSubnetRefresh) {
        if (selectedInverterClientStarted) {
            selectedInverterClient.stop();
            selectedInverterClientStarted = false;
        }

        if (startAddressProbe) {
            const uint32_t packedAddress =
                requestedInverterIp;
            const IPAddress address(
                static_cast<uint8_t>(
                    packedAddress >> 24U),
                static_cast<uint8_t>(
                    packedAddress >> 16U),
                static_cast<uint8_t>(
                    packedAddress >> 8U),
                static_cast<uint8_t>(
                    packedAddress));

            inverterIpProbeRequested = false;
            inverterScanMode =
                InverterScanMode::ManualAddress;
            inverterIpStatus =
                "Testing " + address.toString() + ":8899";
            inverterScanner.startAddress(address);

            Serial.print(
                "Testing manually entered inverter address ");
            Serial.print(address);
            Serial.println(":8899");
        } else {
            clearStoredInverters();
            inverterScanMode =
                InverterScanMode::Subnet;
            inverterScanner.start();

            Serial.println(
                startSubnetRefresh
                    ? "Starting refreshed GivEnergy subnet scan"
                    : "Starting automatic GivEnergy subnet scan");
        }

        inverterScanTriggered = true;
        inverterScanResultsHandled = false;
        displayedInverterHost = 0;
        displayedInverterSlaveAddress = 0;
        refreshInvertersPage();
    }

    if (inverterScanner.state() ==
            givenergy::GivEnergyScanner::State::Scanning) {
        if (!wifiConnected) {
            inverterScanner.cancel();
            if (inverterScanMode ==
                    InverterScanMode::ManualAddress) {
                inverterIpStatus =
                    "IP test cancelled: Wi-Fi disconnected";
            }
            inverterScanMode = InverterScanMode::None;
            Serial.println("Inverter scan cancelled: Wi-Fi disconnected");
            refreshInvertersPage();
            return;
        }

        inverterScanner.update();
        updateInverterScanAddress();
    }

    const givenergy::GivEnergyScanner::State scanState =
        inverterScanner.state();

    if (inverterScanResultsHandled ||
            (scanState !=
                 givenergy::GivEnergyScanner::State::Finished &&
             scanState !=
                 givenergy::GivEnergyScanner::State::Failed)) {
        return;
    }

    inverterScanResultsHandled = true;
    const InverterScanMode completedMode =
        inverterScanMode;
    inverterScanMode = InverterScanMode::None;

    if (scanState ==
            givenergy::GivEnergyScanner::State::Failed) {
        if (completedMode ==
                InverterScanMode::ManualAddress) {
            inverterIpStatus =
                "Could not test the entered IP";
        }
        Serial.println("GivEnergy inverter scan failed");
        refreshInvertersPage();
        return;
    }

    const size_t foundCount = inverterScanner.foundCount();

    if (completedMode == InverterScanMode::ManualAddress) {
        if (foundCount == 0) {
            inverterIpStatus =
                "No response from " +
                inverterIpText +
                ":8899";
            Serial.println(
                "Entered IP did not accept a port-8899 connection");
        } else {
            const IPAddress foundAddress =
                inverterScanner.foundAddress(0);
            size_t targetIndex =
                findStoredInverter(foundAddress);
            const bool alreadyStored =
                targetIndex < inverterCount;

            if (!alreadyStored) {
                if (inverterCount >= MAX_INVERTERS) {
                    inverterIpStatus =
                        "Inverter list is full";
                    refreshInvertersPage();
                    return;
                }
                targetIndex = inverterCount;
                ++inverterCount;
            }

            storeScannerResult(0, targetIndex);

            if (inverterProfileKnown[targetIndex]) {
                inverterIpStatus =
                    String(alreadyStored ? "Updated " : "Added ") +
                    inverterIPs[targetIndex].toString() +
                    " - " +
                    givenergy::deviceProfileName(
                        inverterProfiles[targetIndex]);
            } else {
                inverterIpStatus =
                    "Port 8899 open; identity unavailable";
            }

            printStoredInverter(targetIndex);
        }
    } else {
        inverterCount =
            foundCount < MAX_INVERTERS
                ? foundCount
                : MAX_INVERTERS;

        for (size_t i = 0; i < inverterCount; ++i) {
            storeScannerResult(i, i);
        }

        Serial.print(
            "GivEnergy subnet scan complete. Inverters found: ");
        Serial.println(inverterCount);

        for (size_t i = 0; i < inverterCount; ++i) {
            printStoredInverter(i);
        }
    }

    refreshInvertersPage();
}

static void updateInverterScanAddress()
{
    if (inverterScanner.state() !=
            givenergy::GivEnergyScanner::State::Scanning) {
        return;
    }

    const uint8_t currentHost = inverterScanner.currentHost();
    const uint8_t currentSlaveAddress =
        inverterScanner.currentSlaveAddress();

    if ((currentHost == displayedInverterHost &&
         currentSlaveAddress ==
             displayedInverterSlaveAddress) ||
            currentPage != Page::Inverters) {
        return;
    }

    lvgl_port_lock(-1);

    if (currentPage == Page::Inverters &&
            inverterScanAddressLabel != nullptr) {
        const IPAddress candidate =
            inverterScanner.currentAddress();

        const String checking =
            "Checking " +
            candidate.toString() +
            ":8899  slave 0x" +
            String(currentSlaveAddress, HEX);

        lv_label_set_text(
            inverterScanAddressLabel,
            checking.c_str());

        displayedInverterHost = currentHost;
        displayedInverterSlaveAddress =
            currentSlaveAddress;
    }

    lvgl_port_unlock();
}


static void updateWifiDropdown()
{
    if (wifiDropdown == nullptr) {
        return;
    }

    String options;
    int selectedIndex = -1;
    const String selectedSSID =
        connectivity.getSelectedSSID();

    for (int i = 0; i < connectivity.getNetworkCount(); i++) {
        String ssid = connectivity.getNetworkSSID(i);

        if (ssid.isEmpty()) {
            ssid = "<Hidden network>";
        }

        options += ssid;

        if (ssid == selectedSSID) {
            selectedIndex = i;
        }

        if (i < connectivity.getNetworkCount() - 1) {
            options += "\n";
        }
    }

    if (options.isEmpty()) {
        options = connectivity.isScanRunning()
            ? "Searching for networks..."
            : "No networks found";
    }

    lv_dropdown_set_options(
        wifiDropdown,
        options.c_str()
    );

    if (selectedIndex >= 0) {
        lv_dropdown_set_selected(
            wifiDropdown,
            static_cast<uint16_t>(selectedIndex));
    } else if (connectivity.getNetworkCount() > 0) {
        connectivity.selectNetwork(0);
        lv_dropdown_set_selected(wifiDropdown, 0);
    }
}

static void updateWifiScan()
{
    const uint32_t scanGeneration =
        connectivity.getScanGeneration();

    // The generation number changes only when an asynchronous scan finishes.
    if (scanGeneration != displayedWifiScanGeneration) {
        displayedWifiScanGeneration = scanGeneration;

        lvgl_port_lock(-1);

        if (currentPage == Page::Wifi) {
            updateWifiDropdown();

            if (wifiRefreshLabel != nullptr) {
                lv_label_set_text(
                    wifiRefreshLabel,
                    "Network list updated");
            }

            if (wifiRefreshButton != nullptr) {
                lv_obj_clear_state(
                    wifiRefreshButton,
                    LV_STATE_DISABLED);
            }
        }

        lvgl_port_unlock();
    }

    if (!wifiScanRequested ||
            connectivity.isScanRunning() ||
            connectivity.getState() ==
                Connectivity::WifiState::Connecting ||
            inverterScanner.state() ==
                givenergy::GivEnergyScanner::State::Scanning) {
        return;
    }

    wifiScanRequested = false;
    connectivity.scan();

    lvgl_port_lock(-1);

    if (currentPage == Page::Wifi &&
            wifiRefreshLabel != nullptr) {
        lv_label_set_text(
            wifiRefreshLabel,
            "Refreshing nearby networks...");
    }

    lvgl_port_unlock();
}

static void wifiRefreshEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            connectivity.isScanRunning()) {
        return;
    }

    wifiScanRequested = true;

    if (wifiRefreshButton != nullptr) {
        lv_obj_add_state(
            wifiRefreshButton,
            LV_STATE_DISABLED);
    }

    if (wifiRefreshLabel != nullptr) {
        const bool waiting =
            connectivity.getState() ==
                Connectivity::WifiState::Connecting ||
            inverterScanner.state() ==
                givenergy::GivEnergyScanner::State::Scanning;

        lv_label_set_text(
            wifiRefreshLabel,
            waiting
                ? "Waiting to refresh networks..."
                : "Refreshing nearby networks...");
    }
}

static void wifiDropdownEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    int selectedIndex =
        lv_dropdown_get_selected(wifiDropdown);

    if (connectivity.selectNetwork(selectedIndex)) {
        String message =
            "Selected: " + connectivity.getSelectedSSID();

        lv_label_set_text(
            wifiStatusLabel,
            message.c_str()
        );
    }
}

static void wifiConnectEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const char *password =
        lv_textarea_get_text(wifiPasswordField);

    if (connectivity.isScanRunning()) {
        lv_label_set_text(
            wifiStatusLabel,
            "Please wait for the\nWi-Fi refresh to finish");
        return;
    }

    if (connectivity.connectSelected(password)) {
        lv_label_set_text(
            wifiStatusLabel,
            "Connecting..."
        );
    } else {
        lv_label_set_text(
            wifiStatusLabel,
            "Select a Wi-Fi network first"
        );
    }
}

static void createWifiControls(lv_obj_t *parent)
{
    wifiRefreshLabel = createText(
        parent,
        connectivity.isScanRunning()
            ? "Refreshing nearby networks..."
            : "Press Refresh to scan again",
        20,
        20,
        colourMuted());

    wifiDropdown = lv_dropdown_create(parent);
    lv_obj_set_size(wifiDropdown, 300, 45);
    lv_obj_set_pos(wifiDropdown, 20, 50);

    lv_obj_add_event_cb(
        wifiDropdown,
        wifiDropdownEvent,
        LV_EVENT_VALUE_CHANGED,
        nullptr
    );

    wifiRefreshButton = lv_btn_create(parent);
    lv_obj_set_size(wifiRefreshButton, 125, 45);
    lv_obj_set_pos(wifiRefreshButton, 335, 50);

    lv_obj_add_event_cb(
        wifiRefreshButton,
        wifiRefreshEvent,
        LV_EVENT_CLICKED,
        nullptr);

    if (connectivity.isScanRunning()) {
        lv_obj_add_state(
            wifiRefreshButton,
            LV_STATE_DISABLED);
    }

    lv_obj_t *refreshButtonLabel =
        lv_label_create(wifiRefreshButton);
    lv_label_set_text(refreshButtonLabel, "Refresh");
    lv_obj_center(refreshButtonLabel);

    wifiPasswordField = lv_textarea_create(parent);
    lv_obj_set_size(wifiPasswordField, 300, 45);
    lv_obj_set_pos(wifiPasswordField, 20, 110);

    lv_textarea_set_placeholder_text(
        wifiPasswordField,
        "Wi-Fi password"
    );

    lv_textarea_set_password_mode(
        wifiPasswordField,
        true
    );

    wifiKeyboard = lv_keyboard_create(parent);
    lv_obj_set_size(wifiKeyboard, 440, 220);
    lv_obj_align(
        wifiKeyboard,
        LV_ALIGN_BOTTOM_RIGHT,
        0,
        0
    );

    lv_keyboard_set_textarea(
        wifiKeyboard,
        wifiPasswordField
    );

    lv_obj_t *connectButton = lv_btn_create(parent);
    lv_obj_set_size(connectButton, 140, 45);
    lv_obj_set_pos(connectButton, 20, 175);

    lv_obj_add_event_cb(
        connectButton,
        wifiConnectEvent,
        LV_EVENT_CLICKED,
        nullptr
    );

    lv_obj_t *buttonLabel = lv_label_create(connectButton);
    lv_label_set_text(buttonLabel, "Connect");
    lv_obj_center(buttonLabel);

    lv_obj_t *statusCard = lv_obj_create(parent);
    lv_obj_set_pos(statusCard, 20, 235);
    lv_obj_set_size(statusCard, 180, 110);
    lv_obj_clear_flag(statusCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(statusCard, colourPanel(), 0);
    lv_obj_set_style_bg_opa(statusCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(statusCard, 0, 0);
    lv_obj_set_style_radius(statusCard, 10, 0);

    wifiStatusLabel = lv_label_create(statusCard);
    lv_label_set_text(
        wifiStatusLabel,
        "Not connected\nSelect a network");
    lv_obj_set_width(wifiStatusLabel, 145);
    lv_label_set_long_mode(
        wifiStatusLabel,
        LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(wifiStatusLabel, 5, 5);
    lv_obj_set_style_text_color(
        wifiStatusLabel,
        colourMuted(),
        0);

    updateWifiDropdown();
    updateWifiStatus(nullptr);
}

void loop()
{
    connectivity.update();
    updateInverterScanner();
    processInverterSelection();
    updateSelectedInverterClient();
    updateCellTelemetry();
    updateTimedControlWorkflow();
    updateControlWorkflow();
    updateWifiScan();
    // LVGL is serviced by the task created by lvgl_v8_port.cpp.

    delay(5);
}
