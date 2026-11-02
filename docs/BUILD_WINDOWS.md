# Build & flash from Windows 10/11 (recommended: VS Code + PlatformIO)

The code is **Arduino framework C++**: `src/main.cpp` defines the normal `setup()` and `loop()` functions, just like a single `.ino` sketch. PlatformIO compiles the `.cpp` files directly and pulls in the dependencies automatically.

1. Install [Visual Studio Code](https://code.visualstudio.com/) and [Git for Windows](https://git-scm.com/download/win). Install [Python](https://www.python.org/downloads/) if PlatformIO requests it.
2. Open VS Code > Extensions (`Ctrl+Shift+X`) > install **PlatformIO IDE** (PlatformIO). Restart VS Code.
3. Open a VS Code Terminal and run:
   ```powershell
   git clone https://github.com/Integr8Systems/Integr8ESPCardReader.git
   cd Integr8ESPCardReader
   code .
   ```
   (If Git clone is blocked, download the repository ZIP from GitHub and extract it.)
4. Confirm VS Code opened the **repository root** where `platformio.ini` is visible, not `src` alone. Let PlatformIO download the pinned ESP32 Arduino core and libraries on first build.
5. Edit `include/config.h`: MQTT broker IP/hostname, port, DEVICE_ID, user/password, RFID baud and GPIOs. Do NOT commit actual production credentials.
6. **New blank board provisioning:** temporarily set `ALLOW_FIRST_BOOT_FORMAT = true` in `include/config.h`. On a board with existing data, leave it false. The first mount can then format only the dedicated `storage` partition. After successful first boot, set it back to false and upload again.
7. Connect the T-ETH-Elite via a **data-capable USB-C cable**. Verify that Windows Device Manager shows the board's COM port. No separate Ethernet connection is needed for programming.
8. Click the PlatformIO **checkmark (Build)** at the bottom of VS Code. Once compilation succeeds click the **right arrow (Upload)**.
9. Open **PlatformIO: Monitor** at **115200 baud**. A successful start prints the storage counts; connect Ethernet to test MQTT.
10. If Upload cannot enter bootloader, hold **BOOT**, briefly press **RESET**, release **BOOT**, then start Upload. If necessary set `upload_port = COMx` in `platformio.ini`; lower `upload_speed = 115200` if high-speed transfers fail.
11. For later firmware uploads, **disable erase-all**: the same partition layout must be retained so cardholder/transaction data remains present. Changing partition layout or flashing a filesystem image can destroy the stored data. Back up before any layout changes.

## What's in the project

| Path | Purpose |
| --- | --- |
| `platformio.ini` | Selects ESP32-S3 Arduino build, dependency versions and flash options |
| `partitions.csv` | OTA apps, 8 MiB LittleFS, future reserve |
| `src/main.cpp` | Arduino `setup()`/`loop()`, Ethernet, RFID and MQTT |
| `src/storage.cpp`, `include/storage.h` | Cardholder storage and FIFO transaction record store |
| `include/Integr8Wire.h` | Byte-for-byte Integr8 reverse-hex MQTT codec |
| `include/config.h` | MQTT, UART, and controlled first-time formatting settings |
| `docs/PROTOCOL.md` | C# interoperability, topic and acknowledgement details |
| `docs/WIRING.md` | TTL reader wiring, voltages and GPIO precautions |

## Arduino IDE versus PlatformIO

Arduino IDE can compile `.cpp` in a sketch's `src` directory, but **this repository is currently organised as a PlatformIO project** and includes a custom partition table that needs careful handling. Do not simply open `src/main.cpp` as an `.ino`; that loses the dependency and partition configuration.

If you prefer the Arduino IDE UI, we can organise an Arduino sketch target in a later change. LILYGO's recommended Arduino board selection is **ESP32S3 Dev Module**, Flash **16MB**, PSRAM **OPI**, with **Espressif ESP32 Arduino 3.x** support. The custom 8 MiB partition file must be included in that sketch folder; selecting the Arduino IDE's default partition scheme will change available storage.

Official board documentation: https://wiki.lilygo.cc/products/t-eth-series/t-eth-elite/

## Troubleshooting

- No COM port: check USB data cable, Windows Device Manager, and BOOT/RESET sequence.
- Build errors referencing `NetworkClient` or `ETH_PHY_W5500`: use the pinned pioarduino Arduino **3.x** platform in `platformio.ini`, not older Arduino 2.x.
- `Storage unavailable, halt`: on factory-new board use one-time `ALLOW_FIRST_BOOT_FORMAT=true`, then immediately revert. On a unit containing data, **never** format to hide a mount problem.
- MQTT disconnected: Ethernet link, DHCP, broker address, broker port (default **1889**) and credentials; capture payload as reversed-hex ASCII, not plain JSON.
- If serial output is blank, check port and monitor speed, press RESET, and verify the board's USB OTG switch mode. USB CDC settings vary by board/core revision.
- Current code is an integration prototype: test storage crash recovery and MQTT retries, especially after a power loss, before live access-control use.


## Compiler installation errors
If the build stops in the compiler's `stdint.h` header, follow [Windows PlatformIO troubleshooting](TROUBLESHOOTING.md). This is usually a corrupted downloaded toolchain, not a firmware source or library dependency.
