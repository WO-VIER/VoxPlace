param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\win-$($Config.ToLower())"

# ============================================================
# Helper functions
# ============================================================
function Write-Step {
    param([string]$msg)
    Write-Host "`n==> $msg" -ForegroundColor Cyan
}

function Test-Command {
    param([string]$Name)
    $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

# ============================================================
# 1. Check prerequisites
# ============================================================
Write-Step "Checking prerequisites..."

# CMake
if (-not (Test-Command "cmake")) {
    Write-Host "CMake not found. Install with: winget install Kitware.CMake" -ForegroundColor Red
    exit 1
}

# Compiler (MSVC cl or clang)
$compiler = $null
if (Test-Command "cl") {
    $compiler = "cl"
    Write-Host "Found MSVC compiler (cl)" -ForegroundColor Green
} elseif (Test-Command "clang") {
    $compiler = "clang"
    Write-Host "Found Clang compiler" -ForegroundColor Green
} else {
    Write-Host "No compiler found. Install one of:" -ForegroundColor Red
    Write-Host "  MSVC Build Tools: winget install Microsoft.VisualStudio.2022.BuildTools" -ForegroundColor Yellow
    Write-Host "  LLVM/Clang:       winget install LLVM.LLVM" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "For MSVC, open 'x64 Native Tools Command Prompt for VS 2022' and run this script from there." -ForegroundColor Yellow
    exit 1
}

# Ninja (optional but recommended)
$useNinja = Test-Command "ninja"
if (-not $useNinja) {
    Write-Host "Ninja not found, using default generator. Install with: winget install Ninja-build.Ninja" -ForegroundColor Yellow
}

# ============================================================
# 2. Setup vcpkg
# ============================================================
$vcpkgRoot = $null
if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) {
    $vcpkgRoot = $env:VCPKG_ROOT
    Write-Host "Found vcpkg at: $vcpkgRoot" -ForegroundColor Green
} elseif (Test-Path (Join-Path $projectRoot "vcpkg\vcpkg.exe")) {
    $vcpkgRoot = Join-Path $projectRoot "vcpkg"
    Write-Host "Found vcpkg at: $vcpkgRoot" -ForegroundColor Green
} else {
    Write-Step "vcpkg not found. Cloning to project directory..."
    $vcpkgRoot = Join-Path $projectRoot "vcpkg"
    git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($IsWindows -or $env:OS) {
        & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat")
    } else {
        & (Join-Path $vcpkgRoot "bootstrap-vcpkg.sh")
    }
}

$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
$vcpkgToolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"

# ============================================================
# 3. Install dependencies
# ============================================================
Write-Step "Installing dependencies via vcpkg..."

# Determine triplet
if ($compiler -eq "clang") {
    $triplet = "x64-windows"
} else {
    $triplet = "x64-windows"
}

& $vcpkgExe install --triplet $triplet
if ($LASTEXITCODE -ne 0) {
    Write-Host "vcpkg install failed" -ForegroundColor Red
    exit 1
}

# ============================================================
# 4. Clean if requested
# ============================================================
if ($Clean -and (Test-Path $buildDir)) {
    Write-Step "Cleaning build directory..."
    Remove-Item -Recurse -Force $buildDir
}

# ============================================================
# 5. Configure
# ============================================================
Write-Step "Configuring CMake ($Config)..."

$cmakeArgs = @(
    "-S", $projectRoot
    "-B", $buildDir
    "-DCMAKE_BUILD_TYPE=$Config"
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
)

if ($useNinja) {
    $cmakeArgs += "-G", "Ninja"
}

if ($compiler -eq "clang") {
    $cmakeArgs += "-DCMAKE_C_COMPILER=clang"
    $cmakeArgs += "-DCMAKE_CXX_COMPILER=clang++"
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed" -ForegroundColor Red
    exit 1
}

# ============================================================
# 6. Build
# ============================================================
Write-Step "Building ($Config)..."

& cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed" -ForegroundColor Red
    exit 1
}

# ============================================================
# 7. Done
# ============================================================
Write-Step "Build complete!"
Write-Host ""
Write-Host "Executables are in: $buildDir" -ForegroundColor Green
Write-Host ""
Write-Host "Run client:  & `"$buildDir\VoxPlace.exe`"" -ForegroundColor Yellow
Write-Host "Run server:  & `"$buildDir\VoxPlaceServer.exe`"" -ForegroundColor Yellow
