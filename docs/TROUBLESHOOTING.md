# PlatformIO build troubleshooting (Windows)

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

The current `board = esp32-s3-devkitc-1` is a **generic board profile**
whose default display name is an 8MB/no-PSRAM DevKit.
The actual T-ETH-Elite board has **16MB Flash and 8MB OPI PSRAM**, and
`platformio.ini` explicitly overrides the build flash size/memory type.
The printed default board headline therefore does not establish the
physical memory size. Before production use, confirm upload image header,
partition table, actual Flash size and PSRAM detection on the physical board.
Do not change partition schemes casually because it can destroy records.

Manufacturer:
https://wiki.lilygo.cc/products/t-eth-series/t-eth-elite/
