#pragma once

#include <Arduino.h>

class GivScanner {

public:
//anything under public can be accessed by code outside the class through a 
//GivScanner object
  enum class State : uint8_t
  {
    Idle,
    Scanning,
    Finished,
    Failed
  };

  GivScanner();

  void start();
  void update();
  void clear();

  State getState() const;
  bool isScanning() const;

  size_t getCount() const;
  IPAddress getIP(size_t index) const;
  bool isOnline(size_t index) const;

  uint16_t getCurrantHost() const;


private:
//Anything under private can only be directly accessed by functions belonging 
//to the class 
  static constexpr size_t MAX_INVERTERS = 20;

  static constexpr uint16_t FIRST_HOST = 1;
  static constexpr uint16_t LAST_HOST = 254;
  static constexpr uint16_t INVERTER_PORT = 8899;

  static constexpr uint32_t CONNECT_TIMEOUT_MS = 150;
  static constexpr uint32_t TIME_BETWEEN_PROBES_MS = 20;

  State scanState = State::Idle;

  IPAddress localIP;
  IPAddress subnetMask;

  uint16_t currentHost = FIRST_HOST;
  uint32_t lastProbeTime = 0;

  IPAddress foundIPs[MAX_INVERTERS] = {};
  bool inverterStatuses[MAX_INVERTERS] = {};
  size_t foundCount = 0;

  bool probeInverter(const IPAddress &ip);
  bool addInverter(const IPAddress &ip, bool online);

};