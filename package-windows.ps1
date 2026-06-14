param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$DistDir = (Join-Path $PSScriptRoot "dist"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$configLower = $Config.ToLowerInvariant()
$buildDir = Join-Path $projectRoot "build\win-$configLower"
$packageName = "VoxPlace-win-x64-$configLower"
$packageDir = Join-Path $DistDir $packageName
$zipPath = Join-Path $DistDir "$packageName.zip"

function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Assert-UnderDirectory {
    param(
        [string]$Path,
        [string]$Parent
    )

    $resolvedParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside ${resolvedParent}: $resolvedPath"
    }
}

if (-not $SkipBuild) {
    Write-Step "Building $Config..."
    & (Join-Path $projectRoot "build.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path $buildDir)) {
    throw "Build directory not found: $buildDir"
}

$requiredFiles = @(
    "VoxPlace.exe",
    "VoxPlaceServer.exe"
)

foreach ($file in $requiredFiles) {
    $path = Join-Path $buildDir $file
    if (-not (Test-Path $path)) {
        throw "Missing build output: $path"
    }
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Assert-UnderDirectory -Path $packageDir -Parent $DistDir

if (Test-Path $packageDir) {
    Write-Step "Cleaning package directory..."
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Write-Step "Staging portable package..."
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

$filesToCopy = @(
    "VoxPlace.exe",
    "VoxPlaceServer.exe",
    "VoxPlacePregen.exe"
)

foreach ($file in $filesToCopy) {
    $source = Join-Path $buildDir $file
    if (Test-Path $source) {
        Copy-Item -LiteralPath $source -Destination $packageDir -Force
    }
}

Get-ChildItem -LiteralPath $buildDir -Filter "*.dll" -File -ErrorAction SilentlyContinue |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $packageDir -Force
    }

foreach ($directory in @("assets", "shader")) {
    $source = Join-Path $buildDir $directory
    if (Test-Path $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $packageDir $directory) -Recurse -Force
    }
}

@"
VoxPlace Windows package

Run client:
  .\VoxPlace.exe

Run server:
  .\VoxPlaceServer.exe

If a target machine complains about VCRUNTIME/MSVCP DLLs, install the
Microsoft Visual C++ Redistributable 2015-2022 x64. It is a runtime only,
not a compiler.
"@ | Set-Content -LiteralPath (Join-Path $packageDir "README_RUN.txt") -Encoding UTF8

Write-Step "Creating zip..."
Push-Location $packageDir
try {
    Compress-Archive -Path * -DestinationPath $zipPath -Force
} finally {
    Pop-Location
}

Write-Step "Package complete!"
Write-Host "Folder: $packageDir" -ForegroundColor Green
Write-Host "Zip:    $zipPath" -ForegroundColor Green
