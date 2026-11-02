# Integr8 ESP Card Reader (initial firmware)

LILYGO T-ETH-Elite ESP32-S3 + W5500 Ethernet + UART TTL RFID reader.

**Initial development implementation, not production-ready access-control firmware.** The existing Integr8 controller's reverse/hex MQTT transform has **not** been verified; transport currently uses plain JSON for integration testing only. No door unlock outputs are implemented.

## Limits and Flash layout
- **50,000** fixed-size cardholder records (25 B each = 1,250,000 bytes).
- **100,000** FIFO transaction records (21 B each = 2,100,000 bytes).
- 16 MiB Flash: dual 3 MiB OTA app slots, 8 MiB LittleFS, 1.9375 MiB partition reserved for future functionality.
- Transactions include 64-bit monotonic sequence, AssetID, epoch, state, and integrity checksum. Stops at first pending record until **application-level ACK**, retries at 5 second intervals; duplicates must be handled on the server by device ID + sequence.
- Upon exhaustion of the transaction ring, the oldest slot is overwritten, including a pending record if the network has been offline too long. Lost unacknowledged records must be counted/alerted in a future update.

## Build
Install PlatformIO in VS Code, configure `include/config.h`, and build/upload the `t_eth_elite` environment. The UART reader uses GPIO39 RX and optional GPIO42 TX. See [wiring](docs/WIRING.md). Validate the exact board revision and core/library versions before connecting hardware.

## MQTT development JSON (not yet Integr8 wire-compatible)
- Subscribe: `integr8/<DEVICE_ID>/cards/receive`
- Subscribe: `integr8/<DEVICE_ID>/transactions/acknowledge`
- Publish: `integr8/<DEVICE_ID>/send`
- Upsert card command: `{"Command":"CardUpsert","MessageID":"abc","AssetID":123,"CardNumber":"123456789","ValidFromEpoch":1761955200,"ValidToEpoch":2524607999,"Enabled":true,"Suspended":false,"Blacklisted":false}`
- Delete card: `{"Command":"CardDelete","MessageID":"abc","AssetID":123}`
- Transaction: `{"Sequence":"1","AssetID":123,"Epoch":1793617200}`
- Server ACK (only *after durable server-side transaction commit*): `{"Sequence":"1","Accepted":true}`

MQTT QoS or broker PUBACK is **not** sufficient to delete queued transactions; only the explicit application ACK does that. The same device ID must remain stable across reboots. Transport must be secured before production (broker auth + ACL, protected network and ideally TLS via a tested W5500-capable network stack).

## Known limitations / next engineering tasks
1. Adapt exact Integr8 reverse/hex encoding and real MQTT topic/payload conventions using your controller source; no guessed protocol is shipped.
2. Source TTL RFID model and confirm UART framing, supply voltage, GPIO header numbering and whether GPS expansion is attached.
3. Run hardware build and verify Ethernet API compatibility; not compiled/flashed yet.
4. **Storage write performance**: current prototype uses LittleFS fixed-position file writes and full scans (including 100k transaction scan per send/ACK). Optimise with in-memory indexes, persistent cursors, batched flushes, erase-aware journalling and crash fault injection before field use.
5. An interrupted cardholder update can corrupt that individual record; introduce copy-on-write transactions to guarantee update atomicity.
6. Add UTC clock sync validation, metrics, transaction overflow alerts, replay/idempotency tests, and production key handling.
7. Card scans currently log known/unknown AssetIDs but do not record access result. Expand transaction format before using as an audit log.
8. Rework card storage/index RAM budget after profiling heap and PSRAM on the actual hardware.
9. Switch to properly validated, authenticated MQTT transport, and ensure duplicate commands cannot cause unwanted state changes.
