param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [string]$Cli = "D:\EspTools\arduino-cli.exe",
    [string]$BuildPath = "D:\EspTools\Build\Integr8ESPCardReader"
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
    throw "Arduino CLI not found: $Cli."
}

if (-not (Test-Path -LiteralPath $BuildPath -PathType Container)) {
    throw "Build folder missing: $BuildPath. Run scripts/build.ps1 first."
}

$board = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,FlashMode=qio,CDCOnBoot=cdc"

Write-Host "Uploading previously compiled firmware from $BuildPath to $Port"
Write-Host "The default upload does not erase the full flash; do NOT use EraseFlash=all."

& $Cli upload --port $Port --fqbn $board --build-path $BuildPath $repoRoot

if ($LASTEXITCODE -ne 0) {
    throw "Arduino CLI upload failed with exit code $LASTEXITCODE."
}

Write-Host "Upload finished. Open a serial monitor at 115200 baud to check startup."
