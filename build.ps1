param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Clean,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\win-$($Config.ToLower())"
$global:CMakeExe = $null

function Write-Step {
    param([string]$msg)
    Write-Host "`n==> $msg" -ForegroundColor Cyan
}

function Test-Command {
    param([string]$Name)
    $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Resolve-CommandPath {
    param(
        [string]$Name,
        [string[]]$Candidates = @()
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Add-ToolDirectoryToPath {
    param([string]$ToolPath)

    if ([string]::IsNullOrWhiteSpace($ToolPath)) {
        return
    }

    $directory = Split-Path -Parent $ToolPath
    $pathParts = $env:Path -split ";"
    if ($pathParts -notcontains $directory) {
        $env:Path = "$directory;$env:Path"
    }
}

function Import-MsvcEnvironment {
    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    $vswhere = Resolve-CommandPath "vswhere" $vswhereCandidates
    $installationPath = $null
    if ($null -eq $vswhere) {
        $installationPath = $null
    } else {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
            $installationPath = $null
        }
    }

    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        $knownInstallations = @(
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools",
            "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools"
        )

        foreach ($candidate in $knownInstallations) {
            if (Test-Path (Join-Path $candidate "Common7\Tools\VsDevCmd.bat")) {
                $installationPath = $candidate
                break
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        return $false
    }

    $vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        return $false
    }

    Write-Step "Loading MSVC build environment..."
    $environment = & cmd.exe /s /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    foreach ($line in $environment) {
        $separator = $line.IndexOf("=")
        if ($separator -le 0) {
            continue
        }

        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        Set-Item -Path "Env:$name" -Value $value
    }

    return (Test-Command "cl")
}

function Resolve-VcpkgRoot {
    param([string]$RequestedRoot)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $candidates += $RequestedRoot
    }

    $candidates += (Join-Path $projectRoot "vcpkg")

    $vcpkgCommand = Get-Command "vcpkg" -ErrorAction SilentlyContinue
    if ($null -ne $vcpkgCommand) {
        $candidates += (Split-Path -Parent $vcpkgCommand.Source)
    }

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
# Check Git for FetchContent dependencies
# ============================================================
Write-Step "Checking Git..."

$gitExe = Resolve-CommandPath "git" @(
    "$env:ProgramFiles\Git\cmd\git.exe",
    "$env:ProgramFiles\Git\bin\git.exe"
)
if ($null -eq $gitExe) {
    Write-Host "Git not found. It is required by CMake FetchContent." -ForegroundColor Red
    Write-Host "Run .\setup-windows.ps1, or install it with: winget install --id Git.Git --exact" -ForegroundColor Yellow
    exit 1
}
Add-ToolDirectoryToPath $gitExe

# ============================================================
# Check compiler
# ============================================================
Write-Step "Checking compiler..."

$useMsvc = $false
$useClang = $false
$cmakeGenerator = $null
$cmakeGeneratorPlatform = $null
$msvcParallelBuild = $false

if (-not (Test-Command "cl")) {
    $null = Import-MsvcEnvironment
}

if (Test-Command "cl") {
    $useMsvc = $true
    Write-Host "Found MSVC (cl)" -ForegroundColor Green
    if (Test-Command "ninja") {
        $cmakeGenerator = "Ninja"
    } elseif (Test-Command "nmake") {
        $cmakeGenerator = "NMake Makefiles"
    } else {
        $cmakeGenerator = "Visual Studio 17 2022"
        $cmakeGeneratorPlatform = "x64"
        $msvcParallelBuild = $true
    }
    Write-Host "Using CMake generator: $cmakeGenerator" -ForegroundColor Green
} elseif (Test-Command "clang") {
    $useClang = $true
    Write-Host "Found Clang" -ForegroundColor Green
} else {
    Write-Host "No compiler found. Install one:" -ForegroundColor Red
    Write-Host "  .\setup-windows.ps1" -ForegroundColor Yellow
    Write-Host "  winget install --id Microsoft.VisualStudio.2022.BuildTools --exact" -ForegroundColor Yellow
    Write-Host "  winget install --id LLVM.LLVM --exact" -ForegroundColor Yellow
    exit 1
}

# ============================================================
# Check CMake
# ============================================================
$global:CMakeExe = Resolve-CommandPath "cmake" @(
    "$env:ProgramFiles\CMake\bin\cmake.exe",
    "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
)
if ($null -eq $global:CMakeExe) {
    Write-Host "CMake not found. Install with: winget install Kitware.CMake" -ForegroundColor Red
    exit 1
}
Add-ToolDirectoryToPath $global:CMakeExe

# ============================================================
# Windows libcurl dependency
# ============================================================
$resolvedVcpkgRoot = Resolve-VcpkgRoot $VcpkgRoot
if ($null -ne $resolvedVcpkgRoot) {
    $env:VCPKG_ROOT = $resolvedVcpkgRoot
    Ensure-VcpkgCurl $resolvedVcpkgRoot
} else {
    Write-Host "vcpkg not found. libcurl is required for the HTTPS .proof check." -ForegroundColor Red
    Write-Host "Run .\setup-windows.ps1, or install vcpkg and curl manually:" -ForegroundColor Yellow
    Write-Host "  git clone https://github.com/microsoft/vcpkg.git vcpkg" -ForegroundColor Yellow
    Write-Host "  .\vcpkg\bootstrap-vcpkg.bat -disableMetrics" -ForegroundColor Yellow
    Write-Host "  .\vcpkg\vcpkg.exe install curl:x64-windows" -ForegroundColor Yellow
    Write-Host "Then set VCPKG_ROOT or pass -VcpkgRoot C:\path\to\vcpkg before building." -ForegroundColor Yellow
    exit 1
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
    $cachedGeneratorLine = Select-String -Path $cacheFile -Pattern "^CMAKE_GENERATOR:INTERNAL=" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $cachedGeneratorLine) {
        $cachedGenerator = $cachedGeneratorLine.Line.Substring("CMAKE_GENERATOR:INTERNAL=".Length)
        if ($cachedGenerator -ne $cmakeGenerator) {
            Write-Step "CMake generator changed ($cachedGenerator -> $cmakeGenerator), cleaning build directory..."
            Remove-Item -Recurse -Force $buildDir
        }
    }
}

if ((Test-Path $cacheFile) -and (Test-Path $rootCMake)) {
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
    "-Wno-dev"
    "-Wno-deprecated"
    "-DCMAKE_BUILD_TYPE=$Config"
)

if ($useMsvc) {
    $cmakeArgs += "-G", $cmakeGenerator
    if ($null -ne $cmakeGeneratorPlatform) {
        $cmakeArgs += "-A", $cmakeGeneratorPlatform
    }
}

if ($null -ne $resolvedVcpkgRoot) {
    $toolchainFile = Join-Path $resolvedVcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
    $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
}

& $global:CMakeExe @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed" -ForegroundColor Red
    exit 1
}

# ============================================================
# Build
# ============================================================
Write-Step "Building ($Config)..."

if ($useMsvc -and $msvcParallelBuild) {
    & $global:CMakeExe --build $buildDir --config $Config -- /m
} else {
    & $global:CMakeExe --build $buildDir --config $Config
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed" -ForegroundColor Red
    exit 1
}

# ============================================================
# Done
# ============================================================
Write-Step "Build complete!"
$outputDir = $buildDir
if ($cmakeGenerator -eq "Visual Studio 17 2022") {
    $outputDir = Join-Path $buildDir $Config
}
Write-Host ""
Write-Host "Executables are in: $outputDir\" -ForegroundColor Green
Write-Host ""
Write-Host "Run client:  & `"$outputDir\VoxPlace.exe`"" -ForegroundColor Yellow
Write-Host "Run server:  & `"$outputDir\VoxPlaceServer.exe`"" -ForegroundColor Yellow
