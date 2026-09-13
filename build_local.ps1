# build_local.ps1 - 本地一键编译脚本
$ErrorActionPreference = "Stop"
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "    AI Passport 本地固件一键编译" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

$idfInit = "C:\Espressif\Initialize-Idf.ps1"
if (-not (Test-Path $idfInit)) {
    Write-Error "未找到 C:\Espressif\Initialize-Idf.ps1，请确认 ESP-IDF 工具链路径！"
    exit 1
}

Write-Host "[1/2] 正在加载 ESP-IDF 环境..." -ForegroundColor Yellow
& $idfInit -IdfId esp-idf-7a8737a10725bc5cb19d516a1186b39f

Write-Host "[2/2] 开始编译固件..." -ForegroundColor Yellow
Set-Location "$PSScriptRoot\firmware"
idf.py build

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✅ 本地编译成功！固件生成在 firmware\build\FoloToy-AI-Passport.bin" -ForegroundColor Green
} else {
    Write-Host "`n❌ 编译失败，请检查上方报错！" -ForegroundColor Red
}
