$ErrorActionPreference = "Stop"

Write-Host "==> Cleaning build directory..." -ForegroundColor Blue

if (Test-Path "build") {
    Remove-Item -Recurse -Force "build"
}

Write-Host "==> Configuring CMake..." -ForegroundColor Blue

cmake -S . -B build

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "==> CMake configuration failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "==> Building Release..." -ForegroundColor Blue

cmake --build build --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "==> Build failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "==> Build completed successfully." -ForegroundColor Green