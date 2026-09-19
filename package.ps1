[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

$src = Join-Path (Get-Location) "dist\ScreenshotTool.exe"
if (!(Test-Path $src)) {
    Write-Host "Missing $src - run build.ps1 first"
    exit 1
}

# Portable folder name (Chinese) built from codepoints to avoid encoding issues
$pkgName = -join @([char]0x622A,[char]0x56FE,[char]0x5DE5,[char]0x5177,[char]0x2D,[char]0x7EFF,[char]0x8272,[char]0x7248)
$exeName = -join @([char]0x622A,[char]0x56FE,[char]0x5DE5,[char]0x5177) + ".exe"
$iniName = -join @([char]0x622A,[char]0x56FE,[char]0x5DE5,[char]0x5177) + ".ini"

$pkg = Join-Path (Get-Location) $pkgName
# 保留用户已有配置（含 MinerUToken / VolcApiKey），打包重建时不清掉
$iniBackup = $null
$oldIni = Join-Path $pkg $iniName
if (Test-Path $oldIni) {
    $iniBackup = Join-Path $env:TEMP ("screenshot_tool_ini_" + [guid]::NewGuid().ToString("N") + ".ini")
    Copy-Item $oldIni $iniBackup -Force
}
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force -Path $pkg | Out-Null

Copy-Item $src (Join-Path $pkg $exeName) -Force
Copy-Item $src (Join-Path $pkg "ScreenshotTool.exe") -Force

$iniPath = Join-Path $pkg $iniName
if ($iniBackup -and (Test-Path $iniBackup)) {
    Move-Item $iniBackup $iniPath -Force
}
if (!(Test-Path $iniPath)) {
    @(
        "[Settings]",
        "Hotkey=Ctrl+Shift+R",
        "HotkeyModifiers=6",
        "HotkeyVk=82",
        "LongHotkey=Ctrl+Shift+E",
        "LongHotkeyModifiers=6",
        "LongHotkeyVk=69",
        "Theme=0",
        "MosaicSize=10",
        "LineThickness=4",
        "BrushThickness=25",
        "ThemeColor=#0078D4",
        "DrawColor=#FF0000",
        "DrawAlpha=255",
        "[Meta]",
        "Version=1.0.0"
    ) -join "`r`n" | Out-File -FilePath $iniPath -Encoding unicode
}

Write-Host "Portable package: $pkg"
Get-ChildItem $pkg | ForEach-Object { Write-Host ("  " + $_.Name + "  " + $_.Length) }
Write-Host "DONE"
