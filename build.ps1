[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot
Write-Host "Project root: $((Get-Location).Path)"

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (!(Test-Path $vcvars)) { Write-Host "vcvars64 not found"; exit 1 }
if (!(Test-Path $cmake)) { Write-Host "cmake not found"; exit 1 }

New-Item -ItemType Directory -Force -Path "build","dist" | Out-Null

cmd /c "`"$vcvars`" && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

Write-Host "== CMake configure =="
& $cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "NMake failed, retry with default generator..."
    if (Test-Path build) { Remove-Item -Recurse -Force build }
    New-Item -ItemType Directory -Force -Path build | Out-Null
    & $cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

Write-Host "== Build =="
& $cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host ""
Write-Host "Build finished."
Get-ChildItem -Recurse build,dist -Filter *.exe -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_.FullName }
if (Test-Path "dist\ScreenshotTool.exe") {
    Write-Host "OK: dist\ScreenshotTool.exe"
}
Write-Host "DONE"
