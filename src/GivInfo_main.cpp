#include <Arduino.h>
#include <cstdint>
#include <esp_display_panel.hpp>
#include <lvgl.h>
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

// Only one automatic inverter scan is allowed per boot. This prevents a
// reconnecting Wi-Fi network from repeatedly opening port 8899.
// Set this to false if GivTCP or another local Modbus client is already using
// the inverter. There is no risk-free way to probe 8899 while another client
// owns the inverter's single local socket.
static constexpr bool AUTO_SCAN_INVERTERS = true;
static bool inverterScanTriggered = false;
static bool inverterScanResultsHandled = false;
static volatile bool inverterRescanRequested = false;

static lv_obj_t *wifiDropdown = nullptr;
static lv_obj_t *wifiPasswordField = nullptr;
static lv_obj_t *wifiKeyboard = nullptr;
static lv_obj_t *wifiStatusLabel = nullptr;
static lv_obj_t *topWifiStatusLabel = nullptr;
static lv_obj_t *wifiRefreshLabel = nullptr;
static lv_obj_t *inverterScanAddressLabel = nullptr;

static constexpr uint32_t WIFI_AUTO_REFRESH_MS = 10000;
static uint32_t lastWifiScanStartedMs = 0;
static uint32_t displayedWifiScanGeneration = 0;
static uint8_t displayedInverterHost = 0;

enum class Page : uint8_t {
    Home, // 0
    Inverters, // 1
    Battery, // 2 
    Controls, // 3
    Faults, // 4
    Wifi, // 5
    Settings, // 6 
    Count // 7
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

static size_t inverterCount = 0;

//These will point to permanent parts of the interface
//The 

static lv_obj_t *navButtons[PAGE_COUNT] = {};

static const char *PAGE_NAMES[PAGE_COUNT] = {
    "Home",
    "Inverters",
    "Battery",
    "Controls",
    "Faults",
    "Wi-Fi",
    "Settings"
};

static void inverterRefreshEvent(lv_event_t *event);

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
    int rowNumber,
    const char *name,
    const char *address,
    bool online)
{
    const int y = 45 + (rowNumber * 57);

    lv_obj_t *row = lv_obj_create(contentArea);

    lv_obj_set_pos(row, 15, y);
    lv_obj_set_size(row, 610, 49);

    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(row, colourPanel(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);

    createText(row, name, 12, 14, colourText());
    createText(row, address, 200, 14, colourMuted());
    createText(row, online ? "Online" : "Offline", 500, 14, lv_color_hex(0x22C55E));
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

    if (connectivity.getState() !=
            Connectivity::WifiState::Connected) {
        lv_obj_add_state(refreshButton, LV_STATE_DISABLED);

        createText(
            contentArea,
            "Connect to Wi-Fi before scanning",
            18,
            60,
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
            60,
            colourMuted());
        return;
    }

    if (scanState ==
            givenergy::GivEnergyScanner::State::Scanning) {
        lv_obj_add_state(refreshButton, LV_STATE_DISABLED);

        const IPAddress localIP = connectivity.getLocalIP();
        const IPAddress candidate(
            localIP[0],
            localIP[1],
            localIP[2],
            inverterScanner.currentHost());

        createText(
            contentArea,
            "Scanning for GivEnergy inverters...",
            18,
            60,
            lv_color_hex(0xF59E0B));

        const String checking =
            "Checking " + candidate.toString() + ":8899";

        inverterScanAddressLabel = createText(
            contentArea,
            checking.c_str(),
            18,
            92,
            colourMuted());

        displayedInverterHost = inverterScanner.currentHost();
        return;
    }

    if (scanState ==
            givenergy::GivEnergyScanner::State::Failed) {
        createText(
            contentArea,
            "Inverter scan failed",
            18,
            60,
            lv_color_hex(0xEF4444));
        return;
    }

    if (inverterCount == 0) {
        createText(
            contentArea,
            "No GivEnergy inverters detected",
            18,
            60,
            colourMuted());
        return;
    }

    for (int i = 0;i < inverterCount; i++ )
    {
        String name = inverterSerials[i];

        if (name.isEmpty()) {
            name = String("Inverter ") + String(i + 1);
        }

        String address = inverterIPs[i].toString();


        createInverterRow(static_cast<int>(i),name.c_str(), address.c_str(),inverterStatuses[i]);
    }//static_cast converts i from size_t into an int
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

static void createControlSlider(
    const char *name,
    int y,
    int initialValue)
{
    createText(contentArea, name, 18, y, colourText());

    lv_obj_t *value = createText(
        contentArea,
        "",
        570,
        y,
        colourText());

    lv_label_set_text_fmt(value, "%d%%", initialValue);

    lv_obj_t *slider = lv_slider_create(contentArea);

    lv_obj_set_pos(slider, 18, y + 28);
    lv_obj_set_size(slider, 585, 14);

    lv_slider_set_range(slider, 0, 100);
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

    lv_obj_add_event_cb(
        slider,
        sliderChanged,
        LV_EVENT_VALUE_CHANGED,
        value);
}

static void createControlsPage()
{
    createText(
        contentArea,
        "Battery controls - not connected yet",
        18,
        15,
        colourText());

    createControlSlider("Charge target", 60, 80);
    createControlSlider("Minimum reserve", 135, 15);
    createControlSlider("Charge limit", 210, 50);
    createControlSlider("Discharge limit", 285, 60);

    lv_obj_t *reviewButton = lv_btn_create(contentArea);

    lv_obj_set_pos(reviewButton, 445, 354);
    lv_obj_set_size(reviewButton, 160, 50);
    lv_obj_set_style_bg_color(reviewButton, colourAccent(), 0);

    lv_obj_t *label = lv_label_create(reviewButton);
    lv_label_set_text(label, "Review changes");
    lv_obj_center(label);
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
static void updateAutomaticWifiScan();
static void updateInverterScanner();
static void updateInverterScanAddress();
static void refreshInvertersPage();
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
    }

    if (currentPage == Page::Inverters) {
        inverterScanAddressLabel = nullptr;
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
            createPlaceholderPage("Battery page comes here");
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
    lv_obj_set_size(button, 130, 47);

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

    createNavigationButton(Page::Home,      55);
    createNavigationButton(Page::Inverters, 108);
    createNavigationButton(Page::Battery,   161);
    createNavigationButton(Page::Controls,  214);
    createNavigationButton(Page::Faults,    267);
    createNavigationButton(Page::Wifi,      320);
    createNavigationButton(Page::Settings,  373);

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

    showPage(Page::Home);
}

void setup()
{

    Serial.begin(115200);
    delay(500);

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
    lastWifiScanStartedMs = millis();
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

static void inverterRefreshEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
            connectivity.getState() !=
                Connectivity::WifiState::Connected ||
            inverterScanner.state() ==
                givenergy::GivEnergyScanner::State::Scanning) {
        return;
    }

    // The callback runs in LVGL's task. Let loop() start the network
    // operation so UI and networking work are not mixed across tasks.
    inverterRescanRequested = true;
}

static void updateInverterScanner()
{
    const bool wifiConnected =
        connectivity.getState() ==
        Connectivity::WifiState::Connected;

    const bool startAutomaticScan =
        AUTO_SCAN_INVERTERS &&
        wifiConnected &&
        !inverterScanTriggered;

    const bool startManualScan =
        inverterRescanRequested &&
        wifiConnected &&
        inverterScanner.state() !=
            givenergy::GivEnergyScanner::State::Scanning;

    inverterRescanRequested = false;

    // Start once automatically after Wi-Fi connects, or when the user presses
    // Refresh. Never run two scanner passes at the same time.
    if (startAutomaticScan || startManualScan) {
        inverterCount = 0;

        for (size_t i = 0; i < MAX_INVERTERS; ++i) {
            inverterIPs[i] = IPAddress(0, 0, 0, 0);
            inverterStatuses[i] = false;
            inverterSerials[i] = "";
        }

        inverterScanner.start();
        inverterScanTriggered = true;
        inverterScanResultsHandled = false;

        Serial.println(
            startManualScan
                ? "Starting manual GivEnergy inverter scan"
                : "Starting automatic GivEnergy inverter scan");
        refreshInvertersPage();
    }

    if (inverterScanner.state() ==
            givenergy::GivEnergyScanner::State::Scanning) {
        if (!wifiConnected) {
            inverterScanner.cancel();
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

    if (scanState ==
            givenergy::GivEnergyScanner::State::Failed) {
        Serial.println("GivEnergy inverter scan failed");
        refreshInvertersPage();
        return;
    }

    const size_t foundCount = inverterScanner.foundCount();
    inverterCount =
        foundCount < MAX_INVERTERS
            ? foundCount
            : MAX_INVERTERS;

    Serial.print("GivEnergy scan complete. Inverters found: ");
    Serial.println(inverterCount);

    for (size_t i = 0; i < inverterCount; ++i) {
        inverterIPs[i] = inverterScanner.foundAddress(i);
        inverterStatuses[i] = true;

        const char *serial = inverterScanner.foundSerial(i);
        inverterSerials[i] =
            serial == nullptr ? "" : String(serial);

        Serial.print("Inverter ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(inverterIPs[i]);

        if (!inverterSerials[i].isEmpty()) {
            Serial.print("  Serial: ");
            Serial.print(inverterSerials[i]);
        }

        Serial.println();
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

    if (currentHost == displayedInverterHost ||
            currentPage != Page::Inverters) {
        return;
    }

    lvgl_port_lock(-1);

    if (currentPage == Page::Inverters &&
            inverterScanAddressLabel != nullptr) {
        const IPAddress localIP = connectivity.getLocalIP();
        const IPAddress candidate(
            localIP[0],
            localIP[1],
            localIP[2],
            currentHost);

        const String checking =
            "Checking " + candidate.toString() + ":8899";

        lv_label_set_text(
            inverterScanAddressLabel,
            checking.c_str());

        displayedInverterHost = currentHost;
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

static void updateAutomaticWifiScan()
{
    const uint32_t scanGeneration =
        connectivity.getScanGeneration();

    // The generation number changes only when an asynchronous scan completes.
    if (scanGeneration != displayedWifiScanGeneration) {
        displayedWifiScanGeneration = scanGeneration;

        lvgl_port_lock(-1);

        if (currentPage == Page::Wifi) {
            updateWifiDropdown();

            if (wifiRefreshLabel != nullptr) {
                lv_label_set_text(
                    wifiRefreshLabel,
                    "Networks updated automatically");
            }
        }

        lvgl_port_unlock();
    }

    if (currentPage != Page::Wifi ||
            connectivity.isScanRunning() ||
            connectivity.getState() ==
                Connectivity::WifiState::Connecting ||
            inverterScanner.state() ==
                givenergy::GivEnergyScanner::State::Scanning ||
            millis() - lastWifiScanStartedMs <
                WIFI_AUTO_REFRESH_MS) {
        return;
    }

    connectivity.scan();
    lastWifiScanStartedMs = millis();

    lvgl_port_lock(-1);

    if (currentPage == Page::Wifi &&
            wifiRefreshLabel != nullptr) {
        lv_label_set_text(
            wifiRefreshLabel,
            "Refreshing nearby networks...");
    }

    lvgl_port_unlock();
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
            : "Networks refresh automatically",
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
    updateAutomaticWifiScan();
    // LVGL is serviced by the task created by lvgl_v8_port.cpp.

    delay(5);
}
