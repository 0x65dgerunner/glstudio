#Requires -Version 5.1
<#
.SYNOPSIS
    Builds GLStudio in Release mode and packages a Windows zip for distribution.

.DESCRIPTION
    Configures CMake, compiles glstudio.exe, stages Qt runtime files (via windeployqt
    during build), and creates a zip under dist/.

    With -Publish, uploads the zip to GitHub Releases using the GitHub CLI (gh).

.EXAMPLE
    .\scripts\release.ps1

.EXAMPLE
    .\scripts\release.ps1 -Version 0.1.0 -Publish

.EXAMPLE
    .\scripts\release.ps1 -QtPath "C:/Qt/6.8.2/msvc2022_64" -SkipBuild
#>
[CmdletBinding()]
param(
    [string] $Version,
    [string] $QtPath,
    [string] $BuildDir = "build",
    [string] $OutputDir = "dist",
    [switch] $Publish,
    [switch] $SkipBuild,
    [string] $ReleaseNotes = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step([string] $Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Resolve-ProjectVersion {
    param([string] $Root)
    $cmake = Join-Path $Root "CMakeLists.txt"
    if (-not (Test-Path $cmake)) {
        throw "CMakeLists.txt not found at $cmake"
    }
    $match = Select-String -Path $cmake -Pattern 'project\s*\(\s*\w+\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' |
        Select-Object -First 1
    if (-not $match) {
        throw "Could not read project VERSION from CMakeLists.txt"
    }
    return $match.Matches[0].Groups[1].Value
}

function Resolve-QtPath {
    param([string] $RequestedPath)
    if ($RequestedPath) {
        $normalized = $RequestedPath.Replace('\', '/')
        if (-not (Test-Path (Join-Path $normalized "lib/cmake/Qt6"))) {
            throw "Qt not found at -QtPath '$RequestedPath'"
        }
        return $normalized
    }

    if ($env:Qt6_DIR) {
        $fromEnv = (Resolve-Path (Join-Path $env:Qt6_DIR "../..")).Path.Replace('\', '/')
        if (Test-Path (Join-Path $fromEnv "lib/cmake/Qt6")) {
            return $fromEnv
        }
    }

    if ($env:CMAKE_PREFIX_PATH) {
        $prefix = ($env:CMAKE_PREFIX_PATH -split ';')[0].Replace('\', '/')
        if (Test-Path (Join-Path $prefix "lib/cmake/Qt6")) {
            return $prefix
        }
    }

    $candidates = @(
        "C:/Qt/6.8.2/msvc2022_64",
        "C:/Qt/6.7.3/msvc2022_64"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "lib/cmake/Qt6")) {
            return $candidate
        }
    }

    throw "Qt 6 not found. Pass -QtPath pointing to your Qt MSVC kit (e.g. C:/Qt/6.8.2/msvc2022_64)."
}

function Invoke-Build {
    param(
        [string] $Root,
        [string] $BuildDirectory,
        [string] $QtInstallPath
    )

    $running = Get-Process -Name "glstudio" -ErrorAction SilentlyContinue
    if ($running) {
        throw "Close glstudio before building (process is locking the output executable)."
    }

    Write-Step "Configuring CMake (Release)"
    $buildPath = Join-Path $Root $BuildDirectory
    & cmake -S $Root -B $buildPath `
        -DCMAKE_PREFIX_PATH="$QtInstallPath"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

    Write-Step "Building Release"
    & cmake --build $buildPath --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
}

function New-ReleasePackage {
    param(
        [string] $Root,
        [string] $BuildDirectory,
        [string] $DistDirectory,
        [string] $PackageVersion
    )

    $releaseDir = Join-Path $Root (Join-Path $BuildDirectory "Release")
    if (-not (Test-Path $releaseDir)) {
        throw "Release output not found at $releaseDir. Build the project first."
    }

    $mainExe = Join-Path $releaseDir "glstudio.exe"
    if (-not (Test-Path $mainExe)) {
        throw "glstudio.exe not found in $releaseDir"
    }

    $packageName = "glstudio-$PackageVersion-win64"
    $stageDir = Join-Path $Root (Join-Path $DistDirectory $packageName)
    $zipPath = Join-Path $Root (Join-Path $DistDirectory "$packageName.zip")

    Write-Step "Staging release files"
    if (Test-Path $stageDir) {
        Remove-Item -Recurse -Force $stageDir
    }
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

    $excludeNames = @(
        "*.pdb", "*.ilk", "*.exp", "*.lib", "*.obj",
        "CMakeCache.txt", "cmake_install.cmake"
    )

    Get-ChildItem -Path $releaseDir -Force | ForEach-Object {
        $skip = $false
        foreach ($pattern in $excludeNames) {
            if ($_.Name -like $pattern) {
                $skip = $true
                break
            }
        }
        if ($skip) { return }

        Copy-Item -Path $_.FullName -Destination $stageDir -Recurse -Force
    }

    foreach ($extraFile in @("LICENSE", "README.md")) {
        $source = Join-Path $Root $extraFile
        if (Test-Path $source) {
            Copy-Item -Path $source -Destination $stageDir -Force
        }
    }

    if (Test-Path $zipPath) {
        Remove-Item -Force $zipPath
    }

    Write-Step "Creating zip archive"
    Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force

    return @{
        StageDir = $stageDir
        ZipPath  = $zipPath
        Tag      = "v$PackageVersion"
    }
}

function Publish-GitHubRelease {
    param(
        [string] $Tag,
        [string] $ZipPath,
        [string] $Notes
    )

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        throw @"
GitHub CLI (gh) is not installed. Install it from https://cli.github.com/ and run:
  gh auth login
Then re-run with -Publish, or upload manually:
  $ZipPath
"@
    }

    Write-Step "Publishing GitHub release $Tag"
    $releaseArgs = @("release", "create", $Tag, $ZipPath, "--title", "GLStudio $Tag")
    if ($Notes) {
        $releaseArgs += @("--notes", $Notes)
    }
    else {
        $releaseArgs += @("--generate-notes")
    }

    & gh @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed with exit code $LASTEXITCODE" }
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $Version) {
    $Version = Resolve-ProjectVersion -Root $root
}

$qt = Resolve-QtPath -RequestedPath $QtPath

if (-not $SkipBuild) {
    Invoke-Build -Root $root -BuildDirectory $BuildDir -QtInstallPath $qt
}

$artifact = New-ReleasePackage -Root $root -BuildDirectory $BuildDir -DistDirectory $OutputDir -PackageVersion $Version

Write-Host ""
Write-Host "Release package ready:" -ForegroundColor Green
Write-Host "  Folder: $($artifact.StageDir)"
Write-Host "  Zip:    $($artifact.ZipPath)"

if ($Publish) {
    Publish-GitHubRelease -Tag $artifact.Tag -ZipPath $artifact.ZipPath -Notes $ReleaseNotes
    Write-Host ""
    Write-Host "Published GitHub release $($artifact.Tag)" -ForegroundColor Green
}
else {
    Write-Host ""
    Write-Host "To publish to GitHub Releases:" -ForegroundColor Yellow
    Write-Host "  .\scripts\release.ps1 -Version $Version -Publish"
    Write-Host "Or push a tag to trigger the GitHub Actions workflow:"
    Write-Host "  git tag v$Version"
    Write-Host "  git push origin v$Version"
}
