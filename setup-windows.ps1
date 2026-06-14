param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$VcpkgRoot = (Join-Path $PSScriptRoot "vcpkg"),
    [switch]$Clean,
    [switch]$NoBuild,
    [switch]$SkipPackageInstall
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot

function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Ok {
    param([string]$Message)
    Write-Host $Message -ForegroundColor Green
}

function Resolve-Tool {
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

function Require-Winget {
    $winget = Resolve-Tool "winget" @(
        "$env:LOCALAPPDATA\Microsoft\WindowsApps\winget.exe",
        "$env:ProgramFiles\WindowsApps\Microsoft.DesktopAppInstaller_8wekyb3d8bbwe\winget.exe"
    )
    if ($null -eq $winget) {
        throw "winget is not available. Install 'App Installer' from the Microsoft Store, then rerun .\setup-windows.ps1."
    }

    Add-ToolDirectoryToPath $winget
    return $winget
}

function Install-WingetPackage {
    param(
        [string]$Id,
        [string]$DisplayName,
        [string]$Override = $null
    )

    $winget = Require-Winget
    Write-Step "Installing $DisplayName..."

    $arguments = @(
        "install",
        "--id", $Id,
        "--exact",
        "--source", "winget",
        "--accept-package-agreements",
        "--accept-source-agreements"
    )

    if (-not [string]::IsNullOrWhiteSpace($Override)) {
        $arguments += @("--override", $Override)
    } else {
        $arguments += "--silent"
    }

    & $winget @arguments 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed while installing $DisplayName."
    }
}

function Ensure-Git {
    $git = Resolve-Tool "git" @(
        "$env:ProgramFiles\Git\cmd\git.exe",
        "$env:ProgramFiles\Git\bin\git.exe"
    )

    if ($null -eq $git) {
        if ($SkipPackageInstall) {
            throw "Git is missing and -SkipPackageInstall was set."
        }
        Install-WingetPackage "Git.Git" "Git"
        $git = Resolve-Tool "git" @(
            "$env:ProgramFiles\Git\cmd\git.exe",
            "$env:ProgramFiles\Git\bin\git.exe"
        )
    }

    if ($null -eq $git) {
        throw "Git was installed but could not be found in this PowerShell session. Open a new terminal and rerun the script."
    }

    Add-ToolDirectoryToPath $git
    Write-Ok "Git: $git"
    return $git
}

function Ensure-CMake {
    $cmake = Resolve-Tool "cmake" @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
    )

    if ($null -eq $cmake) {
        if ($SkipPackageInstall) {
            throw "CMake is missing and -SkipPackageInstall was set."
        }
        Install-WingetPackage "Kitware.CMake" "CMake"
        $cmake = Resolve-Tool "cmake" @(
            "$env:ProgramFiles\CMake\bin\cmake.exe",
            "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
        )
    }

    if ($null -eq $cmake) {
        throw "CMake was installed but could not be found in this PowerShell session. Open a new terminal and rerun the script."
    }

    Add-ToolDirectoryToPath $cmake
    Write-Ok "CMake: $cmake"
    return $cmake
}

function Ensure-MsvcBuildTools {
    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    $knownInstallations = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools"
    )
    function Resolve-KnownMsvcBuildTools {
        foreach ($candidate in $knownInstallations) {
            $vsDevCmd = Join-Path $candidate "Common7\Tools\VsDevCmd.bat"
            $msvcTools = Join-Path $candidate "VC\Tools\MSVC"
            if ((Test-Path $vsDevCmd) -and (Test-Path $msvcTools)) {
                return $candidate
            }
        }

        return $null
    }

    $vswhere = Resolve-Tool "vswhere" $vswhereCandidates
    $installationPath = $null

    if ($null -ne $vswhere) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0) {
            $installationPath = $null
        }
    }

    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        $installationPath = Resolve-KnownMsvcBuildTools
    }

    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        if ($SkipPackageInstall) {
            throw "MSVC Build Tools are missing and -SkipPackageInstall was set."
        }

        Write-Host "Visual Studio Build Tools may open a UAC prompt and can take several minutes." -ForegroundColor Yellow
        Install-WingetPackage `
            "Microsoft.VisualStudio.2022.BuildTools" `
            "Visual Studio 2022 Build Tools" `
            "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

        $vswhere = Resolve-Tool "vswhere" $vswhereCandidates
        if ($null -ne $vswhere) {
            $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        }

        if ([string]::IsNullOrWhiteSpace($installationPath)) {
            $installationPath = Resolve-KnownMsvcBuildTools
        }
    }

    $deadline = (Get-Date).AddMinutes(20)
    while ([string]::IsNullOrWhiteSpace($installationPath) -and (Get-Date) -lt $deadline) {
        $setupProcesses = @(Get-Process -Name "setup" -ErrorAction SilentlyContinue)
        if ($setupProcesses.Count -gt 0) {
            Write-Host "Visual Studio installer is still running; waiting..." -ForegroundColor Yellow
            Start-Sleep -Seconds 30
        } else {
            Start-Sleep -Seconds 10
        }

        $vswhere = Resolve-Tool "vswhere" $vswhereCandidates
        if ($null -ne $vswhere) {
            $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($LASTEXITCODE -ne 0) {
                $installationPath = $null
            }
        }

        if ([string]::IsNullOrWhiteSpace($installationPath)) {
            $installationPath = Resolve-KnownMsvcBuildTools
        }
    }

    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        throw "MSVC Build Tools were not found. Reboot if the installer just finished, then rerun .\setup-windows.ps1."
    }

    Write-Ok "MSVC Build Tools: $installationPath"
}

function Ensure-Vcpkg {
    param(
        [string]$GitExe,
        [string]$Root
    )

    if (-not [IO.Path]::IsPathRooted($Root)) {
        $Root = Join-Path $projectRoot $Root
    }

    $Root = [IO.Path]::GetFullPath($Root)
    $toolchain = Join-Path $Root "scripts\buildsystems\vcpkg.cmake"

    if (-not (Test-Path $toolchain)) {
        Write-Step "Cloning vcpkg..."
        & $GitExe clone https://github.com/microsoft/vcpkg.git $Root 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to clone vcpkg."
        }
    } else {
        Write-Ok "vcpkg source: $Root"
    }

    $bootstrap = Join-Path $Root "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrap)) {
        throw "vcpkg bootstrap script not found at $bootstrap."
    }

    $vcpkgExe = Join-Path $Root "vcpkg.exe"
    if (-not (Test-Path $vcpkgExe)) {
        Write-Step "Bootstrapping vcpkg..."
        & $bootstrap -disableMetrics 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to bootstrap vcpkg."
        }
    }

    Write-Step "Installing curl:x64-windows with vcpkg..."
    & $vcpkgExe install "curl:x64-windows" 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install curl:x64-windows with vcpkg."
    }

    $env:VCPKG_ROOT = $Root
    Write-Ok "VCPKG_ROOT: $Root"
    return $Root
}

if (-not $IsWindows -and $PSVersionTable.PSEdition -eq "Core") {
    throw "This script is intended for Windows."
}

Write-Step "Preparing VoxPlace Windows toolchain..."
$git = Ensure-Git
$null = Ensure-CMake
Ensure-MsvcBuildTools
$resolvedVcpkgRoot = Ensure-Vcpkg -GitExe $git -Root $VcpkgRoot

if ($NoBuild) {
    Write-Step "Setup complete."
    Write-Host "To build later, run:" -ForegroundColor Yellow
    Write-Host ".\build.ps1 -Config $Config -VcpkgRoot `"$resolvedVcpkgRoot`"" -ForegroundColor Yellow
    exit 0
}

Write-Step "Building VoxPlace..."
$buildScript = Join-Path $projectRoot "build.ps1"
if ($Clean) {
    & $buildScript -Config $Config -VcpkgRoot $resolvedVcpkgRoot -Clean
} else {
    & $buildScript -Config $Config -VcpkgRoot $resolvedVcpkgRoot
}
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
