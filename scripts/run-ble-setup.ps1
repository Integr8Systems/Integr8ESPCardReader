param(
    [string]$Python = "py"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

Write-Host "Integr8 Reader Setup requires Python 3.10+ with Bleak."
Write-Host "For first use, install: $Python -m pip install bleak"
& $Python (Join-Path $root "tools/integr8_reader_setup.py")
if ($LASTEXITCODE -ne 0) {
    throw "Reader setup app failed (exit code $LASTEXITCODE)."
}
