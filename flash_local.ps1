param (
    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "    AI Passport Firmware Flasher" -ForegroundColor Cyan
Write-Host "    Target Port: $Port" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

$firmwareDir = "$PSScriptRoot\firmware\build"
$appBin = "$firmwareDir\FoloToy-AI-Passport.bin"
$bootloaderBin = "$firmwareDir\bootloader\bootloader.bin"
$partitionsBin = "$firmwareDir\partition_table\partition-table.bin"

if (-not (Test-Path $appBin)) {
    Write-Host "Firmware not found, running build_local.ps1..." -ForegroundColor Yellow
    & "$PSScriptRoot\build_local.ps1"
}

$python = "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
if (-not (Test-Path $python)) {
    $python = "python"
}

Write-Host "Flashing firmware to $Port (baud: 460800)..." -ForegroundColor Green
& $python -m esptool --chip esp32c3 -p $Port -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 $bootloaderBin 0x8000 $partitionsBin 0x10000 $appBin

if ($LASTEXITCODE -eq 0) {
    Write-Host "SUCCESS: Firmware flashed successfully! Board rebooted." -ForegroundColor Green
} else {
    Write-Host "FAILED: Flashing failed, check port availability." -ForegroundColor Red
}
