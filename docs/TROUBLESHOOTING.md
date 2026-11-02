# Arduino CLI troubleshooting (Windows)

This project no longer uses PlatformIO. The historical errors involving
`collect2.exe`, `stdint.h`, `-lm` and `-lstdc++` occurred with a
PlatformIO-managed toolchain; an independent Arduino CLI installation is
intended to avoid that package state. **A successful new build has not yet
been confirmed.**

## CLI not recognised

If `arduino-cli` is not in PATH, pass `-Cli "D:\EspTools\arduino-cli.exe"`
to the PowerShell scripts or invoke it by full path.

## The disk is full during core installation

The executable is small, but core packages, downloaded archives, build
outputs and build caches can take substantial space. Set these BEFORE
installing: `ARDUINO_DIRECTORIES_DATA`, `ARDUINO_DIRECTORIES_DOWNLOADS`,
`ARDUINO_DIRECTORIES_USER`, `ARDUINO_BUILD_CACHE_PATH`
and `INTEGR8_BUILD_PATH`. See [BUILD_WINDOWS.md](BUILD_WINDOWS.md).

Check configuration using `arduino-cli config dump`. Do not assume that
moving `arduino-cli.exe` moves previously installed compiler packages.

## Compile: missing ESP32 board core or libraries

Run `.\scripts\install-dependencies.ps1` and check:
- `arduino-cli core list` reports `esp32:esp32 3.3.7`
- `arduino-cli lib list` lists PubSubClient 2.8.0 and ArduinoJson 7.4.3

## Wrong partition scheme

Confirm that `partitions.csv` exists beside the root
`Integr8ESPCardReader.ino`, and use FQBN `PartitionScheme=custom`
with `FlashSize=16M`. The firmware expects the `storage` data
partition for LittleFS. A default 4 MB scheme will not work.

Never issue an erase-all upload on a unit containing live data.

## Compiler or linker failures persist

Capture the complete Arduino CLI output (PowerShell):

~~~powershell
$fqbn = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,FlashMode=qio,CDCOnBoot=cdc"
& $env:ARDUINO_CLI compile --verbose --fqbn $fqbn --build-path $env:INTEGR8_BUILD_PATH . *> .\arduino_build.log
~~~

Provide the end of the log, especially the **first real error before**
the final build-failed line. Do not try to install unrelated libraries
for missing C/C++ standard runtime headers.

## Upload fails or COM port missing

Use a USB **data** cable and `arduino-cli board list`. Close any
serial monitor that has the COM port open. If needed, hold BOOT, briefly
press RESET, release BOOT and retry upload. Start with a reduced serial
upload speed only if the default fails.

## File system will not mount

On a factory-new board only, permit one initial format in
`include/config.h`, upload, then revert to no auto-format.
On a previously working board, **preserve the Flash** and investigate the
partition table, power supply, data corruption and mount errors.

## Important non-build limitations

The existing fixed-slot LittleFS record store and linear FIFO scans are
prototypes and can be slow. At large cardholder volumes, the current
index allocation may exhaust internal RAM unless redesigned for PSRAM.
A power loss during writes and a full unacknowledged transaction ring
also require safer recovery/overflow behaviour before field deployment.
