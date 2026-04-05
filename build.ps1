param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\win-$($Config.ToLower())"

function Write-Step {
    param([string]$msg)
    Write-Host "`n==> $msg" -ForegroundColor Cyan
}

function Test-Command {
    param([string]$Name)
    $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

# ============================================================
# Check compiler
# ============================================================
Write-Step "Checking compiler..."

$useMsvc = $false
$useClang = $false

if (Test-Command "cl") {
    $useMsvc = $true
    Write-Host "Found MSVC (cl)" -ForegroundColor Green
} elseif (Test-Command "clang") {
    $useClang = $true
    Write-Host "Found Clang" -ForegroundColor Green
} else {
    Write-Host "No compiler found. Install one:" -ForegroundColor Red
    Write-Host "  winget install Microsoft.VisualStudio.2022.BuildTools" -ForegroundColor Yellow
    Write-Host "  winget install LLVM.LLVM" -ForegroundColor Yellow
    exit 1
}

# ============================================================
# Check CMake
# ============================================================
if (-not (Test-Command "cmake")) {
    Write-Host "CMake not found. Install with: winget install Kitware.CMake" -ForegroundColor Red
    exit 1
}

# ============================================================
# Clean if requested
# ============================================================
if ($Clean -and (Test-Path $buildDir)) {
    Write-Step "Cleaning build directory..."
    Remove-Item -Recurse -Force $buildDir
}

# ============================================================
# Configure
# ============================================================
Write-Step "Configuring CMake ($Config)..."

$cmakeArgs = @(
    "-S", $projectRoot
    "-B", $buildDir
    "-DCMAKE_BUILD_TYPE=$Config"
)

if ($useMsvc) {
    $cmakeArgs += "-G", "Visual Studio 17 2022"
    $cmakeArgs += "-A", "x64"
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed" -ForegroundColor Red
    exit 1
}

# ============================================================
# Build
# ============================================================
Write-Step "Building ($Config)..."

if ($useMsvc) {
    & cmake --build $buildDir --config $Config -- /m
} else {
    & cmake --build $buildDir --config $Config
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed" -ForegroundColor Red
    exit 1
}

# ============================================================
# Done
# ============================================================
Write-Step "Build complete!"
Write-Host ""
Write-Host "Executables are in: $buildDir\$Config\" -ForegroundColor Green
Write-Host ""
Write-Host "Run client:  & `"$buildDir\$Config\VoxPlace.exe`"" -ForegroundColor Yellow
Write-Host "Run server:  & `"$buildDir\$Config\VoxPlaceServer.exe`"" -ForegroundColor Yellow
