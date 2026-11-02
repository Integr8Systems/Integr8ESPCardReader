#include <Arduino.h>
#include <ArduinoJson.h>
#include <ETH.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <time.h>
#include "config.h"
#include "storage.h"

// IMPORTANT: wire-level payload transform from Integr8Controller must be applied here
// after confirming its exact implementation. Plain JSON is development-only.
Store store;
HardwareSerial rfid(1);
NetworkClient network;
PubSubClient mqtt(network);
String topicIn, topicAck, topicOut;
unsigned long lastConnect = 0, lastPublish = 0;
String rfidBuffer;
uint64_t inFlight = 0;

uint64_t jsonU64(JsonVariantConst value) {
  String s = value.as<String>();
  if (!s.length()) return 0;
  for (char c : s) if (c < '0' || c > '9') return 0;
  return strtoull(s.c_str(), nullptr, 10);
}
// Accept server dates as UTC yyyy-MM-dd HH:mm:ss or epoch integer.
// Always interpret the agreed wire format as UTC, independent of local timezone.
bool parseUtcEpoch(JsonVariantConst value, uint32_t& result) {
  if (value.is<uint32_t>()) { result = value.as<uint32_t>(); return true; }
  String input = value.as<String>();
  if (input.length() == 0) return false;
  if (input.length() != 19) {
    for (char c : input) if (c < '0' || c > '9') return false;
    unsigned long long n = strtoull(input.c_str(), nullptr, 10);
    if (n > UINT32_MAX) return false;
    result = static_cast<uint32_t>(n); return true;
  }
  int year, month, day, hour, minute, second;
  if (sscanf(input.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
             &year, &month, &day, &hour, &minute, &second) != 6) return false;
  if (year < 1970 || year > 2106 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour > 23 || minute > 59 || second > 59) return false;
  struct tm utc = {};
  utc.tm_year = year - 1900; utc.tm_mon = month - 1; utc.tm_mday = day;
  utc.tm_hour = hour; utc.tm_min = minute; utc.tm_sec = second;
  time_t timestamp = timegm(&utc);
  if (timestamp < 0 || static_cast<uint64_t>(timestamp) > UINT32_MAX) return false;
  struct tm verified;
  if (!gmtime_r(&timestamp, &verified) || verified.tm_year != utc.tm_year ||
      verified.tm_mon != utc.tm_mon || verified.tm_mday != utc.tm_mday ||
      verified.tm_hour != utc.tm_hour || verified.tm_min != utc.tm_min ||
      verified.tm_sec != utc.tm_sec) return false;
  result = static_cast<uint32_t>(timestamp);
  return true;
}
bool validClock(uint32_t& epoch) {
  time_t now = time(nullptr);
  if (now < 1760000000LL || now > UINT32_MAX) return false;
  epoch = static_cast<uint32_t>(now);
  return true;
}
void onMqtt(char* topic, uint8_t* payload, unsigned int length) {
  if (length > 1024) return;
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) return;
  String t(topic);
  if (t == topicAck) {
    uint64_t seq = jsonU64(doc["Sequence"]);
    // Application ACK, not MQTT PUBACK. Server must commit before acknowledging.
    if (seq && seq == inFlight && doc["Accepted"].as<bool>() && store.acknowledge(seq)) inFlight = 0;
    return;
  }
  if (t != topicIn) return;
  const char* command = doc["Command"] | "";
  JsonDocument response;
  response["Command"] = command;
  response["MessageID"] = doc["MessageID"].as<String>();
  bool ok = false;
  if (!strcmp(command, "CardUpsert")) {
    uint64_t card = jsonU64(doc["CardNumber"]);
    uint32_t fromEpoch = 0, toEpoch = 0;
    bool datesOk = parseUtcEpoch(doc["ValidFrom"], fromEpoch) &&
                   parseUtcEpoch(doc["ValidTo"], toEpoch);
    CardRecord r = {
      doc["AssetID"].as<uint32_t>(), card, fromEpoch,
      toEpoch,
      static_cast<uint8_t>((doc["Enabled"].as<bool>() ? FLAG_ENABLED : 0) |
        (doc["Suspended"].as<bool>() ? FLAG_SUSPENDED : 0) |
        (doc["Blacklisted"].as<bool>() ? FLAG_BLACKLISTED : 0)), 0
    };
    ok = datesOk && store.upsert(r);
  } else if (!strcmp(command, "CardDelete")) {
    ok = store.remove(doc["AssetID"].as<uint32_t>());
  }
  response["Success"] = ok;
  response["CardCount"] = store.cards();
  String out; serializeJson(response, out);
  mqtt.publish(topicOut.c_str(), out.c_str(), false);
}
void reconnect() {
  if (mqtt.connected() || millis() - lastConnect < Config::MQTT_RETRY_MS) return;
  lastConnect = millis();
  if (mqtt.connect(Config::DEVICE_ID, Config::MQTT_USER, Config::MQTT_PASSWORD)) {
    mqtt.subscribe(topicIn.c_str(), 1);
    mqtt.subscribe(topicAck.c_str(), 1);
    inFlight = 0; // Resend oldest pending after reconnect.
  }
}
void queueCard(const String& raw) {
  if (raw.isEmpty() || raw.length() > 20) return;
  for (char c : raw) if (!isDigit(c)) return;
  uint64_t number = strtoull(raw.c_str(), nullptr, 10);
  if (!number) return;
  uint32_t epoch;
  if (!validClock(epoch)) { Serial.println("Clock not synced: rejecting scan"); return; }
  uint32_t assetId = 0;
  bool allowed = store.lookup(number, epoch, assetId);
  // Unknown card is logged with AssetID 0. Future: add result/reason field.
  if (!store.append(assetId, epoch)) Serial.println("Transaction write failed");
  Serial.printf("Card scanned: asset=%lu, allowed=%d\n", (unsigned long)assetId, allowed);
  // Access relay control is intentionally NOT implemented in this phase.
}
void pollRfid() {
  while (rfid.available()) {
    char c = static_cast<char>(rfid.read());
    if (c == '\r' || c == '\n' || c == '\x03') {
      if (rfidBuffer.length()) queueCard(rfidBuffer);
      rfidBuffer = "";
    } else if (c >= '0' && c <= '9' && rfidBuffer.length() < 20) {
      rfidBuffer += c;
    } else if (c != '\x02') rfidBuffer = "";
  }
}
void sendOldest() {
  if (!mqtt.connected() || millis() - lastPublish < Config::MQTT_RETRY_MS) return;
  TransactionRecord r;
  if (!store.oldest(r)) { inFlight = 0; return; }
  inFlight = r.sequence;
  JsonDocument doc;
  // String avoids loss of 64-bit precision in external JSON consumers.
  char sequence[24];
  snprintf(sequence, sizeof(sequence), "%llu", static_cast<unsigned long long>(r.sequence));
  doc["Sequence"] = sequence;
  doc["AssetID"] = r.assetId;
  doc["Epoch"] = r.epoch;
  String payload; serializeJson(doc, payload);
  if (mqtt.publish(topicOut.c_str(), payload.c_str(), false)) lastPublish = millis();
}
void setup() {
  Serial.begin(115200);
  rfid.begin(Config::RFID_BAUD, SERIAL_8N1, Config::RFID_RX_GPIO, Config::RFID_TX_GPIO);
  if (!store.begin()) { Serial.println("Storage unavailable, halt (no auto-format)"); while (true) delay(1000); }
  Serial.printf("Storage: %lu cards / %lu pending transactions\n",
    (unsigned long)store.cards(), (unsigned long)store.pending());
  SPI.begin(48, 47, 21, 45); // T-ETH Elite onboard W5500
  ETH.begin(ETH_PHY_W5500, 1, 45, 14, -1, SPI);
  topicIn = String("integr8/") + Config::DEVICE_ID + "/cards/receive";
  topicAck = String("integr8/") + Config::DEVICE_ID + "/transactions/acknowledge";
  topicOut = String("integr8/") + Config::DEVICE_ID + "/send";
  mqtt.setServer(Config::MQTT_HOST, Config::MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(1536);
  configTime(0, 0, "pool.ntp.org", "time.google.com"); // UTC epoch, network required
}
void loop() {
  mqtt.loop();
  reconnect();
  pollRfid();
  sendOldest();
  delay(2);
}
