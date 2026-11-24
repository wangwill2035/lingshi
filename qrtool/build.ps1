# ============================================================
# 入库上架二维码打印工具 - 一键构建脚本
# 用法：powershell -ExecutionPolicy Bypass -File build.ps1
# 产物：dist\入库上架二维码工具.exe
# ============================================================
$ErrorActionPreference = "Stop"
$base = Split-Path -Parent $MyInvocation.MyCommand.Path
$gpp  = Join-Path $base "toolchain\w64devkit\w64devkit\bin\g++.exe"
$wr   = Join-Path $base "toolchain\w64devkit\w64devkit\bin\windres.exe"
$out  = Join-Path $base "dist\入库上架二维码工具.exe"

if (-not (Test-Path -LiteralPath $gpp)) {
    Write-Error "未找到便携编译器：$gpp`n请先解压 toolchain\w64devkit-x64-2.11.0.7z.exe 到 toolchain\ 目录（含一层 w64devkit 子目录）。"
}
if (-not (Test-Path -LiteralPath (Join-Path $base "dist"))) {
    New-Item -ItemType Directory -Path (Join-Path $base "dist") | Out-Null
}

Write-Host "==> 编译版本资源..."
& $wr -c 65001 -I (Join-Path $base "src") `
    (Join-Path $base "src\app.rc") -o (Join-Path $base "src\app_res.o")
if ($LASTEXITCODE -ne 0) { Write-Error "windres 失败，退出码 $LASTEXITCODE" }

Write-Host "==> 编译中..."
& $gpp -O2 -municode -mwindows -std=c++17 -DNDEBUG `
    -finput-charset=UTF-8 -fexec-charset=UTF-8 `
    -I (Join-Path $base "third_party\qrcodegen") `
    (Join-Path $base "src\main.cpp") `
    (Join-Path $base "third_party\qrcodegen\qrcodegen.cpp") `
    (Join-Path $base "src\app_res.o") `
    -o $out -lgdiplus -lgdi32 -luser32 -lcomdlg32 -lwinspool -lcomctl32
if ($LASTEXITCODE -ne 0) { Write-Error "编译失败，退出码 $LASTEXITCODE" }

Write-Host "==> 完成：" -NoNewline
Write-Host $out
