[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

# 安全保险：程序运行时不打包（避免与 ini 的生成/迁移竞争）
$running = Get-Process -Name "SnipMagic","ScreenshotTool" -ErrorAction SilentlyContinue
$cnProg = -join @([char]0x622A,[char]0x56FE,[char]0x5927,[char]0x5E08,'S','n','i','p','M','a','g','i','c')
$running += Get-Process -Name $cnProg -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "ABORT: program is running (PID: $($running.Id -join ',')) - close it and retry"
    exit 1
}

# 打包目标就是 dist 目录：保留 SnipMagic.exe + 截图大师SnipMagic.exe + SnipMagic.ini
$dist = Join-Path (Get-Location) "dist"
if (!(Test-Path $dist)) { New-Item -ItemType Directory -Force -Path $dist | Out-Null }

$exeEn = Join-Path $dist "SnipMagic.exe"
if (!(Test-Path $exeEn)) {
    Write-Host "Missing $exeEn - run build.ps1 first"
    exit 1
}

# 中文名副本：截图大师SnipMagic.exe（与 SnipMagic.exe 内容相同）
$exeCn = Join-Path $dist (-join @([char]0x622A,[char]0x56FE,[char]0x5927,[char]0x5E08,
                                  'S','n','i','p','M','a','g','i','c') + ".exe")
Copy-Item $exeEn $exeCn -Force

# 配置文件：已存在则原样保留（绝不覆盖用户配置，含 MinerUToken / VolcApiKey）
$iniName = "SnipMagic.ini"
$iniPath = Join-Path $dist $iniName
if (!(Test-Path $iniPath)) {
    # 旧版目录迁移（历史遗留：截图工具.ini / 旧绿色版目录下的 ini）
    $legacyIni = -join @([char]0x622A,[char]0x56FE,[char]0x5DE5,[char]0x5177) + ".ini"
    $legacyPkg = -join @([char]0x622A,[char]0x56FE,[char]0x5DE5,[char]0x5177,[char]0x2D,
                         [char]0x7EFF,[char]0x8272,[char]0x7248)
    $candidates = @(
        (Join-Path $dist $legacyIni),
        (Join-Path (Get-Location) $legacyIni),
        (Join-Path (Join-Path (Get-Location) $legacyPkg) $iniName)
    )
    foreach ($cand in $candidates) {
        if (Test-Path $cand) {
            Copy-Item $cand $iniPath -Force
            Write-Host "migrated user ini from: $cand"
            break
        }
    }
}
if (!(Test-Path $iniPath)) {
    # 默认模板（纯 ASCII 内容，与程序 ANSI 读写保持一致）
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
    ) -join "`r`n" | Out-File -FilePath $iniPath -Encoding ascii
}

Write-Host "Package (dist):"
Get-ChildItem $dist -Filter *.exe | ForEach-Object { Write-Host ("  " + $_.Name + "  " + $_.Length) }
Write-Host ("  " + $iniName + "  " + (Get-Item $iniPath).Length)
Write-Host "DONE"
