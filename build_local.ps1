$ErrorActionPreference = "Stop"
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "    AI Passport Local Firmware Builder" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

$idfInit = "C:\Espressif\Initialize-Idf.ps1"
if (-not (Test-Path $idfInit)) {
    Write-Error "C:\Espressif\Initialize-Idf.ps1 not found! Check ESP-IDF installation."
    exit 1
}

Write-Host "[1/2] Loading ESP-IDF environment..." -ForegroundColor Yellow
& $idfInit -IdfId esp-idf-7a8737a10725bc5cb19d516a1186b39f

Write-Host "[2/2] Building firmware..." -ForegroundColor Yellow
Set-Location "$PSScriptRoot\firmware"
idf.py build

if ($LASTEXITCODE -eq 0) {
    Write-Host "SUCCESS: Local build succeeded! Firmware generated at firmware\build\FoloToy-AI-Passport.bin" -ForegroundColor Green
} else {
    Write-Host "FAILED: Build failed, check logs above." -ForegroundColor Red
}
