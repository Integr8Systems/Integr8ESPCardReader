# Integr8 ESP Card Reader (initial firmware)

LILYGO T-ETH-Elite ESP32-S3 + W5500 Ethernet + UART TTL RFID reader.

**Initial development implementation, not production-ready access-control firmware.** MQTT payload encoding now matches the supplied C# reverse/hex character transform (see [protocol](docs/PROTOCOL.md)). Message topic/schema interoperability with the actual server is still to be integrated/tested. No door unlock outputs are implemented.

## Limits and Flash layout
- **50,000** fixed-size cardholder records (25 B each = 1,250,000 bytes).
- **100,000** FIFO transaction records (21 B each = 2,100,000 bytes).
- 16 MiB Flash: dual 3 MiB OTA app slots, 8 MiB LittleFS, 1.9375 MiB partition reserved for future functionality.
- Transactions include 64-bit monotonic sequence, AssetID, epoch, state, and integrity checksum. Stops at first pending record until **application-level ACK**, retries at 5 second intervals; duplicates must be handled on the server by device ID + sequence.
- Upon exhaustion of the transaction ring, the oldest slot is overwritten, including a pending record if the network has been offline too long. Lost unacknowledged records must be counted/alerted in a future update.

## Build
Follow the [Windows build-and-upload walkthrough](docs/BUILD_WINDOWS.md). This is **Arduino framework C++**, built with VS Code + PlatformIO rather than an `.ino` sketch. Configure `include/config.h`, select `t_eth_elite`, and provision the LittleFS partition once on a factory-new board before use. UART reader: GPIO39 RX / optional GPIO42 TX. See [wiring](docs/WIRING.md).

## MQTT JSON message bodies (carried as Integr8 reversed-hex ASCII; dates interpreted as UTC)
- Subscribe: `integr8/<DEVICE_ID>/cards/receive`
- Subscribe: `integr8/<DEVICE_ID>/transactions/acknowledge`
- Publish: `integr8/<DEVICE_ID>/send`
- Upsert card command: `{"Command":"CardUpsert","MessageID":"abc","AssetID":123,"CardNumber":"123456789","ValidFrom":"2026-11-01 00:00:00","ValidTo":"2050-01-01 23:59:59","Enabled":true,"Suspended":false,"Blacklisted":false}`
- Delete card: `{"Command":"CardDelete","MessageID":"abc","AssetID":123}`
- Transaction: `{"Sequence":"1","AssetID":123,"Epoch":1793617200}`
- Server ACK (only *after durable server-side transaction commit*): `{"Sequence":"1","Accepted":true}`

MQTT QoS or broker PUBACK is **not** sufficient to delete queued transactions; only the explicit application ACK does that. The same device ID must remain stable across reboots. Transport must be secured before production (broker auth + ACL, protected network and ideally TLS via a tested W5500-capable network stack).

## Known limitations / next engineering tasks
1. Verify actual Integr8 MQTT topics/message field schema with the deployed server; the byte-level reverse-hex codec now matches the supplied C# code.
2. Source TTL RFID model and confirm UART framing, supply voltage, GPIO header numbering and whether GPS expansion is attached.
3. Run hardware build and verify Ethernet API compatibility; not compiled/flashed yet.
4. **Storage write performance**: current prototype uses LittleFS fixed-position file writes and full scans (including 100k transaction scan per send/ACK). Optimise with in-memory indexes, persistent cursors, batched flushes, erase-aware journalling and crash fault injection before field use.
5. An interrupted cardholder update can corrupt that individual record; introduce copy-on-write transactions to guarantee update atomicity.
6. Add UTC clock sync validation, metrics, transaction overflow alerts, replay/idempotency tests, and production key handling.
7. Card scans currently log known/unknown AssetIDs but do not record access result. Expand transaction format before using as an audit log.
8. Rework card storage/index RAM budget after profiling heap and PSRAM on the actual hardware.
9. Switch to properly validated, authenticated MQTT transport, and ensure duplicate commands cannot cause unwanted state changes.

## Future site commissioning

The ESP32-S3 includes Bluetooth 5 LE. An Integr8 BLE provisioning app is planned to set MQTT broker/port/device ID/credentials on-site without reflashing. **Not implemented yet.** See [BLE commissioning design](docs/BLE_PROVISIONING.md).

For compiler package errors such as missing standard `stdint.h`, see [Windows build troubleshooting](docs/TROUBLESHOOTING.md).

Date conversion: `include/UtcEpoch.h` provides validated, timezone-independent UTC epoch conversion without `timegm()`. Host test: `tests/utc_epoch_host_test.cpp`.
