#pragma once
// Copy this file locally and provide values for your installation.
// Do not commit real passwords. MQTT protocol adapter is intentionally isolated.
namespace Config {
constexpr char MQTT_HOST[] = "192.168.2.8";
constexpr uint16_t MQTT_PORT = 1889;
constexpr char MQTT_USER[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char DEVICE_ID[] = "reader-001";
constexpr uint32_t MQTT_RETRY_MS = 5000;
// On a BRAND NEW board only: set true, flash once and let it initialise the
// dedicated LittleFS partition. Then set false and upload again to prevent
// accidental data loss after corruption/power failures.
constexpr bool ALLOW_FIRST_BOOT_FORMAT = false;
constexpr int RFID_RX_GPIO = 39; // ESP32 RX <- RFID TX, bare board; see docs/WIRING.md
constexpr int RFID_TX_GPIO = 42; // ESP32 TX -> RFID RX if required
constexpr uint32_t RFID_BAUD = 9600;
}
