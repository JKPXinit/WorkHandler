<#
.SYNOPSIS
Packages an existing WorkHandler Release build for Windows.

.DESCRIPTION
The script does not compile the project. It locates a configured Release build,
copies the application and QtDock runtime, runs the matching windeployqt, checks
the runtime layout, and optionally creates a SHA256-tagged zip archive.

.EXAMPLE
.\package.ps1

.EXAMPLE
.\package.ps1 -BuildDir .\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release -NoZip
#>
param(
    [string]$OutputPath = "./dist",
    [string]$BuildDir = "",
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$ExecutableName = "WorkHandler.exe",
    [string]$QtDeployTool = "",
    [switch]$NoZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$sourceRoot = Join-Path $projectRoot "Source"
$expectedProjectName = "WorkHandler"
$qtDockRuntimeName = "qtadvanceddocking.dll"

function Convert-ToProjectAbsolutePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Path))
}

function Test-SamePath {
    param(
        [Parameter(Mandatory = $true)][string]$Left,
        [Parameter(Mandatory = $true)][string]$Right
    )

    $leftPath = [System.IO.Path]::GetFullPath($Left).TrimEnd('\', '/')
    $rightPath = [System.IO.Path]::GetFullPath($Right).TrimEnd('\', '/')
    return [string]::Equals(
        $leftPath,
        $rightPath,
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)][string]$CachePath,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
        return $null
    }

    $pattern = "^" + [regex]::Escape($Name) + ":[^=]*=(.*)$"
    $match = Select-String -LiteralPath $CachePath -Pattern $pattern |
        Select-Object -First 1
    if (-not $match) {
        return $null
    }

    return $match.Matches[0].Groups[1].Value
}

function Resolve-ExecutablePath {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDirectory,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$BuildConfiguration
    )

    $candidates = @(
        (Join-Path $BuildDirectory $Name),
        (Join-Path (Join-Path $BuildDirectory $BuildConfiguration) $Name)
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    return $null
}

function Test-ReleaseConfiguration {
    param(
        [Parameter(Mandatory = $true)][string]$CachePath,
        [Parameter(Mandatory = $true)][string]$BuildConfiguration
    )

    $buildType = Get-CMakeCacheValue -CachePath $CachePath -Name "CMAKE_BUILD_TYPE"
    if ($buildType) {
        return [string]::Equals(
            $buildType,
            $BuildConfiguration,
            [System.StringComparison]::OrdinalIgnoreCase)
    }

    $configurationTypes = Get-CMakeCacheValue `
        -CachePath $CachePath `
        -Name "CMAKE_CONFIGURATION_TYPES"
    if (-not $configurationTypes) {
        return $false
    }

    return @($configurationTypes -split ";") -contains $BuildConfiguration
}

function Get-WorkHandlerBuild {
    param(
        [string]$RequestedBuildDir,
        [Parameter(Mandatory = $true)][string]$BuildConfiguration,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($RequestedBuildDir) {
        $buildDirectory = Convert-ToProjectAbsolutePath $RequestedBuildDir
        $cacheFiles = @(Join-Path $buildDirectory "CMakeCache.txt")
    } else {
        $buildRoot = Join-Path $projectRoot "build"
        if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
            throw "Build root does not exist: $buildRoot"
        }
        $cacheFiles = @(
            Get-ChildItem -LiteralPath $buildRoot -Recurse `
                -Filter "CMakeCache.txt" -File |
                Select-Object -ExpandProperty FullName
        )
    }

    $matches = @()
    foreach ($cachePath in $cacheFiles) {
        if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
            continue
        }

        $projectName = Get-CMakeCacheValue `
            -CachePath $cachePath -Name "CMAKE_PROJECT_NAME"
        $homeDirectory = Get-CMakeCacheValue `
            -CachePath $cachePath -Name "CMAKE_HOME_DIRECTORY"
        if ($projectName -ne $expectedProjectName -or
            -not $homeDirectory -or
            -not (Test-SamePath -Left $homeDirectory -Right $sourceRoot) -or
            -not (Test-ReleaseConfiguration `
                -CachePath $cachePath `
                -BuildConfiguration $BuildConfiguration)) {
            continue
        }

        $buildDirectory = Split-Path -Parent $cachePath
        $executablePath = Resolve-ExecutablePath `
            -BuildDirectory $buildDirectory `
            -Name $Name `
            -BuildConfiguration $BuildConfiguration
        if (-not $executablePath) {
            continue
        }

        $matches += [pscustomobject]@{
            BuildDirectory = [System.IO.Path]::GetFullPath($buildDirectory)
            CachePath = [System.IO.Path]::GetFullPath($cachePath)
            ExecutablePath = $executablePath
            LastWriteTime = (Get-Item -LiteralPath $executablePath).LastWriteTime
        }
    }

    if ($matches.Count -eq 0) {
        if ($RequestedBuildDir) {
            throw "The requested directory is not a configured WorkHandler $BuildConfiguration build containing $Name."
        }
        throw "Cannot find a configured WorkHandler $BuildConfiguration build containing $Name under $(Join-Path $projectRoot 'build')."
    }

    $orderedMatches = @($matches | Sort-Object LastWriteTime -Descending)
    if ($orderedMatches.Count -gt 1 -and -not $RequestedBuildDir) {
        Write-Warning "Multiple Release builds found; using the newest executable: $($orderedMatches[0].ExecutablePath)"
    }

    return $orderedMatches[0]
}

function Resolve-WinDeployQt {
    param(
        [Parameter(Mandatory = $true)][string]$CachePath,
        [string]$OverridePath
    )

    if ($OverridePath) {
        $resolvedOverride = Convert-ToProjectAbsolutePath $OverridePath
        if (-not (Test-Path -LiteralPath $resolvedOverride -PathType Leaf)) {
            throw "Qt deploy tool does not exist: $resolvedOverride"
        }
        return $resolvedOverride
    }

    $qmakePath = Get-CMakeCacheValue `
        -CachePath $CachePath -Name "QT_QMAKE_EXECUTABLE"
    if ($qmakePath) {
        $candidate = Join-Path (Split-Path -Parent $qmakePath) "windeployqt.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    $qtDirectory = Get-CMakeCacheValue -CachePath $CachePath -Name "Qt6_DIR"
    if ($qtDirectory) {
        $qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtDirectory))
        $candidate = Join-Path $qtRoot "bin/windeployqt.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    $pathCommand = Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue
    if ($pathCommand) {
        return $pathCommand.Source
    }

    throw "Cannot find windeployqt.exe for this build. Pass -QtDeployTool explicitly."
}

function Ensure-MsvcRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$CachePath,
        [Parameter(Mandatory = $true)][string]$PackageDirectory
    )

    $requiredRuntimeFiles = @(
        "msvcp140.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll"
    )
    $missingRuntimeFiles = @(
        $requiredRuntimeFiles | Where-Object {
            -not (Test-Path `
                -LiteralPath (Join-Path $PackageDirectory $_) `
                -PathType Leaf)
        }
    )
    if ($missingRuntimeFiles.Count -eq 0) {
        return
    }

    $compilerPath = Get-CMakeCacheValue `
        -CachePath $CachePath -Name "CMAKE_CXX_COMPILER"
    if (-not $compilerPath) {
        throw "MSVC runtime was not deployed and CMAKE_CXX_COMPILER is unavailable."
    }

    $normalizedCompilerPath = $compilerPath.Replace('/', '\')
    $toolsMarker = "\VC\Tools\MSVC\"
    $markerIndex = $normalizedCompilerPath.IndexOf(
        $toolsMarker,
        [System.StringComparison]::OrdinalIgnoreCase)
    if ($markerIndex -lt 0) {
        throw "MSVC runtime was not deployed and the compiler path is not a Visual Studio MSVC path: $compilerPath"
    }

    $vcRoot = $normalizedCompilerPath.Substring(
        0,
        $markerIndex + "\VC".Length)
    $toolVersionStart = $markerIndex + $toolsMarker.Length
    $toolVersionEnd = $normalizedCompilerPath.IndexOf('\', $toolVersionStart)
    $toolVersion = if ($toolVersionEnd -gt $toolVersionStart) {
        $normalizedCompilerPath.Substring(
            $toolVersionStart,
            $toolVersionEnd - $toolVersionStart)
    } else {
        ""
    }
    $versionPrefix = if ($toolVersion -match '^(\d+\.\d+)\.') {
        $Matches[1]
    } else {
        ""
    }

    $redistRoot = Join-Path $vcRoot "Redist/MSVC"
    if (-not (Test-Path -LiteralPath $redistRoot -PathType Container)) {
        throw "MSVC runtime was not deployed and the Visual Studio redist directory is missing: $redistRoot"
    }

    $redistCandidates = @(
        Get-ChildItem -LiteralPath $redistRoot -Directory |
            Where-Object {
                (-not $versionPrefix -or $_.Name -like "$versionPrefix.*") -and
                (Test-Path `
                    -LiteralPath (Join-Path $_.FullName "x64/Microsoft.VC143.CRT") `
                    -PathType Container)
            } |
            Sort-Object Name -Descending
    )
    if ($redistCandidates.Count -eq 0) {
        throw "Cannot find an x64 Microsoft.VC143.CRT directory matching compiler $toolVersion under $redistRoot"
    }

    $runtimeDirectory = Join-Path `
        $redistCandidates[0].FullName `
        "x64/Microsoft.VC143.CRT"
    Write-Host "Deploying MSVC runtime from: $runtimeDirectory"
    Get-ChildItem -LiteralPath $runtimeDirectory -Filter "*.dll" -File |
        Copy-Item -Destination $PackageDirectory -Force
}

function Assert-RequiredPackageFiles {
    param([Parameter(Mandatory = $true)][string]$PackageDirectory)

    $requiredRelativePaths = @(
        $ExecutableName,
        $qtDockRuntimeName,
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6HttpServer.dll",
        "Qt6Network.dll",
        "Qt6Sql.dll",
        "Qt6Widgets.dll",
        "msvcp140.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "platforms/qwindows.dll",
        "sqldrivers/qsqlite.dll"
    )

    foreach ($relativePath in $requiredRelativePaths) {
        $requiredPath = Join-Path $PackageDirectory $relativePath
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Required package file is missing: $requiredPath"
        }
    }
}

function Assert-NoRuntimeData {
    param([Parameter(Mandatory = $true)][string]$PackageDirectory)

    foreach ($directoryName in @("Config", "data", "log", "uploads", "translations")) {
        $runtimeDirectory = Join-Path $PackageDirectory $directoryName
        if (Test-Path -LiteralPath $runtimeDirectory) {
            throw "Runtime or translation data must not be shipped: $runtimeDirectory"
        }
    }

    $forbiddenFiles = @(
        Get-ChildItem -LiteralPath $PackageDirectory -Recurse -Force -File |
            Where-Object {
                $_.Name -eq "SoftwareConfig.xml" -or
                $_.Name -eq "Windowscf.ini" -or
                $_.Name -like "*.db" -or
                $_.Name -like "*.db-wal" -or
                $_.Name -like "*.db-shm" -or
                $_.Name -like "*.log" -or
                $_.Extension -eq ".qrc" -or
                $_.Extension -eq ".ts"
            }
    )

    if ($forbiddenFiles.Count -gt 0) {
        $forbiddenList = ($forbiddenFiles |
            Select-Object -ExpandProperty FullName) -join "`n"
        throw "Package contains generated data or source resources:`n$forbiddenList"
    }
}

if ([System.IO.Path]::GetFileName($ExecutableName) -ne $ExecutableName) {
    throw "ExecutableName must be a file name without a directory: $ExecutableName"
}

$build = Get-WorkHandlerBuild `
    -RequestedBuildDir $BuildDir `
    -BuildConfiguration $Configuration `
    -Name $ExecutableName
$buildDirAbs = $build.BuildDirectory
$cachePath = $build.CachePath
$sourceExe = $build.ExecutablePath
$sourceRuntimeDir = Split-Path -Parent $sourceExe
$qtDockRuntime = Join-Path $sourceRuntimeDir $qtDockRuntimeName

if (-not (Test-Path -LiteralPath $qtDockRuntime -PathType Leaf)) {
    throw "QtDock runtime was not copied by the CMake post-build step: $qtDockRuntime"
}

$outputDirAbs = Convert-ToProjectAbsolutePath $OutputPath
$projectName = Get-CMakeCacheValue -CachePath $cachePath -Name "CMAKE_PROJECT_NAME"
$projectVersion = Get-CMakeCacheValue -CachePath $cachePath -Name "CMAKE_PROJECT_VERSION"
if (-not $projectVersion) {
    $projectVersion = "dev"
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$packageName = "${projectName}_${projectVersion}_${Configuration}_${timestamp}"
$packageDir = Join-Path $outputDirAbs $packageName
$zipPath = Join-Path $outputDirAbs "${packageName}.zip"
$winDeployQt = Resolve-WinDeployQt `
    -CachePath $cachePath `
    -OverridePath $QtDeployTool

$gitCommand = Get-Command "git.exe" -ErrorAction SilentlyContinue
if ($gitCommand) {
    $gitStatus = & $gitCommand.Source -C $projectRoot status --short 2>$null
    if ($LASTEXITCODE -eq 0 -and $gitStatus) {
        Write-Warning "Working tree has changes. Packaging uses the existing build output and does not rebuild it."
    }
}

New-Item -ItemType Directory -Path $outputDirAbs -Force | Out-Null
if (Test-Path -LiteralPath $packageDir) {
    throw "Package directory already exists: $packageDir"
}

New-Item -ItemType Directory -Path $packageDir | Out-Null
Copy-Item -LiteralPath $sourceExe -Destination $packageDir -Force
Copy-Item -LiteralPath $qtDockRuntime -Destination $packageDir -Force

$packagedExecutable = Join-Path $packageDir $ExecutableName
$deployArguments = @(
    "--release",
    "--no-compiler-runtime",
    "--no-system-dxc-compiler",
    "--no-translations",
    "--dir", $packageDir,
    $packagedExecutable
)

Write-Host "Build directory: $buildDirAbs"
Write-Host "Executable: $sourceExe"
Write-Host "Using windeployqt: $winDeployQt"
& $winDeployQt @deployArguments
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Ensure-MsvcRuntime -CachePath $cachePath -PackageDirectory $packageDir
Assert-RequiredPackageFiles -PackageDirectory $packageDir
Assert-NoRuntimeData -PackageDirectory $packageDir

if (-not $NoZip) {
    Compress-Archive `
        -Path (Join-Path $packageDir "*") `
        -DestinationPath $zipPath `
        -Force
    $zipHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
    Write-Host "Package zip: $zipPath"
    Write-Host "SHA256: $($zipHash.Hash)"
}

Write-Host "Package directory: $packageDir"
