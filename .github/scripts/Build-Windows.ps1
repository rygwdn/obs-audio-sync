[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Build-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    # Add vcpkg's Qt6 to CMAKE_PREFIX_PATH for Qt Test libraries
    # The obs-deps-qt6 prebuilds don't include Qt Test
    $VcpkgRoot = $env:VCPKG_ROOT
    if ( -not $VcpkgRoot ) {
      # Try common vcpkg locations
      $PossiblePaths = @(
        "$env:ProgramFiles\vcpkg",
        "$env:ProgramFiles(x86)\vcpkg",
        "$env:LOCALAPPDATA\vcpkg",
        "$env:USERPROFILE\vcpkg"
      )
      
      foreach ( $Path in $PossiblePaths ) {
        if ( Test-Path "$Path\vcpkg.exe" ) {
          $VcpkgRoot = $Path
          break
        }
      }
    }

    if ( $VcpkgRoot -and (Test-Path "$VcpkgRoot\installed\x64-windows\share\qt6") ) {
      $VcpkgQt6Path = "$VcpkgRoot\installed\x64-windows"
      if ( $env:CMAKE_PREFIX_PATH ) {
        $env:CMAKE_PREFIX_PATH = "$env:CMAKE_PREFIX_PATH;$VcpkgQt6Path"
      } else {
        $env:CMAKE_PREFIX_PATH = $VcpkgQt6Path
      }
      Write-Host "Added vcpkg Qt6 to CMAKE_PREFIX_PATH: $VcpkgQt6Path"
    }

    $CmakeArgs = @('--preset', "windows-ci-${Target}")
    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CmakeInstallArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        '--preset', "windows-${Target}"
        '--config', $Configuration
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CmakeInstallArgs += @(
        '--install', "build_${Target}"
        '--prefix', "${ProjectRoot}/release/${Configuration}"
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
