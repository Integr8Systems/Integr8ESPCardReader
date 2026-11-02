param(
    [string]$Cli = "D:\EspTools\arduino-cli.exe",
    [string]$BuildPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not $BuildPath) {
    if ($env:INTEGR8_BUILD_PATH) {
        $BuildPath = $env:INTEGR8_BUILD_PATH
    }
    elseif ($env:ARDUINO_DIRECTORIES_DATA) {
        $BuildPath = Join-Path $env:ARDUINO_DIRECTORIES_DATA "Integr8ESPCardReader-Build"
    }
    else {
        $BuildPath = Join-Path $repoRoot ".arduino-build"
    }
}

if (-not (Get-Command -Name $Cli -ErrorAction SilentlyContinue)) {
    throw "Arduino CLI not found: $Cli. Install arduino-cli or pass -Cli 'D:\EspTools\arduino-cli.exe'."
}

$board = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,FlashMode=qio,CDCOnBoot=cdc"

New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null
Write-Host "Arduino sketch: $repoRoot"
Write-Host "Board:          $board"
Write-Host "Build output:   $BuildPath"

& $Cli compile --fqbn $board --build-path $BuildPath $repoRoot

if ($LASTEXITCODE -ne 0) {
    throw "Arduino CLI compile failed with exit code $LASTEXITCODE."
}

Write-Host "Build succeeded. To upload, run .\scripts\upload.ps1 -Port COMx using the same -Cli and -BuildPath options."
