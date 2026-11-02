#pragma once
#include <Arduino.h>

namespace Settings {
struct Mqtt {
  String host;
  uint16_t port = 1889;
  String deviceId;
  String username;
  String password;
};

// Single-record NVS write; settings are unrelated to the cardholder and
// transaction LittleFS files. The checksum catches corruption, not tampering.
bool valid(const Mqtt& value);
bool load(Mqtt& value);
bool save(const Mqtt& value);
}
