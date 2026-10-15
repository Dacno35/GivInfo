#pragma once

#include <Arduino.h>
#include <WiFi.h>

class Connectivity {
public:
    enum class WifiState : uint8_t {
        Disconnected,
        Connecting,
        Connected,
        Failed
    };

    // Starts a background Wi-Fi scan. Results are collected by update().
    void scan();
    void update();

    bool selectNetwork(int index);
    bool connectSelected(const char *password);

    int getNetworkCount() const;
    String getNetworkSSID(int index) const;
    int32_t getNetworkRSSI(int index) const;

    String getSelectedSSID() const;
    String getConnectedSSID() const;
    WifiState getState() const;
    IPAddress getLocalIP() const;

    bool isScanRunning() const;
    uint32_t getScanGeneration() const;

private:
    static constexpr int MAX_NETWORKS = 20;
    static constexpr uint32_t WIFI_TIMEOUT_MS = 15000;

    void storeScanResults(int foundCount);

    String networkSSIDs[MAX_NETWORKS];
    int32_t networkRSSIs[MAX_NETWORKS] = {};
    int networkCount = 0;
    int selectedNetworkIndex = -1;

    String selectedSSID;
    String connectedSSID;

    WifiState wifiState = WifiState::Disconnected;
    uint32_t wifiConnectStarted = 0;

    bool scanRunning = false;
    uint32_t scanGeneration = 0;
};
