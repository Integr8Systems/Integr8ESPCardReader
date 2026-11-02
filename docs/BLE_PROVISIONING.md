# Planned BLE on-site commissioning (design only)

The LILYGO T-ETH-Elite ESP32-S3 has integrated Bluetooth 5 LE (no Classic Bluetooth).
BLE provisioning is **not yet implemented in the current firmware**. This document
records the requirements for a future Integr8 commissioning application.

## Goal

Use a Windows or Android field app (potentially .NET MAUI, to reuse C# skills)
to configure the reader at each site without recompiling or uploading firmware.
BLE operates locally, independently of the site's Ethernet/MQTT connection.

## Initial configuration fields

- MQTT hostname / IPv4 address
- MQTT TCP port (Integr8 default: 1889; configurable)
- Unique device ID / MQTT client ID
- MQTT username and password
- Future options: MQTT TLS trust material, DHCP/static Ethernet settings,
  time-sync server, reader operating mode

## Proposed commissioning flow

1. Fresh device with no configuration enters a limited provisioning state.
   An already commissioned device must NOT advertise a configuration GATT
   service indefinitely. Enter provisioning with a deliberate physical action
   (use a suitable hardware button after validating board revision/pin access).
2. The app scans for an `Integr8 Reader` service and shows a stable,
   non-secret device identifier.
3. An installer verifies ownership using a per-device one-time activation
   credential; use authenticated/bonded BLE, not an unauthenticated GATT write.
4. Installer enters or scans site-specific parameters, sends them in bounded
   structured messages, and requests validation.
5. Firmware checks values, persists a candidate configuration, and attempts
   Ethernet + MQTT connectivity with an explicit test timeout.
6. On success atomically activate that config in NVS and close provisioning.
   On failure preserve the previous working config and report the fault.
7. Reconfiguration later requires an installer-authorised physical action.
   Audit any provisioning changes where practical.

## Persistence and safety

- Save small configuration values in ESP32 **NVS**, not in the transaction
  FIFO or cardholder file. NVS supports wear-levelling and power-loss-safe
  commit patterns suitable for occasional commissioning changes.
- Consider encrypted NVS and secure boot/flash encryption for deployed units,
  including how to provision and recover keys. These need production setup
  and lifecycle planning.
- Do not send plaintext MQTT passwords in advertisements, logs or diagnostics.
  Never rely on reverse-hex MQTT encoding for confidentiality.
- Preserve transaction and cardholder data when reconfiguring or upgrading.
- Handle failed settings updates without bricking connectivity.
- Define a versioned settings schema for future upgrades and migrations.

This is an architectural plan only, not a claim that the current firmware
already supports BLE configuration or secure credential storage.

References:
- https://wiki.lilygo.cc/products/t-eth-series/t-eth-elite/
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/overview.html
