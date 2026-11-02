#include "../include/BleSetup.h"
#include "../include/DeviceConfig.h"
#include "../include/config.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <Preferences.h>
#include <cstring>

#if defined(CONFIG_NIMBLE_ENABLED)
#include <host/ble_store.h>
#endif

// Provisioning v1: authenticated BLE writes, a physical setup window,
// one NVS blob and an automatic reboot after saving. No secret is returned
// via a readable BLE characteristic.
//
// IMPORTANT: BLE passkey is printed over USB serial for first-time setup.
// Label each manufactured board with its individual passkey before field use.
namespace {
constexpr char SERVICE_UUID[] = "875d0000-e1a7-4dca-9a63-a18b1122c001";
constexpr char WRITE_UUID[]   = "875d0001-e1a7-4dca-9a63-a18b1122c001";
constexpr char STATUS_UUID[]  = "875d0002-e1a7-4dca-9a63-a18b1122c001";
constexpr char AUTH_UUID[]    = "875d0003-e1a7-4dca-9a63-a18b1122c001";
constexpr size_t MAX_JSON = 512;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
char receiving[MAX_JSON + 1] = {};
char candidate[MAX_JSON + 1] = {};
size_t rxLength = 0;
bool overflow = false;
bool ready = false;
bool malformed = false;
bool active = false;
bool bleInitialised = false;
BLEServer* setupServer = nullptr;
uint32_t startedAt = 0;
uint32_t restartAt = 0;
uint32_t pressAt = 0;
bool holdTriggered = false;
bool bondClearTriggered = false;
BLECharacteristic* statusChar = nullptr;

void status(const char* message) {
  if (statusChar) statusChar->setValue(String(message));
}
uint32_t setupPin() {
  Preferences p;
  if (!p.begin("i8ble", false)) return 0;
  uint32_t pin = p.getUInt("pin", 0);
  if (pin < 100000 || pin > 999999) {
    pin = 100000U + (esp_random() % 900000U);
    if (p.putUInt("pin", pin) != sizeof(uint32_t)) pin = 0;
  }
  p.end();
  return pin;
}

class WriteCallbacks final : public BLECharacteristicCallbacks {
public:
  void onWrite(BLECharacteristic* characteristic) override {
    if (!active) return;
    const uint8_t* bytes = characteristic->getData();
    const size_t count = characteristic->getLength();
    if (!bytes || count == 0) return;

    portENTER_CRITICAL(&rxMux);
    switch (bytes[0]) {
      case 1: // BEGIN
        rxLength = 0; overflow = false; ready = false; malformed = false;
        break;
      case 2: // DATA; client sends chunks <= 16 bytes
        if (count <= 1 || rxLength + count - 1 > MAX_JSON) {
          overflow = true;
        } else if (!overflow && !ready) {
          memcpy(receiving + rxLength, bytes + 1, count - 1);
          rxLength += count - 1;
        }
        break;
      case 3: // COMMIT; copy to main Arduino loop for JSON/NVS work
        if (count != 1 || overflow || rxLength == 0 || ready) {
          malformed = true;
        } else {
          memcpy(candidate, receiving, rxLength);
          candidate[rxLength] = 0;
          ready = true;
        }
        break;
      default: malformed = true; break;
    }
    portEXIT_CRITICAL(&rxMux);
  }
};

WriteCallbacks writeCallbacks;

class SetupServerCallbacks final : public BLEServerCallbacks {
public:
  void onConnect(BLEServer* server) override {
    Serial.println("[BLE] Windows/client connected. Waiting for authenticated PIN pairing.");
  }

  void onDisconnect(BLEServer* server) override {
    Serial.println("[BLE] Client disconnected. Advertising resumes while setup remains active.");
    // BLEServer handles re-advertising automatically while
    // advertiseOnDisconnect(true) is enabled. The setup timeout disables it.
  }
};
SetupServerCallbacks serverCallbacks;

class SetupSecurityCallbacks final : public BLESecurityCallbacks {
public:
#if defined(CONFIG_NIMBLE_ENABLED)
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    if (desc == nullptr) {
      Serial.println("[BLE] Authentication failed: no connection security descriptor.");
      return;
    }
    Serial.printf("[BLE] Security: encrypted=%u, MITM authenticated=%u, bonded=%u\n",
                  (unsigned int)desc->sec_state.encrypted,
                  (unsigned int)desc->sec_state.authenticated,
                  (unsigned int)desc->sec_state.bonded);
  }
#elif defined(CONFIG_BLUEDROID_ENABLED)
  void onAuthenticationComplete(esp_ble_auth_cmpl_t desc) override {
    Serial.printf("[BLE] Security: authentication %s, status=%u\n",
                  desc.success ? "completed" : "FAILED",
                  (unsigned int)desc.fail_reason);
  }
#endif
};
SetupSecurityCallbacks securityCallbacks;

// Technician recovery: 12-second BOOT hold removes BLE bonds only.
// MQTT NVS settings, the persistent PIN and the LittleFS database remain.
void clearBluetoothBonds() {
  if (!setupServer || setupServer->getConnectedCount() != 0) {
    Serial.println("[BLE] Disconnect the PC before clearing BLE pairing keys.");
    return;
  }
#if defined(CONFIG_NIMBLE_ENABLED)
  const int err = ble_store_clear();
  Serial.printf("[BLE] Clear Bluetooth pairing keys: %s (code %d).\n",
                err == 0 ? "OK" : "FAILED", err);
  if (err == 0) status("READY");
#elif defined(CONFIG_BLUEDROID_ENABLED)
  // No destructive NVS erase is attempted on unsupported Bluetooth backends.
  Serial.println("[BLE] Bond reset on Bluedroid requires platform-specific support.");
#else
  Serial.println("[BLE] Bluetooth bond reset not available in this build.");
#endif
}

bool initialise() {
  if (bleInitialised) return true;
  const uint32_t pin = setupPin();
  if (!pin) {
    Serial.println("[BLE] Cannot persist setup PIN, refusing to start.");
    return false;
  }
  const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
  char name[16] = {};
  snprintf(name, sizeof(name), "I8R-%06lX", (unsigned long)suffix);

  if (!BLEDevice::init(String(name))) return false;
  BLEDevice::setMTU(247);
  // Bool overload also enables the core's security state consistently.
  // Do NOT relax MITM: MQTT credentials require authenticated encryption.
  BLESecurity::setCapability(ESP_IO_CAP_OUT); // PIN shown over USB or label.
  BLESecurity::setPassKey(true, pin);
  BLESecurity::setAuthenticationMode(true, true, true); // bond + MITM + LE SC
  BLEDevice::setSecurityCallbacks(&securityCallbacks);

  BLEServer* server = BLEDevice::createServer();
  setupServer = server;
  server->setCallbacks(&serverCallbacks);
#if !defined(CONFIG_BT_NIMBLE_EXT_ADV) || defined(CONFIG_BLUEDROID_ENABLED)
  server->advertiseOnDisconnect(true); // Important: failed pairing must not hide the reader.
#endif
  BLEService* service = server->createService(SERVICE_UUID);
  uint32_t writeProperties = BLECharacteristic::PROPERTY_WRITE;
#if defined(CONFIG_NIMBLE_ENABLED)
  writeProperties |= BLECharacteristic::PROPERTY_WRITE_AUTHEN;
#endif
  BLECharacteristic* writeChar = service->createCharacteristic(WRITE_UUID, writeProperties);
#if defined(CONFIG_BLUEDROID_ENABLED)
  writeChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
#endif
  writeChar->setCallbacks(&writeCallbacks);
  statusChar = service->createCharacteristic(STATUS_UUID, BLECharacteristic::PROPERTY_READ);
  status("READY");

  // A public READY status does NOT prove the link is authenticated.
  // Test this characteristic before allowing settings to be sent.
  uint32_t authReadProperties = BLECharacteristic::PROPERTY_READ;
#if defined(CONFIG_NIMBLE_ENABLED)
  authReadProperties |= BLECharacteristic::PROPERTY_READ_AUTHEN;
#endif
  BLECharacteristic* authCheck = service->createCharacteristic(AUTH_UUID, authReadProperties);
#if defined(CONFIG_BLUEDROID_ENABLED)
  authCheck->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
#endif
  authCheck->setValue(String("AUTH_OK"));
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  // Put the human-readable name in the *primary* advertisement, so Windows
  // can discover the unit even if a SCAN_RSP packet is missed.
  BLEAdvertisementData advData;
  advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advData.setName(String(name));
  advertising->setAdvertisementData(advData);

  // Fit the long 128-bit service UUID into the scan response (31-byte limit).
  BLEAdvertisementData scanData;
  scanData.setCompleteServices(BLEUUID(SERVICE_UUID));
  advertising->setScanResponseData(scanData);
  advertising->setScanResponse(true);
  advertising->setMinInterval(0x50);  // 50ms (units = 0.625ms)
  advertising->setMaxInterval(0xA0);  // 100ms
  bleInitialised = true;
  Serial.printf("[BLE] Device %s / unique setup PIN %06lu\n", name, (unsigned long)pin);
  Serial.println("[BLE] Keep PIN private and record it on the device label for future service.");
  return true;
}

void start() {
  if (!initialise()) return;
  startedAt = millis();
  active = true;
#if !defined(CONFIG_BT_NIMBLE_EXT_ADV) || defined(CONFIG_BLUEDROID_ENABLED)
  if (setupServer) setupServer->advertiseOnDisconnect(true);
#endif
  BLEDevice::startAdvertising();
  restartAt = 0;
  status("READY");
  Serial.printf("[BLE] Secure setup enabled for %lu seconds.\n",
                (unsigned long)(Config::BLE_SETUP_WINDOW_MS / 1000U));
}

void handleCandidate(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonObject>()) {
    status("ERROR:JSON");
    return;
  }
  Settings::Mqtt cfg;
  cfg.host = doc["host"] | "";
  cfg.deviceId = doc["deviceId"] | "";
  cfg.username = doc["username"] | "";
  cfg.password = doc["password"] | "";
  int port = doc["port"] | 0;
  if (port < 1 || port > 65535) {
    status("ERROR:PORT");
    return;
  }
  cfg.port = static_cast<uint16_t>(port);
  if (!Settings::valid(cfg)) {
    status("ERROR:VALUES");
    return;
  }
  if (!Settings::save(cfg)) {
    status("ERROR:NVS");
    return;
  }
  status("SAVED");
  restartAt = millis();
  Serial.printf("[BLE] Saved MQTT settings for device %s; rebooting.\n", cfg.deviceId.c_str());
}
}

namespace BleSetup {
void begin(bool unconfigured) {
  pinMode(Config::BLE_SETUP_BUTTON_GPIO, INPUT_PULLUP);
  if (unconfigured) start();
}
void poll() {
  const uint32_t now = millis();
  const bool pressed = digitalRead(Config::BLE_SETUP_BUTTON_GPIO) == LOW;
  if (!pressed) {
    pressAt = 0;
    holdTriggered = false;
    bondClearTriggered = false;
  } else {
    if (pressAt == 0) pressAt = now;
    if (!holdTriggered && static_cast<uint32_t>(now - pressAt) >= Config::BLE_SETUP_HOLD_MS) {
      holdTriggered = true;
      start();
    }
    if (!bondClearTriggered && static_cast<uint32_t>(now - pressAt) >=
                               Config::BLE_BOND_CLEAR_HOLD_MS) {
      bondClearTriggered = true;
      clearBluetoothBonds();
    }
  }
  if (!active) return;

  if (restartAt) {
    if (static_cast<uint32_t>(now - restartAt) >= Config::BLE_REBOOT_DELAY_MS) ESP.restart();
    return;
  }

  char local[MAX_JSON + 1] = {};
  bool haveCandidate = false;
  bool invalidFrame = false;
  portENTER_CRITICAL(&rxMux);
  if (ready) {
    memcpy(local, candidate, sizeof(local));
    ready = false;
    haveCandidate = true;
  }
  if (malformed || overflow) {
    invalidFrame = true;
    malformed = false;
    overflow = false;
  }
  portEXIT_CRITICAL(&rxMux);
  if (invalidFrame) status("ERROR:FRAME");
  if (haveCandidate) handleCandidate(local);

  if (static_cast<uint32_t>(now - startedAt) > Config::BLE_SETUP_WINDOW_MS) {
    active = false;
#if !defined(CONFIG_BT_NIMBLE_EXT_ADV) || defined(CONFIG_BLUEDROID_ENABLED)
    if (setupServer) setupServer->advertiseOnDisconnect(false);
#endif
    BLEDevice::getAdvertising()->stop();
    status("EXPIRED");
    Serial.println("[BLE] Setup window closed.");
  }
}
}
