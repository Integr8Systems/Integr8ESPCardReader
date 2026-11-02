# Integr8 ESP Card Reader

Integr8 standalone ESP32-S3 card reader prototype for **LILYGO T-ETH-Elite**
(W5500 Ethernet, 16 MB Flash, 8 MB OPI PSRAM) and a TTL UART RFID reader.

**Build system: official Arduino CLI (no PlatformIO).**
This repository is itself an Arduino sketch. The main sketch file is
[Integr8ESPCardReader.ino](Integr8ESPCardReader.ino); the actual readable
Arduino C++ implementation is in [src/main.cpp](src/main.cpp) and
[src/storage.cpp](src/storage.cpp).

> DEVELOPMENT PROTOTYPE. The complete firmware has not yet been compiled on
> the official Arduino CLI toolchain or tested on the physical LILYGO board.
> Do not connect to a live access-controlled door. Storage performance,
> power-failure recovery, transaction overflow policy and message/topic
> compatibility still require engineering and validation.

## Getting started on Windows (PowerShell)

Detailed instructions: **[docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md)**.

Prerequisites: install the standalone [Arduino CLI](https://arduino.github.io/arduino-cli/latest/installation/)
and optionally use a non-system drive for its packages and build cache.

From the repository root:

~~~powershell
# First time only: official ESP32 core and two Arduino libraries
.\scripts\install-dependencies.ps1

# Compile (no USB connection required)
.\scripts\build.ps1

# Identify the COM port, then upload the compiled result
arduino-cli board list
.\scripts\upload.ps1 -Port COM5
~~~

If Arduino CLI isn't on your PATH, pass its location:

~~~powershell
.\scripts\install-dependencies.ps1 -Cli "D:\EspTools\arduino-cli.exe"
.\scripts\build.ps1 -Cli "D:\EspTools\arduino-cli.exe" -BuildPath "D:\EspTools\Build\Integr8ESPCardReader"
.\scripts\upload.ps1 -Cli "D:\EspTools\arduino-cli.exe" -BuildPath "D:\EspTools\Build\Integr8ESPCardReader" -Port COM5
~~~

The board FQBN is:

~~~text
esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,FlashMode=qio,CDCOnBoot=cdc
~~~

Arduino ESP32 core **3.3.7**, PubSubClient **2.8.0** and ArduinoJson
**7.4.3** are the initial tested-against source/API versions. A real Arduino
CLI board compile and upload are still pending. The sketch-root
[partitions.csv](partitions.csv) is loaded automatically by Espressif's
Arduino build recipe. Do not select a default 4 MB partition scheme.

For low disk-space setups, set the Arduino CLI data, download, user and
build-cache directories to a secondary drive BEFORE installing the core.
See the Windows guide.

## Firmware scope and limits

- Up to **50,000** cardholders, with AssetID, numeric card number,
  ValidFrom/ValidTo and enabled/suspended/blacklisted flags.
- Up to **100,000** transaction records, initially tracking AssetID,
  epoch and per-device sequence number.
- Persistent LittleFS cardholder/transaction files in the dedicated 8 MB
  **storage** partition.
- Two 3 MB OTA application partitions and about 1.94 MB reserved for future
  features such as schedules. Reserved space is *not* currently a filesystem.
- W5500 wired Ethernet and MQTT client (default port **1889**).
- Integr8 wire transformation matched to supplied C#:
  UTF-8 JSON -> uppercase hexadecimal -> reverse hex characters.
- FIFO transaction retransmission: keep sending the earliest unacknowledged
  record; advance only after an explicit *server application* acknowledgement.
  The server must deduplicate by unique device ID + transaction sequence.
- TTL UART RFID: initial ASCII-decimal-line parser, configurable baud rate.

Read [docs/PROTOCOL.md](docs/PROTOCOL.md) and
[docs/WIRING.md](docs/WIRING.md) for messages and suggested GPIO wiring.

## First boot and settings

Edit [include/config.h](include/config.h) for current development-site
MQTT details. Site-by-site BLE provisioning is a **planned** feature, not
yet implemented: see [docs/BLE_PROVISIONING.md](docs/BLE_PROVISIONING.md).

On a **brand-new blank board only**, set
`Config::ALLOW_FIRST_BOOT_FORMAT` to `true` for the first successful
boot to initialise the LittleFS partition, then return it to `false` and upload
again. **Never autoformat a deployed controller to hide a mount failure.**
An ordinary upload with the same partition map should not erase the data
partition. Avoid "Erase All Flash", changing partitions or uploading a
filesystem image on a reader that contains cardholder data.

## Source layout

| File | Purpose |
| --- | --- |
| `Integr8ESPCardReader.ino` | Arduino CLI sketch entrypoint |
| `src/main.cpp` | setup/loop, MQTT, Ethernet and UART RFID |
| `src/storage.cpp` | Flash cardholder and transaction storage |
| `include/config.h` | Site configuration (development only) |
| `include/Integr8Wire.h` | C# compatible reversed-hex codec |
| `include/UtcEpoch.h` | Validated UTC epoch conversion |
| `include/storage.h` | Record types and store API |
| `partitions.csv` | **16 MB custom** flash map |
| `scripts/*.ps1` | Arduino CLI install, build and upload |
| `tests/*_host_test.cpp` | Optional host C++ codec/date tests |

## Outstanding engineering work

This is not yet production-ready. In particular: the existing LittleFS
store uses fixed-position writes and scans up to 100,000 transactions per
send/ack. The cardholder index requires substantial memory that must be
validated against actual ESP32 internal heap/PSRAM; metadata wear levelling,
atomic writes and crash recovery are unfinished. If 100,000 log entries are
reached, an unacknowledged record can be overwritten; that overflow must
be prevented or explicitly alerted before field deployment. The current
transaction record does not include reader, outcome or reason and does not
drive a relay. MQTT topic names and the server's application ACK contract
still need testing against the Integr8 backend. The reverse-hex conversion
is not encryption.
