param(
    [string]$Cli = "arduino-cli"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command -Name $Cli -ErrorAction SilentlyContinue)) {
    throw "Arduino CLI not found: $Cli. Install it first or pass -Cli 'D:\EspTools\arduino-cli.exe'."
}

$index = "https://espressif.github.io/arduino-esp32/package_esp32_index.json"

Write-Host "Installing official Espressif Arduino ESP32 3.3.7 core..."
& $Cli core update-index --additional-urls $index
if ($LASTEXITCODE -ne 0) { throw "Arduino core index update failed." }

& $Cli core install "esp32:esp32@3.3.7" --additional-urls $index
if ($LASTEXITCODE -ne 0) { throw "Arduino ESP32 core installation failed." }

Write-Host "Installing libraries..."
& $Cli lib install "PubSubClient@2.8.0"
if ($LASTEXITCODE -ne 0) { throw "PubSubClient installation failed." }

& $Cli lib install "ArduinoJson@7.4.3"
if ($LASTEXITCODE -ne 0) { throw "ArduinoJson installation failed." }

Write-Host "Arduino CLI setup completed."
