#include "../include/DeviceConfig.h"
#include <Preferences.h>
#include <stddef.h>
#include <cstring>

namespace {
constexpr uint32_t MAGIC = 0x49384D51UL; // I8MQ
constexpr uint16_t VERSION = 1;
constexpr char PREF_NAMESPACE[] = "i8mqtt";
constexpr char PREF_KEY[] = "settings";

struct __attribute__((packed)) Stored {
  uint32_t magic;
  uint16_t version;
  uint16_t port;
  char host[96];
  char deviceId[48];
  char username[64];
  char password[96];
  uint32_t checksum;
};
static_assert(sizeof(Stored) < 512, "NVS config record exceeds expected size");

uint32_t hashBytes(const uint8_t* p, size_t length) {
  uint32_t value = 2166136261UL;
  for (size_t i = 0; i < length; ++i) value = (value ^ p[i]) * 16777619UL;
  return value;
}
bool boundedText(const String& value, size_t max, bool required) {
  if ((required && value.length() == 0) || value.length() >= max) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == 0 || c == '\r' || c == '\n') return false;
  }
  return true;
}
bool terminated(const char* text, size_t length) {
  return memchr(text, 0, length) != nullptr;
}
bool isHostCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ':';
}
bool isIdCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_';
}
}
namespace Settings {
bool valid(const Mqtt& value) {
  if (!value.port ||
      !boundedText(value.host, sizeof(Stored::host), true) ||
      !boundedText(value.deviceId, sizeof(Stored::deviceId), true) ||
      !boundedText(value.username, sizeof(Stored::username), false) ||
      !boundedText(value.password, sizeof(Stored::password), false)) return false;
  for (char c : value.host) if (!isHostCharacter(c)) return false;
  for (char c : value.deviceId) if (!isIdCharacter(c)) return false;
  return true;
}
bool save(const Mqtt& value) {
  if (!valid(value)) return false;
  Stored stored = {};
  stored.magic = MAGIC;
  stored.version = VERSION;
  stored.port = value.port;
  value.host.toCharArray(stored.host, sizeof(stored.host));
  value.deviceId.toCharArray(stored.deviceId, sizeof(stored.deviceId));
  value.username.toCharArray(stored.username, sizeof(stored.username));
  value.password.toCharArray(stored.password, sizeof(stored.password));
  stored.checksum = hashBytes(reinterpret_cast<const uint8_t*>(&stored),
                               offsetof(Stored, checksum));
  Preferences pref;
  if (!pref.begin(PREF_NAMESPACE, false)) return false;
  // Writing one NVS blob avoids a mixture of old and new MQTT fields.
  bool written = pref.putBytes(PREF_KEY, &stored, sizeof(stored)) == sizeof(stored);
  pref.end();
  return written;
}
bool load(Mqtt& value) {
  Preferences pref;
  if (!pref.begin(PREF_NAMESPACE, true)) return false;
  Stored stored = {};
  bool ok = pref.getBytesLength(PREF_KEY) == sizeof(stored) &&
            pref.getBytes(PREF_KEY, &stored, sizeof(stored)) == sizeof(stored);
  pref.end();
  if (!ok || stored.magic != MAGIC || stored.version != VERSION ||
      !terminated(stored.host, sizeof(stored.host)) ||
      !terminated(stored.deviceId, sizeof(stored.deviceId)) ||
      !terminated(stored.username, sizeof(stored.username)) ||
      !terminated(stored.password, sizeof(stored.password)) ||
      stored.checksum != hashBytes(reinterpret_cast<const uint8_t*>(&stored),
                                   offsetof(Stored, checksum))) return false;
  Mqtt temp;
  temp.host = stored.host;
  temp.deviceId = stored.deviceId;
  temp.username = stored.username;
  temp.password = stored.password;
  temp.port = stored.port;
  if (!valid(temp)) return false;
  value = temp;
  return true;
}
}
