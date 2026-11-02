# Arduino CLI on Windows (LILYGO T-ETH-Elite ESP32-S3)

**No Arduino IDE, Espressif IDE, or PlatformIO required.** The full firmware
is Arduino-framework C++, kept in separate source files. Arduino CLI compiles
the root `Integr8ESPCardReader.ino` and recursively compiles `src/*.cpp`.

## 1. Install CLI and choose a drive

Download the standalone Windows Arduino CLI executable:

https://arduino.github.io/arduino-cli/latest/installation/

Put the executable in a folder on a drive with sufficient space, for example
`D:\EspTools\arduino-cli.exe`. **The executable itself is small, but the
ESP32 board support and prebuilt compiler libraries are much larger**.

To keep ALL Arduino CLI packages and caches on D:, start a new PowerShell
session and set these BEFORE installing any cores or libraries:

~~~powershell
$env:ARDUINO_DIRECTORIES_DATA = "D:\EspTools\ArduinoData"
$env:ARDUINO_DIRECTORIES_DOWNLOADS = "D:\EspTools\Downloads"
$env:ARDUINO_DIRECTORIES_USER = "D:\EspTools\Sketchbook"
$env:ARDUINO_BUILD_CACHE_PATH = "D:\EspTools\Cache"
$env:INTEGR8_BUILD_PATH = "D:\EspTools\Build\Integr8ESPCardReader"
$env:ARDUINO_CLI = "D:\EspTools\arduino-cli.exe"
~~~

These variables apply to the **current** PowerShell session; set them again
in a new window or configure them persistently. If you already installed
the core on C:, changing the variables does NOT automatically migrate it.
Inspect where your existing installation lives using
`& $env:ARDUINO_CLI config dump`.

If CLI is installed in PATH you can use `arduino-cli` in place of
`& $env:ARDUINO_CLI`.

## 2. Install board support and libraries (once)

Open PowerShell in `C:\workspace\Integr8ESPCardReader`.
Run the provided setup script:

~~~powershell
.\scripts\install-dependencies.ps1 -Cli $env:ARDUINO_CLI
~~~

This installs:
- Official **esp32:esp32@3.3.7** platform from Espressif's Boards Manager
- **PubSubClient@2.8.0**
- **ArduinoJson@7.4.3**

The equivalent direct CLI commands are:

~~~powershell
$index = "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
& $env:ARDUINO_CLI core update-index --additional-urls $index
& $env:ARDUINO_CLI core install esp32:esp32@3.3.7 --additional-urls $index
& $env:ARDUINO_CLI lib install PubSubClient@2.8.0
& $env:ARDUINO_CLI lib install ArduinoJson@7.4.3
~~~

Check the installation with:

~~~powershell
& $env:ARDUINO_CLI core list
& $env:ARDUINO_CLI lib list
~~~

## 3. Configure the firmware

For this BLE-enabled revision, MQTT host/port/user/password and device ID are provisioned over Bluetooth and stored in NVS (not hardcoded). Use `include/config.h` only for hardware/timing defaults. See [BLE commissioning](BLE_PROVISIONING.md).

Custom 16 MB flash mapping is defined by the **sketch-root**
`partitions.csv`; DO NOT move it into `src`. The Espressif Arduino
core's Windows prebuild hook copies this file into the build directory.

## 4. Compile

~~~powershell
.\scripts\build.ps1 -Cli $env:ARDUINO_CLI
~~~

Equivalent direct command:

~~~powershell
$fqbn = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,FlashMode=qio,CDCOnBoot=cdc"
& $env:ARDUINO_CLI compile --fqbn $fqbn --build-path $env:INTEGR8_BUILD_PATH .
~~~

Board options:
- Flash: **16 MB QIO**
- PSRAM: **8 MB OPI**
- Partitions: **Custom** (`partitions.csv`)
- USB CDC on boot: Enabled

If you do not set the build path, `build.ps1` defaults to a
folder under the Arduino data directory when available, otherwise
`.arduino-build` in the repository. Set `INTEGR8_BUILD_PATH` or
pass `-BuildPath` to keep builds off C:.

The final command must complete successfully before uploading. We have
**not yet verified this project through a real Arduino CLI build**.

## 5. USB upload

Attach the LILYGO board using a **USB data** cable. Identify its COM port:

~~~powershell
& $env:ARDUINO_CLI board list
~~~

Then (replace COM5 with the detected port):

~~~powershell
.\scripts\upload.ps1 -Port COM5 -Cli $env:ARDUINO_CLI
~~~

Equivalent Arduino CLI command:

~~~powershell
& $env:ARDUINO_CLI upload --port COM5 --fqbn $fqbn --build-path $env:INTEGR8_BUILD_PATH .
~~~

The upload command uses the binaries from the compile step; it does NOT
compile on its own. Do not enable "Erase All Flash" or change the partition
table on an already commissioned reader.

If the board won't enter upload mode, hold BOOT, press RESET briefly,
release BOOT, then retry. You may also have to close the serial monitor.

## 6. Serial monitor

~~~powershell
& $env:ARDUINO_CLI monitor -p COM5 -b esp32:esp32:esp32s3 -c baudrate=115200
~~~

After programming, the serial output should show storage mount status.
Initial card and transaction file provisioning will involve substantial
flash writes and is **not** yet benchmarked. Wire the Ethernet cable and
verify that W5500 networking and MQTT connect before proceeding to RFID.

If you see "Storage unavailable" on a **factory-new** board, set
`Config::ALLOW_FIRST_BOOT_FORMAT=true` for initial provisioning,
upload once, and revert to `false` immediately after success.
Never format a board with real data to work around a mount failure.

## 7. Source layout and supported framework

The root Arduino sketch is required only to satisfy the Arduino sketch
layout. Functions `setup()` and `loop()` are deliberately defined in
`src/main.cpp`, not duplicated in the `.ino` file.

Our C++ sources include the headers from `include/` using
relative `../include/...` paths. This keeps site settings in their
original location, avoiding unnecessary local configuration changes.

The official Arduino ESP32 **3.3.7** core provides Ethernet (W5500 SPI),
LittleFS, serial UART and ArduinoJson/PubSubClient dependencies. Do not
install a second Arduino ESP32 core on top of the existing one solely to
try to fix a broken PlatformIO cache.

References:
- https://docs.arduino.cc/arduino-cli/sketch-specification/
- https://docs.arduino.cc/arduino-cli/commands-reference/arduino-cli_compile
- https://docs.arduino.cc/arduino-cli/commands-reference/arduino-cli_upload
- https://docs.arduino.cc/arduino-cli/configuration
- https://github.com/espressif/arduino-esp32/blob/3.3.7/platform.txt
\n## Configure MQTT after upload over Bluetooth\n\nInstall the lightweight PC dependency once: `py -m pip install bleak`. Then run `py tools/integr8_reader_setup.py` from the repository root. An unconfigured reader advertises as `I8R-XXXXXX` for 180 seconds. On configured readers, hold BOOT for 4 seconds after normal startup to reopen the setup window. Pair using the per-reader PIN printed on the USB serial monitor, enter the site's MQTT details, save, then verify the 30-second MQTT heartbeat after reboot. [Full instructions](BLE_PROVISIONING.md).\n
## Bluetooth pairing on Windows

The current Windows utility uses WinRT custom PIN pairing (not Bleak's
Just Works pairing method). Update Bleak using `py -m pip install -U bleak`,
then run `py tools/integr8_reader_setup.py`. Reopen the reader's ten-minute
setup window, Scan, select `I8R-XXXXXX`, enter the device's six-digit
PIN, click **Pair & Test Bluetooth**, and only then use **Save MQTT settings**.
See [BLE troubleshooting](BLE_PROVISIONING.md#troubleshooting-unreliable-discovery--pin-pairing-on-windows).
