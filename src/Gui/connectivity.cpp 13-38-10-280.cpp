#include "connectivity.h"

void Connectivity::scan()
{
    if (scanRunning ||
            wifiState == WifiState::Connecting) {
        return;
    }

    WiFi.mode(WIFI_STA);

    // Remove old SDK scan results from RAM. This does not disconnect STA mode.
    WiFi.scanDelete();

    const int16_t result =
        WiFi.scanNetworks(
            true,   // asynchronous
            false  // do not include hidden SSIDs
        );

    if (result == WIFI_SCAN_RUNNING) {
        scanRunning = true;
        return;
    }

    if (result >= 0) {
        storeScanResults(result);
        return;
    }

    scanRunning = false;
    ++scanGeneration;
}

void Connectivity::update()
{
    if (scanRunning) {
        const int16_t result = WiFi.scanComplete();

        if (result >= 0) {
            storeScanResults(result);
        } else if (result == WIFI_SCAN_FAILED) {
            scanRunning = false;
            WiFi.scanDelete();
            ++scanGeneration;
        }
    }

    const wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
        connectedSSID = WiFi.SSID();
        wifiState = WifiState::Connected;
        return;
    }

    if (wifiState == WifiState::Connecting) {
        if (status == WL_CONNECT_FAILED ||
                status == WL_NO_SSID_AVAIL ||
                millis() - wifiConnectStarted >=
                    WIFI_TIMEOUT_MS) {
            wifiState = WifiState::Failed;
        }
        return;
    }

    if (wifiState == WifiState::Connected) {
        connectedSSID = "";
        wifiState = WifiState::Disconnected;
    }
}

void Connectivity::storeScanResults(int foundCount)
{
    const String oldSelection = selectedSSID;

    networkCount = 0;

    for (int i = 0;
            i < foundCount && networkCount < MAX_NETWORKS;
            ++i) {
        const String ssid = WiFi.SSID(i);

        if (ssid.isEmpty()) {
            continue;
        }

        const int32_t rssi = WiFi.RSSI(i);
        int duplicateIndex = -1;

        for (int j = 0; j < networkCount; ++j) {
            if (networkSSIDs[j] == ssid) {
                duplicateIndex = j;
                break;
            }
        }

        if (duplicateIndex >= 0) {
            if (rssi > networkRSSIs[duplicateIndex]) {
                networkRSSIs[duplicateIndex] = rssi;
            }
            continue;
        }

        networkSSIDs[networkCount] = ssid;
        networkRSSIs[networkCount] = rssi;
        ++networkCount;
    }

    // Keep the strongest networks at the top of the dropdown.
    for (int i = 1; i < networkCount; ++i) {
        const String ssid = networkSSIDs[i];
        const int32_t rssi = networkRSSIs[i];
        int j = i - 1;

        while (j >= 0 && networkRSSIs[j] < rssi) {
            networkSSIDs[j + 1] = networkSSIDs[j];
            networkRSSIs[j + 1] = networkRSSIs[j];
            --j;
        }

        networkSSIDs[j + 1] = ssid;
        networkRSSIs[j + 1] = rssi;
    }

    for (int i = networkCount; i < MAX_NETWORKS; ++i) {
        networkSSIDs[i] = "";
        networkRSSIs[i] = 0;
    }

    selectedNetworkIndex = -1;

    for (int i = 0; i < networkCount; ++i) {
        if (networkSSIDs[i] == oldSelection) {
            selectedNetworkIndex = i;
            selectedSSID = networkSSIDs[i];
            break;
        }
    }

    if (selectedNetworkIndex < 0 && networkCount > 0) {
        selectedNetworkIndex = 0;
        selectedSSID = networkSSIDs[0];
    } else if (networkCount == 0 &&
            wifiState != WifiState::Connected) {
        selectedSSID = "";
    }

    scanRunning = false;
    WiFi.scanDelete();
    ++scanGeneration;
}

bool Connectivity::selectNetwork(int index)
{
    if (index < 0 || index >= networkCount) {
        return false;
    }

    selectedNetworkIndex = index;
    selectedSSID = networkSSIDs[index];
    return true;
}

bool Connectivity::connectSelected(const char *password)
{
    if (selectedNetworkIndex < 0 ||
            selectedNetworkIndex >= networkCount ||
            selectedSSID.isEmpty() ||
            scanRunning) {
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(
        selectedSSID.c_str(),
        password == nullptr ? "" : password);

    wifiConnectStarted = millis();
    wifiState = WifiState::Connecting;
    return true;
}

int Connectivity::getNetworkCount() const
{
    return networkCount;
}

String Connectivity::getNetworkSSID(int index) const
{
    if (index < 0 || index >= networkCount) {
        return "";
    }

    return networkSSIDs[index];
}

int32_t Connectivity::getNetworkRSSI(int index) const
{
    if (index < 0 || index >= networkCount) {
        return 0;
    }

    return networkRSSIs[index];
}

String Connectivity::getSelectedSSID() const
{
    return selectedSSID;
}

String Connectivity::getConnectedSSID() const
{
    return connectedSSID;
}

Connectivity::WifiState Connectivity::getState() const
{
    return wifiState;
}

IPAddress Connectivity::getLocalIP() const
{
    return WiFi.localIP();
}

bool Connectivity::isScanRunning() const
{
    return scanRunning;
}

uint32_t Connectivity::getScanGeneration() const
{
    return scanGeneration;
}
