#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

inline uint32_t millis() {
  static uint32_t value = 0;
  return ++value;
}

class String {
 public:
  String() = default;
  String(const char *value) : value_(value == nullptr ? "" : value) {}
  String(const std::string &value) : value_(value) {}

  String &operator=(const char *value) {
    value_ = value == nullptr ? "" : value;
    return *this;
  }

  bool operator==(const String &other) const {
    return value_ == other.value_;
  }

  bool isEmpty() const { return value_.empty(); }
  const char *c_str() const { return value_.c_str(); }

 private:
  std::string value_;
};

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : bytes_{a, b, c, d} {}

  uint8_t operator[](size_t index) const { return bytes_[index]; }
  uint8_t &operator[](size_t index) { return bytes_[index]; }

 private:
  uint8_t bytes_[4]{};
};

class HardwareSerial {
 public:
  template <typename T>
  void print(const T &) {}

  template <typename T>
  void println(const T &) {}
};

inline HardwareSerial Serial;
