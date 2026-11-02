# PlatformIO build troubleshooting (Windows)

## Linker/compiler failures: matched pioarduino toolchain

### Symptoms

The initial pioarduino 55.03.39 / Arduino ESP32 3.3.9 build compiled the
firmware, but the link failed:

```text
ld.exe: cannot find -lm: No such file or directory
ld.exe: cannot find -lstdc++: No such file or directory
collect2.exe: error: ld returned 1 exit status
```

A subsequent override of **only** the compiler package introduced a
different error:

```text
'xtensa-esp32s3-elf-g++' is not recognized as an internal or external command
```

This indicated that the override package failed to provide/expose the
expected compiler executable. Its diagnostic command also mistakenly assumed
the generic prefix `xtensa-esp-elf-g++`, whereas PlatformIO compiles this
ESP32-S3 target with `xtensa-esp32s3-elf-g++`.

### Repository configuration change

We now pin the **complete pioarduino 55.03.37 platform**, not a single custom
compiler archive. This platform release includes its matched Arduino 3.3.7
core, ESP-IDF support libraries and `toolchain-xtensa-esp-elf`
14.2.0+20251107. The N16R8 16MB Flash / 8MB OPI PSRAM board profile remains
unchanged.

**This is a candidate fix based on the published matching package versions;
a complete Windows build and device upload have not yet been verified.**

### Run from Windows PowerShell

```powershell
cd C:\workspace\Integr8ESPCardReader
git pull origin main

& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e t_eth_elite -t clean
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e t_eth_elite
```

The PACKAGES section should report `framework-arduinoespressif32 @ 3.3.7`,
not 3.3.9. Compiler and support libraries should come from the same platform
release.

### Correct compiler diagnostic

First locate the actual compiler installed by PlatformIO. Do not assume a
compiler path until it exists:

```powershell
$bin = Join-Path $env:USERPROFILE ".platformio\packages\toolchain-xtensa-esp-elf\bin"
Get-ChildItem -LiteralPath $bin -Filter "*g++.exe" | Select-Object Name,FullName
```

For the ESP32-S3, try this **only if the file exists**:

```powershell
$compiler = Join-Path $bin "xtensa-esp32s3-elf-g++.exe"
Test-Path -LiteralPath $compiler
& $compiler -print-file-name=libm.a
& $compiler -print-file-name=libstdc++.a
```

Each `-print-file-name` call should return a full path to the compiler's
target runtime archive. A bare `libm.a` or `libstdc++.a` means that
archive was not found. If `Test-Path` returns `False`, inspect the
`Get-ChildItem` results and package installation; do not run the missing
executable.

Use each PowerShell command on its own line. `&` is the invocation operator
at the start of an individual command; it is **not** a shell command
separator as in some other terminals.

### Preserve the entire diagnostic build log

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e t_eth_elite -v *> .\build_verbose.log
```

If the error persists after the complete platform switch, share the log;
it will show whether compiler discovery and linking are now consistent.
Do not install unrelated PlatformIO libraries as substitutes for the
standard C/C++ runtime, and do not flash until the firmware links.

Platform manifests:
- https://github.com/pioarduino/platform-espressif32/blob/55.03.37/platform.json
- https://github.com/pioarduino/platform-espressif32/blob/55.03.39/platform.json

## Error: `fatal error: stdint.h: No such file or directory`

If the log contains:

```text
.../toolchain-xtensa-esp-elf/lib/gcc/xtensa-esp-elf/14.2.0/include/stdint.h:9
# include_next <stdint.h>
fatal error: stdint.h: No such file or directory
```

this is a **standard C compiler header**, not a missing Arduino or project
library. The build can fail while compiling dependencies such as
`PubSubClient.cpp` and `NetworkClient.cpp` before compiling our firmware.

An incomplete or corrupted PlatformIO toolchain installation is a common
cause. Repair the local dependency installation first:

1. Close VS Code and all PlatformIO processes.
2. Open Windows PowerShell (normal user privileges).
3. Remove the downloaded compiler package:

```powershell
Remove-Item -LiteralPath "$env:USERPROFILE\.platformio\packages\toolchain-xtensa-esp-elf" -Recurse -Force
```

4. Reopen VS Code, open the repository, and click PlatformIO **Build**.
   PlatformIO downloads a fresh compiler package as needed.

If still failing, verify the reinstall completed, check whether antivirus
quarantined package files, and inspect verbose compiler include paths.
Only then consider reinstalling the downloaded Arduino framework packages:

```powershell
Remove-Item -LiteralPath "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32" -Recurse -Force
Remove-Item -LiteralPath "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32-libs" -Recurse -Force
```

Then rebuild to re-download. These commands remove cached tools/frameworks,
**not** the local repository source files. Do not search for a replacement
`stdint.h` package in the PlatformIO library registry.

Relevant confirmed PlatformIO community fix:
https://community.platformio.org/t/im-unable-to-compile/41276

## Board summary says '8MB Flash, No PSRAM'

The old generic `esp32-s3-devkitc-1` profile displayed 8MB/no PSRAM even
though T-ETH-Elite hardware actually has **16 MB Flash and 8 MB OPI PSRAM**.
We now use `board = esp32-s3-devkitc1-n16r8` from the pinned pioarduino 55.03.37 platform. This board profile explicitly matches the physical
Flash size and PSRAM type. It corrects board identification; it does NOT
by itself repair unrelated missing C/C++ linker symbols.

Before field use, check actual Flash size, PSRAM detection and partition
layout on the hardware. Repartitioning an already commissioned device
can permanently erase cardholder and transaction data.

Manufacturer:
https://wiki.lilygo.cc/products/t-eth-series/t-eth-elite/

## Compiler fix: `timegm` was not declared

Some ESP32 Arduino/newlib toolchains do not expose `timegm()`. The firmware now
uses a pure, timezone-independent calendar-to-epoch converter in
`include/UtcEpoch.h`. It supports the C# date format
`yyyy-MM-dd HH:mm:ss` interpreted as UTC, and optionally epoch strings.
It validates leap years, invalid dates and the `uint32_t` 2106 cutoff.

Fetch the latest source and build again:

```powershell
git pull origin main
```

In VS Code, press PlatformIO **Build**. This fixes this source-level error,
but the complete ESP32 firmware still requires a successful board build/upload
and hardware testing.

## Linker failure: `collect2.exe: error: ld returned 1 exit status`

Example of the actual underlying errors in a November 2026 build:

```text
undefined reference to `__errno'
undefined reference to `time'
undefined reference to `puts'
undefined reference to `operator delete(void*, unsigned int)'
undefined reference to `__gxx_personality_v0'
```

These are missing **C/C++ runtime symbols across the Arduino core and
precompiled ESP-IDF libraries**, not undefined functions from our application.
They indicate the runtime archives weren't resolved at link time. Possible
causes: an incomplete/corrupted toolchain or framework-libs installation, an
incompatible package set, or a linker command assembled with missing library
arguments. Do not add dummy replacements for `__errno`, `time`, etc.

### Step 1: pull profile correction and clean/rebuild

```powershell
cd C:\workspace\Integr8ESPCardReader
git pull origin main
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e t_eth_elite -t clean
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e t_eth_elite
```

The PlatformIO summary should now identify `N16R8` (16 MB Flash, 8 MB
PSRAM). The board correction is independent of the linker fix.

### Step 2: rebuild with a fresh set of downloaded runtime packages (only if needed)

The earlier `stdint.h` failure was repaired by re-downloading the Xtensa
compiler, but its standard libraries may still be missing or incompatible
with a cached framework-libs package. Close VS Code and all PlatformIO
processes first, then in PowerShell:

```powershell
# Run inside the Integr8ESPCardReader repository root.
Remove-Item -LiteralPath ".pio" -Recurse -Force -ErrorAction SilentlyContinue
$pkg = Join-Path $env:USERPROFILE ".platformio\packages"
Remove-Item -LiteralPath (Join-Path $pkg "toolchain-xtensa-esp-elf") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $pkg "framework-arduinoespressif32") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $pkg "framework-arduinoespressif32-libs") -Recurse -Force -ErrorAction SilentlyContinue
```

Restart VS Code and Build. PlatformIO will download the packages pinned by
`platformio.ini` and rebuild its project output. These commands delete
only generated build data and downloaded dependencies. **They do not delete
the Git repository source or alter Flash storage on the physical ESP32.**
Ensure downloads complete and antivirus does not quarantine files.

### Step 3: capture the verbose link command instead of losing console output

If it still fails, save a verbose build log to a file:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e t_eth_elite -v *> .\build_verbose.log
```

Inspect the long `xtensa-esp-elf-g++` link invocation (often in an `@...`
response file on Windows), which libraries are supplied and whether the
compiler has its standard libraries available. Attach the `build_verbose.log`
file rather than scrolling in the VS Code terminal. Do not commit logs
containing site credentials.

The project has not yet been linked successfully on the target environment.
Avoid upload or storage provisioning until the complete build succeeds.
