# build_local.ps1
# Equivalent to .github/workflows/build.yml — run from the repo root.
# Requirements: MSYS2 installed at C:\msys64, 7-Zip at default path.
#
# Usage:  .\build_local.ps1
# Output: .\Sightline Sandbox\ClientTube.exe  +  libvlc.dll  +  plugins\

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
Set-Location $RepoRoot

# Output folder (sibling to src\ inside the repo, already exists per screenshot)
$OutputDir = Join-Path $RepoRoot "Sightline Sandbox"

# ---------------------------------------------------------------------------
# 0. Helpers
# ---------------------------------------------------------------------------
function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Die($msg)  { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

$SevenZip = "C:\Program Files\7-Zip\7z.exe"
if (-not (Test-Path $SevenZip)) { Die "7-Zip not found at $SevenZip. Install it from https://7-zip.org" }

$Msys2Bash = "C:\msys64\usr\bin\bash.exe"
if (-not (Test-Path $Msys2Bash)) { Die "MSYS2 not found at C:\msys64. Install it from https://www.msys2.org" }

# Helper: run a bash command with the MINGW32 toolchain on PATH.
function Bash($cmd) {
    & $Msys2Bash -lc "export PATH=/mingw32/bin:`$PATH; $cmd"
    if ($LASTEXITCODE -ne 0) { Die "Command failed: $cmd" }
}

# ---------------------------------------------------------------------------
# 1. Install MSYS2 packages (idempotent)
# ---------------------------------------------------------------------------
Step "Ensuring MSYS2 MINGW32 packages are installed"
& $Msys2Bash -lc "pacman -S --needed --noconfirm mingw-w64-i686-toolchain mingw-w64-i686-cmake mingw-w64-i686-ninja 2>&1"
if ($LASTEXITCODE -ne 0) { Die "pacman install failed" }

# ---------------------------------------------------------------------------
# 2. Third-party headers
# ---------------------------------------------------------------------------
Step "Fetching third-party headers"

if (-not (Test-Path "third_party\imgui")) {
    Write-Host "  Cloning ImGui v1.91.6..."
    git clone --depth=1 --branch v1.91.6 https://github.com/ocornut/imgui.git third_party/imgui
    if ($LASTEXITCODE -ne 0) { Die "git clone imgui failed" }
} else {
    Write-Host "  ImGui already present, skipping."
}

# json.hpp must live at third_party\nlohmann\json.hpp so that
# #include <nlohmann/json.hpp> resolves when -I third_party is on the compiler path.
if (-not (Test-Path "third_party\nlohmann\json.hpp")) {
    Write-Host "  Downloading json.hpp into third_party\nlohmann\..."
    New-Item -ItemType Directory -Path "third_party\nlohmann" -Force | Out-Null
    Invoke-WebRequest `
        -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" `
        -OutFile "third_party\nlohmann\json.hpp"
} else {
    Write-Host "  nlohmann/json.hpp already present, skipping."
}

if (-not (Test-Path "third_party\stb_image.h")) {
    Write-Host "  Downloading stb_image.h..."
    Invoke-WebRequest `
        -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" `
        -OutFile "third_party\stb_image.h"
} else {
    Write-Host "  stb_image.h already present, skipping."
}

if (-not (Test-Path "third_party\IconsFontAwesome6.h")) {
    Write-Host "  Downloading IconsFontAwesome6.h..."
    Invoke-WebRequest `
        -Uri "https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/main/IconsFontAwesome6.h" `
        -OutFile "third_party\IconsFontAwesome6.h"
} else {
    Write-Host "  IconsFontAwesome6.h already present, skipping."
}

# ---------------------------------------------------------------------------
# 2b. Font Awesome TTF (runtime asset — must ship next to ClientTube.exe)
# ---------------------------------------------------------------------------
New-Item -ItemType Directory -Path "assets" -Force | Out-Null

if (-not (Test-Path "assets\fa-solid-900.ttf")) {
    Write-Host "  Downloading fa-solid-900.ttf..."
    Invoke-WebRequest `
        -Uri "https://github.com/FortAwesome/Font-Awesome/raw/6.x/webfonts/fa-solid-900.ttf" `
        -OutFile "assets\fa-solid-900.ttf"
} else {
    Write-Host "  fa-solid-900.ttf already present, skipping."
}

# ---------------------------------------------------------------------------
# 3. VLC SDK
# ---------------------------------------------------------------------------
Step "Setting up VLC SDK"

$VlcVer  = "3.0.21"
$VlcArch = "win32"
$VlcSDK  = "$RepoRoot\vlc_sdk"

if (-not (Test-Path "$VlcSDK\sdk\include\vlc\vlc.h")) {
    $VlcUrl  = "https://download.videolan.org/vlc/$VlcVer/$VlcArch/vlc-$VlcVer-$VlcArch.7z"
    $VlcDest = "$RepoRoot\vlc.7z"

    Write-Host "  Downloading VLC $VlcVer ($VlcArch)..."
    Invoke-WebRequest -Uri $VlcUrl -OutFile $VlcDest

    Write-Host "  Extracting..."
    & $SevenZip x $VlcDest -o"vlc_extracted" -y | Out-Null

    $Inner = Get-ChildItem vlc_extracted -Directory | Select-Object -First 1
    if ($null -eq $Inner) { Die "VLC archive extraction produced no directory" }
    Rename-Item $Inner.FullName "vlc_sdk"
    Move-Item "vlc_extracted\vlc_sdk" $VlcSDK

    Remove-Item $VlcDest -Force
    Remove-Item "vlc_extracted" -Recurse -Force -ErrorAction SilentlyContinue

    if (-not (Test-Path "$VlcSDK\sdk\include\vlc\vlc.h")) {
        Die "vlc.h not found after extraction!"
    }
    Write-Host "  VLC SDK ready."
} else {
    Write-Host "  VLC SDK already present, skipping download."
}

# ---------------------------------------------------------------------------
# 4. CMake configure
# ---------------------------------------------------------------------------
Step "Configuring with CMake (Ninja / MinGW32)"

$VlcSdkEscaped  = $VlcSDK.Replace('\', '\\')
$VlcRootMsys    = (& $Msys2Bash -lc "export PATH=/mingw32/bin:`$PATH; cygpath -m '$VlcSdkEscaped'").Trim()
$RepoRootFwd    = $RepoRoot.Replace('\', '/')

Bash @"
cd '$RepoRootFwd' && cmake -S . -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DVLC_SDK_ROOT='$VlcRootMsys'
"@

# ---------------------------------------------------------------------------
# 5. Build
# ---------------------------------------------------------------------------
Step "Building"

Bash "cd '$RepoRootFwd' && cmake --build build --parallel"

# ---------------------------------------------------------------------------
# 6. Copy final artifacts to 'Sightline Sandbox'
# ---------------------------------------------------------------------------
Step "Copying artifacts to 'Sightline Sandbox'"

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    Write-Host "  Created: $OutputDir"
}

Copy-Item "build\ClientTube.exe"   $OutputDir -Force
Copy-Item "$VlcSDK\libvlc.dll"     $OutputDir -Force
Copy-Item "$VlcSDK\libvlccore.dll" $OutputDir -Force

# Copy assets/ folder (fonts, etc.) next to the executable
$AssetsOut = Join-Path $OutputDir "assets"
if (-not (Test-Path $AssetsOut)) {
    New-Item -ItemType Directory -Path $AssetsOut | Out-Null
}
Copy-Item "assets\*" $AssetsOut -Recurse -Force
Write-Host "  Copied assets\ -> $AssetsOut"

# Copy loose DLLs from the VLC SDK root that TLS / crypto plugins depend on.
$TlsDlls = @(
    "libgnutls*.dll",
    "libgcrypt*.dll",
    "libgpg-error*.dll",
    "libnettle*.dll",
    "libhogweed*.dll",
    "libp11-kit*.dll",
    "libtasn1*.dll",
    "libunistring*.dll",
    "libidn2*.dll"
)
foreach ($pattern in $TlsDlls) {
    $matches = Get-ChildItem -Path $VlcSDK -Filter $pattern -ErrorAction SilentlyContinue
    foreach ($f in $matches) {
        Copy-Item $f.FullName $OutputDir -Force
        Write-Host "  Copied TLS DLL: $($f.Name)"
    }
}

# Only copy the plugin subfolders actually needed for HTTPS streaming + VP9/H264 playback.
$NeededPlugins = @(
    "access",
    "keystore",
    "misc",
    "codec",
    "demux",
    "packetizer",
    "audio_output",
    "video_output",
    "audio_filter",
    "video_filter",
    "logger"
)

$PluginsDest = Join-Path $OutputDir "plugins"
if (-not (Test-Path $PluginsDest)) {
    New-Item -ItemType Directory -Path $PluginsDest | Out-Null
}

foreach ($sub in $NeededPlugins) {
    $src = Join-Path "$VlcSDK\plugins" $sub
    $dst = Join-Path $PluginsDest $sub
    if (Test-Path $src) {
        if (-not (Test-Path $dst)) {
            Copy-Item $src $dst -Recurse -Force
            Write-Host "  Copied plugins\$sub"
        } else {
            Write-Host "  plugins\$sub already present, skipping."
        }
    } else {
        Write-Host "  WARNING: plugins\$sub not found in VLC SDK, skipping." -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
Write-Host "  Output folder : $OutputDir"
Write-Host "  Executable    : $OutputDir\ClientTube.exe"
Write-Host "  VLC DLLs      : libvlc.dll  libvlccore.dll"
Write-Host "  Assets        : assets\ (fa-solid-900.ttf, ...)"
Write-Host "  Plugins       : plugins\ (essential subfolders only)"
