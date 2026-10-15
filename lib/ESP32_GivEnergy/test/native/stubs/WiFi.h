#pragma once

#include "Arduino.h"

using wl_status_t = int;

constexpr wl_status_t WL_NO_SSID_AVAIL = 1;
constexpr wl_status_t WL_CONNECTED = 3;
constexpr wl_status_t WL_CONNECT_FAILED = 4;
constexpr int WIFI_STA = 1;
constexpr int16_t WIFI_SCAN_RUNNING = -1;
constexpr int16_t WIFI_SCAN_FAILED = -2;

class WiFiClass {
 public:
  wl_status_t status() const { return WL_CONNECTED; }
  IPAddress localIP() const { return IPAddress(192, 168, 1, 10); }
  void mode(int) {}
  void scanDelete() {}
  int16_t scanNetworks(bool, bool) { return WIFI_SCAN_RUNNING; }
  int16_t scanComplete() { return 0; }
  String SSID(int = 0) const { return String("Test network"); }
  int32_t RSSI(int) const { return -40; }
  void begin(const char *, const char *) {}
};

extern WiFiClass WiFi;

class WiFiClient {
 public:
  bool connect(IPAddress, uint16_t, int32_t) { return true; }
  bool connected() const { return true; }
  void stop() {}
  void setNoDelay(bool) {}
  void setTimeout(uint32_t) {}
  int available() const { return 0; }
  int read(uint8_t *, size_t) { return 0; }
  size_t write(const uint8_t *, size_t length) { return length; }
  void flush() {}
};
