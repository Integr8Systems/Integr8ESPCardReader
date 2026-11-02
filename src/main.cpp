#include <Arduino.h>
#include <ArduinoJson.h>
#include <ETH.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <time.h>
#include "../include/config.h"
#include "../include/storage.h"
#include "../include/Integr8Wire.h"
#include "../include/UtcEpoch.h"
#include "../include/DeviceConfig.h"
#include "../include/BleSetup.h"
#include <string>

// Encodes every outbound MQTT JSON payload and decodes every inbound payload
// using the exact UTF8 -> uppercase HEX -> reverse-chars Integr8 wire format.
Store store;
HardwareSerial rfid(1);
NetworkClient network;
PubSubClient mqtt(network);
String topicIn, topicAck, topicOut;
unsigned long lastConnect = 0, lastPublish = 0;
String rfidBuffer;
uint64_t inFlight = 0;
Settings::Mqtt mqttSettings;
bool mqttConfigured = false;
uint32_t lastHeartbeat = 0;

// PubSubClient is limited by its configured packet buffer. Keep a bounded input.
constexpr size_t MAX_WIRE_MESSAGE = 2048;

bool publishJson(const String& topic, JsonDocument& document) {
  String json;
  serializeJson(document, json);
  std::string wire;
  if (!Integr8Wire::encode(reinterpret_cast<const uint8_t*>(json.c_str()),
                           json.length(), wire)) return false;
  if (wire.size() > MAX_WIRE_MESSAGE) return false;
  return mqtt.publish(topic.c_str(),
                      reinterpret_cast<const uint8_t*>(wire.data()),
                      static_cast<unsigned int>(wire.size()), false);
}

uint64_t jsonU64(JsonVariantConst value) {
  String s = value.as<String>();
  if (!s.length()) return 0;
  for (char c : s) if (c < '0' || c > '9') return 0;
  return strtoull(s.c_str(), nullptr, 10);
}
// Accept date strings from C# as UTC yyyy-MM-dd HH:mm:ss, or epoch seconds.
// Portable conversion, independent of the host's timezone and libc timegm().
bool parseUtcEpoch(JsonVariantConst value, uint32_t& result) {
  if (value.is<uint32_t>()) {
    result = value.as<uint32_t>();
    return true;
  }
  String input = value.as<String>();
  if (input.length() == 19) {
    return Integr8Time::parseUtcDateTime(input.c_str(), input.length(), result);
  }
  return Integr8Time::parseEpochDigits(input.c_str(), input.length(), result);
}
bool validClock(uint32_t& epoch) {
  time_t now = time(nullptr);
  if (now < 1760000000LL || now > UINT32_MAX) return false;
  epoch = static_cast<uint32_t>(now);
  return true;
}
void onMqtt(char* topic, uint8_t* payload, unsigned int length) {
  if (length > MAX_WIRE_MESSAGE) return;
  std::string json;
  if (!Integr8Wire::decode(payload, length, json)) {
    Serial.println("[MQTT] Rejected invalid reversed-hex payload");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json.data(), json.size())) {
    Serial.println("[MQTT] Rejected malformed JSON payload");
    return;
  }
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
  if (!publishJson(topicOut, response)) Serial.println("[MQTT] Card response publish failed");
}
void reconnect() {
  if (!mqttConfigured || mqtt.connected() || millis() - lastConnect < Config::MQTT_RETRY_MS) return;
  lastConnect = millis();
  if (mqtt.connect(mqttSettings.deviceId.c_str(), mqttSettings.username.c_str(), mqttSettings.password.c_str())) {
    mqtt.subscribe(topicIn.c_str(), 1);
    mqtt.subscribe(topicAck.c_str(), 1);
    inFlight = 0; // Resend oldest pending after reconnect.
    lastHeartbeat = millis() - Config::HEARTBEAT_MS; // Send first heartbeat promptly after MQTT connection.
    Serial.printf("[MQTT] Connected: %s:%u as %s\n", mqttSettings.host.c_str(), mqttSettings.port, mqttSettings.deviceId.c_str());
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
  if (!mqtt.connected() || !store.pending() || millis() - lastPublish < Config::MQTT_RETRY_MS) return;
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
  if (publishJson(topicOut, doc)) lastPublish = millis();
}
void sendHeartbeat() {
  if (!mqtt.connected() || static_cast<uint32_t>(millis() - lastHeartbeat) < Config::HEARTBEAT_MS) return;
  JsonDocument heartbeat;
  heartbeat["Command"] = "Heartbeat";
  heartbeat["DeviceID"] = mqttSettings.deviceId;
  uint32_t epoch = 0;
  validClock(epoch);
  heartbeat["Epoch"] = epoch;  // 0 if NTP has not synchronised yet.
  heartbeat["UptimeSeconds"] = millis() / 1000UL;
  heartbeat["IPAddress"] = ETH.localIP().toString();
  heartbeat["Cardholders"] = store.cards();
  heartbeat["PendingTransactions"] = store.pending();
  heartbeat["FreeHeapBytes"] = ESP.getFreeHeap();
  if (publishJson(topicOut, heartbeat)) {
    lastHeartbeat = millis();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(Config::BLE_SETUP_BUTTON_GPIO, INPUT_PULLUP);
  rfid.begin(Config::RFID_BAUD, SERIAL_8N1, Config::RFID_RX_GPIO, Config::RFID_TX_GPIO);

  mqttConfigured = Settings::load(mqttSettings);
  BleSetup::begin(!mqttConfigured);

  if (!store.begin()) {
    Serial.println("[Storage] Unavailable: no transactions will be recorded.");
    Serial.println("[Storage] Set ALLOW_FIRST_BOOT_FORMAT only on a new blank board.");
    while (true) {
      BleSetup::poll();
      delay(20);
    }
  }

  Serial.printf("[Storage] Cards: %lu, pending transactions: %lu\n",
                (unsigned long)store.cards(), (unsigned long)store.pending());
  Serial.printf("[Hardware] Flash: %lu, PSRAM: %lu\n",
                (unsigned long)ESP.getFlashChipSize(), (unsigned long)ESP.getPsramSize());

  SPI.begin(48, 47, 21, 45); // T-ETH Elite onboard W5500
  if (!ETH.begin(ETH_PHY_W5500, 1, 45, 14, -1, SPI)) {
    Serial.println("[Ethernet] W5500 initialisation failed");
  }

  if (mqttConfigured) {
    topicIn = String("integr8/") + mqttSettings.deviceId + "/cards/receive";
    topicAck = String("integr8/") + mqttSettings.deviceId + "/transactions/acknowledge";
    topicOut = String("integr8/") + mqttSettings.deviceId + "/send";
    mqtt.setServer(mqttSettings.host.c_str(), mqttSettings.port);
    Serial.printf("[MQTT] Server: %s:%u, device: %s\n",
                  mqttSettings.host.c_str(), mqttSettings.port, mqttSettings.deviceId.c_str());
  } else {
    Serial.println("[MQTT] Not configured. Pair over Bluetooth to enter site settings.");
  }

  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(4096);
  configTime(0, 0, "pool.ntp.org", "time.google.com"); // UTC epoch
}

void loop() {
  BleSetup::poll();
  mqtt.loop();
  reconnect();
  pollRfid();
  sendOldest();     // Keep queued transactions ahead of diagnostics.
  sendHeartbeat();  // Independent of transaction acknowledgements.
  delay(2);
}
