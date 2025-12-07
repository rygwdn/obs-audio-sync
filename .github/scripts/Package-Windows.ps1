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

    if ($Package) {
        # Generate NSIS installer using CPack
        Log-Group "Generating NSIS installer for ${ProductName}..."

        if (-not (Test-Path "${BuildDir}")) {
            throw "Build directory not found: ${BuildDir}. Run the build script first."
        }

        # Generate installer using CPack (CPack will handle staging via DESTDIR)
        Push-Location $BuildDir
        try {
            $CpackArgs = @(
                "-C", $Configuration
                "-G", "NSIS"
            )
            try {
                Invoke-External cpack $CpackArgs
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
            $InstallerFile = Get-ChildItem -Path "${ProjectRoot}/release" -Filter "${ProductName}-*-windows-x64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($InstallerFile) {
                $DestPath = "${ProjectRoot}/release/${OutputName}.exe"
                if ($InstallerFile.FullName -ne $DestPath) {
                    Move-Item -Path $InstallerFile.FullName -Destination $DestPath -Force
                }
                Log-Output "Installer created: ${DestPath}"
            } else {
                # Also check build directory as fallback
                $InstallerFile = Get-ChildItem -Path "${BuildDir}" -Filter "${ProductName}-*-windows-x64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($InstallerFile) {
                    $DestPath = "${ProjectRoot}/release/${OutputName}.exe"
                    Move-Item -Path $InstallerFile.FullName -Destination $DestPath -Force
                    Log-Output "Installer created: ${DestPath}"
                } else {
                    throw "Installer file not found after CPack execution. Expected: ${ProductName}-*-windows-x64.exe"
                }
            }
        } finally {
            Pop-Location
        }

        Log-Group
    } else {
        # Create zip archive (backward compatibility)
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
    }
}

Package
