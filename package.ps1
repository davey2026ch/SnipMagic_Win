[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

# 安全保险：程序正在运行时禁止打包（重建目录窗口期内若程序保存设置，
# 会用内存里的旧状态覆盖 ini，曾导致 token 被清空）
$running = Get-Process -Name "SnipMagic","ScreenshotTool" -ErrorAction SilentlyContinue
$cnName = -join @([char]0x622A,[char]0x56FE,[char]0x5927,[char]0x5E08,'S','n','i','p','M','a','g','i','c')
$running += Get-Process -Name $cnName -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "ABORT: program is running (PID: $($running.Id -join ',')) - close it and retry"
    exit 1
}

$src = Join-Path (Get-Location) "dist\SnipMagic.exe"
if (!(Test-Path $src)) {
    Write-Host "Missing $src - run build.ps1 first"
    exit 1
}

# 名称用码点拼接，避免脚本文件编码差异导致乱码
# 截图大师SnipMagic-绿色版
$pkgName = -join @([char]0x622A,[char]0x56FE,[char]0x5927,[char]0x5E08,
                   'S','n','i','p','M','a','g','i','c',[char]0x2D,
                   [char]0x7EFF,[char]0x8272,[char]0x7248)
# 截图大师SnipMagic.exe
$exeCn = -join @([char]0x622A,[char]0x56FE,[char]0x5927,[char]0x5E08,
                 'S','n','i','p','M','a','g','i','c') + ".exe"
# 截图工具.exe（旧版中文名，用于识别历史目录里的用户配置）
$legacyExe = -join @([char]0x622A,[char]0x56FE,[char]0x5DE5,[char]0x5177) + ".exe"
$legacyIni = $legacyExe -replace "\.exe$", ".ini"
$legacyPkg = -join @([char]0x622A,[char]0x56FE,[char]0x5DE5,[char]0x5177,[char]0x2D,
                     [char]0x7EFF,[char]0x8272,[char]0x7248)
$iniName = "SnipMagic.ini"

$pkg = Join-Path (Get-Location) $pkgName
# 保留用户已有配置（含 MinerUToken / VolcApiKey），打包重建时不清掉
$iniBackup = $null
$oldIni = Join-Path $pkg $iniName
if (Test-Path $oldIni) {
    $iniBackup = Join-Path $env:TEMP ("snipmagic_ini_" + [guid]::NewGuid().ToString("N") + ".ini")
    Copy-Item $oldIni $iniBackup -Force
}
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force -Path $pkg | Out-Null

Copy-Item $src (Join-Path $pkg $exeCn) -Force
Copy-Item $src (Join-Path $pkg "SnipMagic.exe") -Force

$iniPath = Join-Path $pkg $iniName
if ($iniBackup -and (Test-Path $iniBackup)) {
    Move-Item $iniBackup $iniPath -Force
}
if (!(Test-Path $iniPath)) {
    # 从旧版目录迁移用户配置（截图工具.ini / 旧中文 exe 同名 ini）
    foreach ($legacyDir in @((Join-Path (Get-Location) $legacyPkg), (Join-Path (Get-Location) "dist"))) {
        foreach ($cand in @((Join-Path $legacyDir $iniName), (Join-Path $legacyDir $legacyIni))) {
            if (!(Test-Path $iniPath) -and (Test-Path $cand)) {
                Copy-Item $cand $iniPath -Force
                Write-Host "migrated user ini from: $cand"
            }
        }
    }
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
        "DrawAlpha=255"
    ) -join "`r`n" | Out-File -FilePath $iniPath -Encoding unicode
}

Write-Host "Portable package: $pkg"
Get-ChildItem $pkg | ForEach-Object { Write-Host ("  " + $_.Name + "  " + $_.Length) }
Write-Host "DONE"
