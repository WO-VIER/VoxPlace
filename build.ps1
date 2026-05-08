param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Clean,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
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

function Resolve-VcpkgRoot {
    param([string]$RequestedRoot)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $candidates += $RequestedRoot
    }

    $vcpkgCommand = Get-Command "vcpkg" -ErrorAction SilentlyContinue
    if ($null -ne $vcpkgCommand) {
        $candidates += (Split-Path -Parent $vcpkgCommand.Source)
    }

    $candidates += (Join-Path $projectRoot "vcpkg")

    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $toolchainFile = Join-Path $candidate "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path $toolchainFile) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Get-VcpkgExecutable {
    param([string]$Root)

    $exe = Join-Path $Root "vcpkg.exe"
    if (Test-Path $exe) {
        return $exe
    }

    $exe = Join-Path $Root "vcpkg"
    if (Test-Path $exe) {
        return $exe
    }

    return $null
}

function Ensure-VcpkgCurl {
    param([string]$Root)

    $vcpkgExe = Get-VcpkgExecutable $Root
    if ($null -eq $vcpkgExe) {
        Write-Host "vcpkg executable not found in $Root" -ForegroundColor Yellow
        return
    }

    Write-Step "Ensuring libcurl is available through vcpkg..."
    & $vcpkgExe install "curl:x64-windows"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Failed to install curl:x64-windows with vcpkg" -ForegroundColor Red
        exit 1
    }
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
# Windows libcurl dependency
# ============================================================
$resolvedVcpkgRoot = Resolve-VcpkgRoot $VcpkgRoot
if ($null -ne $resolvedVcpkgRoot) {
    Ensure-VcpkgCurl $resolvedVcpkgRoot
} else {
    Write-Host "vcpkg not found. libcurl is required for the HTTPS .proof check." -ForegroundColor Yellow
    Write-Host "Install vcpkg, then run: vcpkg install curl:x64-windows" -ForegroundColor Yellow
    Write-Host "Set VCPKG_ROOT or pass -VcpkgRoot C:\path\to\vcpkg before building." -ForegroundColor Yellow
}

# ============================================================
# Clean if requested or if CMakeLists.txt changed
# ============================================================
$cacheFile = Join-Path $buildDir "CMakeCache.txt"
$rootCMake = Join-Path $projectRoot "CMakeLists.txt"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Step "Cleaning build directory..."
    Remove-Item -Recurse -Force $buildDir
} elseif ((Test-Path $cacheFile) -and (Test-Path $rootCMake)) {
    $cacheTime = (Get-Item $cacheFile).LastWriteTime
    $cmakeTime = (Get-Item $rootCMake).LastWriteTime
    if ($cmakeTime -gt $cacheTime) {
        Write-Step "CMakeLists.txt changed, cleaning build directory..."
        Remove-Item -Recurse -Force $buildDir
    }
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

if ($null -ne $resolvedVcpkgRoot) {
    $toolchainFile = Join-Path $resolvedVcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
    $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
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
