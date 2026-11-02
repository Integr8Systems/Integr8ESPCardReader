#pragma once
#include <Arduino.h>

// MQTT broker, device ID and credentials are now stored in ESP32 NVS and
// provisioned over authenticated BLE. No per-site firmware builds required.
namespace Config {
constexpr uint16_t DEFAULT_MQTT_PORT = 1889;
constexpr uint32_t MQTT_RETRY_MS = 5000;
constexpr uint32_t HEARTBEAT_MS = 30000;

// Hold the board BOOT button (GPIO0) for 4 seconds AFTER boot to enable
// a 10 minute on-site setup window. Never reboot with BOOT held down.
constexpr int BLE_SETUP_BUTTON_GPIO = 0;
constexpr uint32_t BLE_SETUP_HOLD_MS = 4000;
constexpr uint32_t BLE_BOND_CLEAR_HOLD_MS = 12000; // physical BOOT, BLE keys only
constexpr uint32_t BLE_SETUP_WINDOW_MS = 600000; // 10 minutes for Windows PIN pairing
constexpr uint32_t BLE_REBOOT_DELAY_MS = 6000;

// BRAND-NEW FLASH ONLY: change to true for initial LittleFS format,
// then restore false and reflash. Never enable for existing site devices.
constexpr bool ALLOW_FIRST_BOOT_FORMAT = false;

constexpr int RFID_RX_GPIO = 39;
constexpr int RFID_TX_GPIO = 42;
constexpr uint32_t RFID_BAUD = 9600;
}
