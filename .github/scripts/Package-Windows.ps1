[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [switch] $Package
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $BuildDir = "${ProjectRoot}/build_${Target}"

    # Always create zip archive
    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    # Always generate NSIS installer using CPack
    if (-not (Test-Path "${BuildDir}")) {
        throw "Build directory not found: ${BuildDir}. Run the build script first."
    }

    Log-Group "Generating NSIS installer for ${ProductName}..."

    # Generate installer using CPack (CPack will handle staging via DESTDIR)
    Push-Location $BuildDir
    try {
        # Add verbose logging to debug CPack staging issues
        $CpackArgs = @(
            "-C", $Configuration
            "-G", "NSIS"
            "--verbose"
            "--debug"
        )
        
        # Log CPack configuration before running
        Write-Output "::group::CPack Configuration"
        Write-Output "Build Directory: ${BuildDir}"
        Write-Output "Configuration: ${Configuration}"
        Write-Output "Project Root: ${ProjectRoot}"
        
        # Check if install rules exist
        $InstallManifest = "${BuildDir}/install_manifest.txt"
        if (Test-Path $InstallManifest) {
            Write-Output "Install manifest found: ${InstallManifest}"
            Write-Output "Install manifest contents:"
            Get-Content $InstallManifest | ForEach-Object { Write-Output "  $_" }
        } else {
            Write-Output "WARNING: Install manifest not found at ${InstallManifest}"
            Write-Output "This suggests cmake --install was not run or files were not installed."
        }
        
        # Check for installed files in release directory
        $ReleaseDir = "${ProjectRoot}/release/${Configuration}"
        if (Test-Path $ReleaseDir) {
            Write-Output "Release directory exists: ${ReleaseDir}"
            Write-Output "Release directory contents:"
            Get-ChildItem -Path $ReleaseDir -Recurse | ForEach-Object {
                Write-Output "  $($_.FullName.Replace($ProjectRoot, '.'))"
            }
        } else {
            Write-Output "WARNING: Release directory not found at ${ReleaseDir}"
        }
        
        # Check CPack variables from CMake cache
        Write-Output "Checking CPack configuration..."
        $CmakeCache = "${BuildDir}/CMakeCache.txt"
        if (Test-Path $CmakeCache) {
            $CpackVars = Get-Content $CmakeCache | Select-String -Pattern "^CPACK_" | Select-Object -First 20
            Write-Output "CPack variables from CMakeCache:"
            $CpackVars | ForEach-Object { Write-Output "  $_" }
        }
        
        Write-Output "::endgroup::"
        
        try {
            Write-Output "::group::Running CPack"
            Invoke-External cpack $CpackArgs
            Write-Output "::endgroup::"
            
            # Log CPack staging results
            Write-Output "::group::CPack Staging Results"
            $CpackStagingDir = "${BuildDir}/_CPack_Packages/win64/NSIS"
            if (Test-Path $CpackStagingDir) {
                Write-Output "CPack staging directory found: ${CpackStagingDir}"
                Write-Output "Staging directory contents:"
                Get-ChildItem -Path $CpackStagingDir -Recurse | ForEach-Object {
                    $size = if ($_.PSIsContainer) { "<DIR>" } else { "$([math]::Round($_.Length / 1KB, 2)) KB" }
                    Write-Output "  $($_.FullName.Replace($BuildDir, '.')) [$size]"
                }
                
                # Check for the actual package directory
                $PackageDir = Get-ChildItem -Path $CpackStagingDir -Directory -Filter "*" | Select-Object -First 1
                if ($PackageDir) {
                    Write-Output "Package directory: ${PackageDir.FullName}"
                    Write-Output "Package directory contents:"
                    Get-ChildItem -Path $PackageDir.FullName -Recurse | ForEach-Object {
                        $size = if ($_.PSIsContainer) { "<DIR>" } else { "$([math]::Round($_.Length / 1KB, 2)) KB" }
                        Write-Output "  $($_.FullName.Replace($PackageDir.FullName, '.')) [$size]"
                    }
                } else {
                    Write-Output "WARNING: No package directory found in staging area"
                }
            } else {
                Write-Output "ERROR: CPack staging directory not found at ${CpackStagingDir}"
                Write-Output "This suggests CPack did not stage any files."
            }
            Write-Output "::endgroup::"
        } catch {
            # CPack failed - output NSIS log if it exists
            $NSISLogPath = "${BuildDir}/_CPack_Packages/win64/NSIS/NSISOutput.log"
            Write-Output "::error::CPack failed. Checking for NSIS log..."
            if (Test-Path $NSISLogPath) {
                Write-Output "::error::=========================================="
                Write-Output "::error::NSIS Output Log:"
                Write-Output "::error::=========================================="
                # Read and output the entire log file
                $logContent = Get-Content $NSISLogPath -Raw
                # Output each line with error prefix so it's visible in CI
                $logContent -split "`r?`n" | ForEach-Object {
                    if ($_.Trim().Length -gt 0) {
                        Write-Output "::error::$($_)"
                    }
                }
                Write-Output "::error::=========================================="
            } else {
                Write-Output "::error::NSIS log not found at: ${NSISLogPath}"
                # Try to find any NSIS-related files for debugging
                $NSISDir = "${BuildDir}/_CPack_Packages/win64/NSIS"
                if (Test-Path $NSISDir) {
                    Write-Output "::error::NSIS directory contents:"
                    Get-ChildItem -Path $NSISDir | ForEach-Object { Write-Output "::error::  $($_.Name)" }
                } else {
                    Write-Output "::error::NSIS directory not found at: ${NSISDir}"
                }
            }
            throw
        }

        # Find and move installer to release directory with proper name
        # CPack outputs to the directory specified by CPACK_OUTPUT_FILE_PREFIX (release/)
        Write-Output "::group::Installer File Check"
        $InstallerFile = Get-ChildItem -Path "${ProjectRoot}/release" -Filter "${ProductName}-*-windows-x64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($InstallerFile) {
            $DestPath = "${ProjectRoot}/release/${OutputName}.exe"
            if ($InstallerFile.FullName -ne $DestPath) {
                Move-Item -Path $InstallerFile.FullName -Destination $DestPath -Force
            }
            $InstallerSize = [math]::Round((Get-Item $DestPath).Length / 1KB, 2)
            Write-Output "Installer created: ${DestPath}"
            Write-Output "Installer size: ${InstallerSize} KB"
            if ($InstallerSize -lt 1) {
                Write-Output "WARNING: Installer size is very small (${InstallerSize} KB), which suggests no files were packaged"
            }
            Log-Output "Installer created: ${DestPath}"
        } else {
            # Also check build directory as fallback
            Write-Output "Installer not found in release directory, checking build directory..."
            $InstallerFile = Get-ChildItem -Path "${BuildDir}" -Filter "${ProductName}-*-windows-x64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($InstallerFile) {
                $DestPath = "${ProjectRoot}/release/${OutputName}.exe"
                Move-Item -Path $InstallerFile.FullName -Destination $DestPath -Force
                $InstallerSize = [math]::Round((Get-Item $DestPath).Length / 1KB, 2)
                Write-Output "Installer found in build directory and moved to: ${DestPath}"
                Write-Output "Installer size: ${InstallerSize} KB"
                Log-Output "Installer created: ${DestPath}"
            } else {
                Write-Output "ERROR: Installer file not found in release or build directory"
                Write-Output "Searched in:"
                Write-Output "  - ${ProjectRoot}/release"
                Write-Output "  - ${BuildDir}"
                Write-Output "Looking for: ${ProductName}-*-windows-x64.exe"
                throw "Installer file not found after CPack execution. Expected: ${ProductName}-*-windows-x64.exe"
            }
        }
        Write-Output "::endgroup::"
    } finally {
        Pop-Location
    }

    Log-Group
}

Package
