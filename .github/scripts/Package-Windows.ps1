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

    # Build installer executable
    if (-not (Test-Path "${BuildDir}")) {
        throw "Build directory not found: ${BuildDir}. Run the build script first."
    }

    Log-Group "Building installer executable for ${ProductName}..."

    Push-Location $BuildDir
    try {
        Write-Output "::group::Building Installer"
        Write-Output "Build Directory: ${BuildDir}"
        Write-Output "Configuration: ${Configuration}"
        Write-Output "Project Root: ${ProjectRoot}"
        
        # Build the installer target
        $BuildArgs = @(
            "--build", "."
            "--config", $Configuration
            "--target", "${ProductName}-installer"
            "--parallel"
        )
        
        if ($DebugPreference -eq 'Continue') {
            $BuildArgs += "--verbose"
        }
        
        Invoke-External cmake $BuildArgs
        Write-Output "::endgroup::"
        
        # Find the installer executable
        Write-Output "::group::Locating Installer Executable"
        $InstallerPattern = "${ProductName}-*-windows-x64-installer.exe"
        
        # Check common output locations
        $PossibleLocations = @(
            "${BuildDir}/${Configuration}",
            "${BuildDir}/Release",
            "${BuildDir}/RelWithDebInfo",
            "${BuildDir}/Debug",
            "${BuildDir}"
        )
        
        $InstallerFile = $null
        foreach ($Location in $PossibleLocations) {
            if (Test-Path $Location) {
                $Found = Get-ChildItem -Path $Location -Filter $InstallerPattern -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($Found) {
                    $InstallerFile = $Found
                    Write-Output "Found installer at: ${InstallerFile.FullName}"
                    break
                }
            }
        }
        
        if (-not $InstallerFile) {
            Write-Output "ERROR: Installer executable not found"
            Write-Output "Searched in:"
            $PossibleLocations | ForEach-Object { Write-Output "  - $_" }
            Write-Output "Looking for pattern: ${InstallerPattern}"
            throw "Installer executable not found. Expected: ${InstallerPattern}"
        }
        
        # Copy installer to release directory with proper name
        $DestPath = "${ProjectRoot}/release/${OutputName}-installer.exe"
        $DestDir = Split-Path -Path $DestPath -Parent
        if (-not (Test-Path $DestDir)) {
            New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
        }
        
        Copy-Item -Path $InstallerFile.FullName -Destination $DestPath -Force
        $InstallerSize = [math]::Round((Get-Item $DestPath).Length / 1KB, 2)
        Write-Output "Installer created: ${DestPath}"
        Write-Output "Installer size: ${InstallerSize} KB"
        
        if ($InstallerSize -lt 10) {
            Write-Output "WARNING: Installer size is very small (${InstallerSize} KB), which suggests the DLL may not be embedded"
        }
        
        Log-Output "Installer created: ${DestPath}"
        Write-Output "::endgroup::"
    } catch {
        Write-Output "::error::Failed to build installer: $_"
        throw
    } finally {
        Pop-Location
    }

    Log-Group
}

Package
