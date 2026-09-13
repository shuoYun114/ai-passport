# flash_local.ps1 - 本地一键烧录脚本
param (
    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "    AI Passport 本地固件一键刷写" -ForegroundColor Cyan
Write-Host "    目标端口: $Port" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

$firmwareDir = "$PSScriptRoot\firmware\build"
$appBin = "$firmwareDir\FoloToy-AI-Passport.bin"
$bootloaderBin = "$firmwareDir\bootloader\bootloader.bin"
$partitionsBin = "$firmwareDir\partition_table\partition-table.bin"

if (-not (Test-Path $appBin)) {
    Write-Host "未找到编译生成的固件，正在先为您自动执行本地编译..." -ForegroundColor Yellow
    & "$PSScriptRoot\build_local.ps1"
}

$python = "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
if (-not (Test-Path $python)) {
    $python = "python"
}

Write-Host "正在刷写固件至 $Port (波特率: 460800)..." -ForegroundColor Green
& $python -m esptool --chip esp32c3 -p $Port -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 $bootloaderBin 0x8000 $partitionsBin 0x10000 $appBin

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✅ 固件刷写成功！开发板已自动重启生效！" -ForegroundColor Green
} else {
    Write-Host "`n❌ 刷写失败，请确认串口未被占用并重试！" -ForegroundColor Red
}
