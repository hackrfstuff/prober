# package_windows.ps1 - create portable distribution folder
# run from VS Developer Command Prompt or Qt Command Prompt

param(
    [string]$QtPath = "C:\Qt\6.10.2\msvc2022_64",
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$DistDir = Join-Path $RepoRoot "dist"
$BuildDir = Join-Path $RepoRoot "build"

Write-Host "=== Prober Portable Distribution Builder ===" -ForegroundColor Cyan
Write-Host "Repo root: $RepoRoot"
Write-Host "Dist dir: $DistDir"
Write-Host "Qt path: $QtPath"
Write-Host ""

Write-Host "[1/8] Cleaning dist folder..." -ForegroundColor Yellow
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Write-Host "[2/8] Configuring CMake..." -ForegroundColor Yellow
Push-Location $RepoRoot
try {
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtPath"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

    Write-Host "[3/8] Building..." -ForegroundColor Yellow
    cmake --build build -j
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
} finally {
    Pop-Location
}

Write-Host "[4/8] Copying executables..." -ForegroundColor Yellow
$CliExe = Join-Path $BuildDir "prober.exe"
$GuiImplExe = Join-Path $BuildDir "gui\prober_gui_impl.exe"
$LauncherExe = Join-Path $BuildDir "gui_launcher\prober_gui.exe"

if (-not (Test-Path $CliExe)) { throw "CLI not found: $CliExe" }
if (-not (Test-Path $GuiImplExe)) { throw "GUI impl not found: $GuiImplExe" }
if (-not (Test-Path $LauncherExe)) { throw "GUI launcher not found: $LauncherExe" }

Copy-Item $CliExe (Join-Path $DistDir "prober.exe")
Copy-Item $LauncherExe (Join-Path $DistDir "prober_gui.exe")

$GuiDistDir = Join-Path $DistDir "tools\gui"
New-Item -ItemType Directory -Force -Path $GuiDistDir | Out-Null
Copy-Item $GuiImplExe (Join-Path $GuiDistDir "prober_gui_impl.exe")

Write-Host "[5/8] Copying tools..." -ForegroundColor Yellow
$ToolsSrc = Join-Path $RepoRoot "tools\avrdude"
$ToolsDst = Join-Path $DistDir "tools\avrdude"
New-Item -ItemType Directory -Force -Path $ToolsDst | Out-Null
Copy-Item "$ToolsSrc\*" $ToolsDst -Recurse

$FirmwareSrc = Join-Path $RepoRoot "tools\c2_firmware"
$FirmwareDst = Join-Path $DistDir "tools\c2_firmware"
if (-not (Test-Path $FirmwareSrc)) {
    throw "C2 interface firmware source missing: $FirmwareSrc"
}
$FirmwareHex = Join-Path $FirmwareSrc "uno_nano.hex"
if (-not (Test-Path $FirmwareHex)) {
    throw "C2 interface firmware missing: $FirmwareHex"
}
New-Item -ItemType Directory -Force -Path $FirmwareDst | Out-Null
Copy-Item "$FirmwareSrc\*.hex" $FirmwareDst
Copy-Item "$FirmwareSrc\README.txt" $FirmwareDst -ErrorAction SilentlyContinue
$CopiedHex = Get-ChildItem $FirmwareDst -Filter "*.hex"
Write-Host "  C2 firmware: $($CopiedHex.Name) ($([math]::Round($CopiedHex.Length / 1KB, 1)) KB)" -ForegroundColor Green

$BluejayVersions = @("v0.21.0", "v0.19.2")
foreach ($ver in $BluejayVersions) {
    $BjSrc = Join-Path $RepoRoot "tools\bluejay_firmware\$ver"
    $BjDst = Join-Path $DistDir "tools\bluejay_firmware\$ver"
    if (-not (Test-Path $BjSrc)) {
        throw "Bluejay firmware source missing: $BjSrc"
    }
    $SrcHexCount = (Get-ChildItem $BjSrc -Filter "*.hex").Count
    if ($SrcHexCount -eq 0) {
        throw "Bluejay firmware $ver contains zero .hex files: $BjSrc"
    }
    New-Item -ItemType Directory -Force -Path $BjDst | Out-Null
    Copy-Item "$BjSrc\*.hex" $BjDst
    $DstHexCount = (Get-ChildItem $BjDst -Filter "*.hex").Count
    Write-Host "  Bluejay $ver`: $DstHexCount hex files copied" -ForegroundColor Green
}
$BjReadme = Join-Path $RepoRoot "tools\bluejay_firmware\README.txt"
$BjReadmeDst = Join-Path $DistDir "tools\bluejay_firmware"
New-Item -ItemType Directory -Force -Path $BjReadmeDst | Out-Null
Copy-Item $BjReadme $BjReadmeDst -ErrorAction SilentlyContinue

Write-Host "[6/8] Deploying Qt runtime..." -ForegroundColor Yellow
$WinDeployQt = Join-Path $QtPath "bin\windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) { throw "windeployqt not found: $WinDeployQt" }

$GuiImplDist = Join-Path $GuiDistDir "prober_gui_impl.exe"
& $WinDeployQt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import $GuiImplDist
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

Write-Host "[7/8] Bundling vc_redist..." -ForegroundColor Yellow
$VcRedist = $null
if ($env:VC_REDIST_PATH -and (Test-Path $env:VC_REDIST_PATH)) {
    $VcRedist = $env:VC_REDIST_PATH
} else {
    $VsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
        "C:\Program Files\Microsoft Visual Studio\18\Community",
        "C:\Program Files\Microsoft Visual Studio\18\Professional",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise"
    )
    foreach ($vsPath in $VsPaths) {
        $redists = Get-ChildItem -Path "$vsPath\VC\Redist\MSVC" -Filter "vc_redist.x64.exe" -Recurse -ErrorAction SilentlyContinue
        if ($redists) {
            $VcRedist = $redists[0].FullName
            break
        }
    }
}
if ($VcRedist) {
    Copy-Item $VcRedist (Join-Path $DistDir "vc_redist.x64.exe")
    Write-Host "  Bundled: $VcRedist" -ForegroundColor Green
} else {
    Write-Host "  WARNING: vc_redist.x64.exe not found. Users may need to install VC++ Redistributable separately." -ForegroundColor Red
    Write-Host "  Set VC_REDIST_PATH env var to override." -ForegroundColor Red
}

Write-Host ""
Write-Host "[8/9] Enforcing clean dist root..." -ForegroundColor Yellow

$DupeRedists = Get-ChildItem -Path (Join-Path $DistDir "tools") -Filter "vc_redist*" -Recurse -ErrorAction SilentlyContinue
foreach ($dupe in $DupeRedists) {
    Write-Host "  Removing duplicate: $($dupe.FullName)" -ForegroundColor Yellow
    Remove-Item $dupe.FullName -Force
}

$AllowedRoot = @("prober.exe", "prober_gui.exe", "vc_redist.x64.exe", "tools")
$RootItems = Get-ChildItem $DistDir | ForEach-Object { $_.Name }
$Unexpected = $RootItems | Where-Object { $_ -notin $AllowedRoot }
if ($Unexpected) {
    foreach ($item in $Unexpected) {
        $itemPath = Join-Path $DistDir $item
        Write-Host "  Removing unexpected: $item" -ForegroundColor Yellow
        Remove-Item $itemPath -Recurse -Force
    }
    Write-Host "  Cleaned unexpected items from dist root" -ForegroundColor Green
} else {
    Write-Host "  Dist root already clean" -ForegroundColor Green
}

Write-Host ""
Write-Host "[9/9] Running sanity checks..." -ForegroundColor Yellow

$RootItems = Get-ChildItem $DistDir | ForEach-Object { $_.Name }
$Unexpected = $RootItems | Where-Object { $_ -notin $AllowedRoot }
if ($Unexpected) {
    Write-Host "  FAIL: Could not clean dist root: $($Unexpected -join ', ')" -ForegroundColor Red
    throw "Dist root enforcement failed"
} else {
    Write-Host "  Dist root: OK ($($RootItems -join ', '))" -ForegroundColor Green
}

$CliDist = Join-Path $DistDir "prober.exe"
$CliTest = & $CliDist --list-ports --json 2>&1
if ($LASTEXITCODE -le 1) {
    Write-Host "  CLI: OK" -ForegroundColor Green
} else {
    Write-Host "  CLI: FAILED (exit code $LASTEXITCODE)" -ForegroundColor Red
}

$GuiImplCheck = Join-Path $GuiDistDir "prober_gui_impl.exe"
if (Test-Path $GuiImplCheck) {
    Write-Host "  GUI impl: OK ($GuiImplCheck)" -ForegroundColor Green
} else {
    Write-Host "  GUI impl: MISSING" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== DIST READY ===" -ForegroundColor Green
Write-Host "Location: $DistDir" -ForegroundColor Green
Write-Host ""
Write-Host "Contents:"
$TotalSize = 0
Get-ChildItem $DistDir -Recurse | ForEach-Object {
    $rel = $_.FullName.Substring($DistDir.Length + 1)
    if ($_.PSIsContainer) {
        Write-Host "  [DIR] $rel"
    } else {
        $TotalSize += $_.Length
        Write-Host "  $rel ($([math]::Round($_.Length / 1KB, 1)) KB)"
    }
}
Write-Host ""
Write-Host "Total size: $([math]::Round($TotalSize / 1MB, 1)) MB" -ForegroundColor Cyan
