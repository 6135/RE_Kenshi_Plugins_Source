<#
.SYNOPSIS
    Provisions the Visual C++ 2010 (v100) x64 toolchain used to build the plugins.

.DESCRIPTION
    KenshiLib plugins must be compiled with the VS2010 compiler: they read game
    objects such as std::string and std::set across the DLL boundary, so the STL
    layout has to match Kenshi's own MSVC 10 build. GitHub hosted runners only
    ship the newest toolset, so this script installs the Windows SDK 7.1
    compilers (the freely redistributed VC10 compiler package) and stages them
    into a single self-contained directory that can be cached between runs.

    On success the staged directory contains:
        VC\bin\amd64\cl.exe, VC\include, VC\lib\amd64
        SDK\Include, SDK\Lib\x64
        toolchain.json describing those paths

.NOTES
    Every strategy is logged. If provisioning fails the log shows which stage
    failed and what was on disk at the time.
#>
[CmdletBinding()]
param(
    [string]$ToolchainDir = (Join-Path $env:USERPROFILE 'vc10-toolchain'),
    [string]$DownloadDir = (Join-Path $env:USERPROFILE 'vc10-downloads'),
    [string]$IsoUrl = 'https://download.microsoft.com/download/F/1/0/F10113F5-B750-4969-A255-274341AC6BCE/GRMSDKX_EN_DVD.iso'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Write-Stage([string]$Message) {
    Write-Host ""
    Write-Host "=== $Message" -ForegroundColor Cyan
}

function Get-StagedCompiler([string]$Root) {
    $cl = Join-Path $Root 'VC\bin\amd64\cl.exe'
    if (Test-Path $cl) { return $cl }
    return $null
}

function Find-InstalledVC10 {
    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio 10.0\VC'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio 10.0\VC')
    )
    foreach ($root in $roots) {
        if (Test-Path (Join-Path $root 'bin\amd64\cl.exe')) { return $root }
        if (Test-Path (Join-Path $root 'bin\x86_amd64\cl.exe')) { return $root }
    }
    return $null
}

function Find-InstalledSdk71 {
    $roots = @(
        (Join-Path $env:ProgramFiles 'Microsoft SDKs\Windows\v7.1'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs\Windows\v7.1')
    )
    foreach ($root in $roots) {
        if (Test-Path (Join-Path $root 'Include\windows.h')) { return $root }
    }
    return $null
}

# The SDK 7.1 setup refuses to install its compiler package when a newer VC++ 2010
# runtime is already present (the well known "return code 5100"). Hosted runners
# ship the SP1 runtime, so it has to go first.
function Remove-VC2010Redistributables {
    $keys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    $found = @()
    foreach ($key in $keys) {
        $found += Get-ItemProperty $key -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -match 'Microsoft Visual C\+\+ 2010.*Redistributable' }
    }
    if (-not $found) {
        Write-Host "No Visual C++ 2010 redistributable found - nothing to remove."
        return
    }
    foreach ($product in $found) {
        $code = $product.PSChildName
        Write-Host "Removing $($product.DisplayName) ($code)"
        $process = Start-Process -FilePath 'msiexec.exe' `
            -ArgumentList @('/x', $code, '/qn', '/norestart') `
            -Wait -PassThru -NoNewWindow
        Write-Host "  msiexec exit code: $($process.ExitCode)"
    }
}

function Invoke-SdkSetup([string]$SetupPath) {
    $arguments = @(
        '/q'
        '/norestart'
        '/ceip', 'off'
        '/features:OptionId.WindowsDesktopSoftwareDevelopmentKit,OptionId.VisualCppCompilers'
    )
    Write-Host "Running $SetupPath $($arguments -join ' ')"
    $process = Start-Process -FilePath $SetupPath -ArgumentList $arguments -Wait -PassThru -NoNewWindow
    Write-Host "SDKSetup exit code: $($process.ExitCode)"
    return $process.ExitCode
}

# Administrative installs extract an MSI's payload without touching the registry.
# Used as the fallback when SDKSetup itself refuses to run on a modern OS.
function Expand-CompilerMsi([string]$IsoRoot, [string]$Destination) {
    $candidates = Get-ChildItem -Path $IsoRoot -Recurse -Filter '*.msi' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match 'vc_|compiler|cpp' }
    if (-not $candidates) {
        Write-Host "No compiler MSI found on the media. Full listing of Setup:"
        Get-ChildItem -Path (Join-Path $IsoRoot 'Setup') -Recurse -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName | Write-Host
        return $false
    }
    foreach ($msi in $candidates) {
        Write-Host "Extracting $($msi.FullName)"
        $process = Start-Process -FilePath 'msiexec.exe' `
            -ArgumentList @('/a', "`"$($msi.FullName)`"", '/qn', "TARGETDIR=`"$Destination`"") `
            -Wait -PassThru -NoNewWindow
        Write-Host "  msiexec exit code: $($process.ExitCode)"
    }
    return $true
}

function Copy-Tree([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source)) {
        throw "Expected directory is missing: $Source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $Source '*') -Destination $Destination -Recurse -Force
}

Write-Stage "Checking for a staged toolchain in $ToolchainDir"
$staged = Get-StagedCompiler $ToolchainDir
if ($staged) {
    Write-Host "Staged compiler already present: $staged"
} else {
    Write-Stage "Looking for an installed VS2010 / Windows SDK 7.1"
    $vcRoot = Find-InstalledVC10
    $sdkRoot = Find-InstalledSdk71
    Write-Host "VC10 root: $vcRoot"
    Write-Host "SDK 7.1 root: $sdkRoot"

    if (-not $vcRoot -or -not $sdkRoot) {
        Write-Stage "Removing conflicting Visual C++ 2010 runtimes"
        Remove-VC2010Redistributables

        New-Item -ItemType Directory -Force -Path $DownloadDir | Out-Null
        $isoPath = Join-Path $DownloadDir 'GRMSDKX_EN_DVD.iso'
        if (-not (Test-Path $isoPath)) {
            Write-Stage "Downloading Windows SDK 7.1 media"
            Write-Host "URL: $IsoUrl"
            & curl.exe --fail --location --show-error --silent --retry 3 --output $isoPath $IsoUrl
            if ($LASTEXITCODE -ne 0) { throw "Failed to download the Windows SDK 7.1 ISO (curl exit $LASTEXITCODE)." }
        } else {
            Write-Stage "Using cached Windows SDK 7.1 media"
        }
        Write-Host "ISO size: $([math]::Round((Get-Item $isoPath).Length / 1MB, 1)) MB"

        Write-Stage "Mounting the media"
        $image = Mount-DiskImage -ImagePath $isoPath -PassThru
        try {
            $driveLetter = ($image | Get-Volume).DriveLetter
            $isoRoot = "${driveLetter}:\"
            Write-Host "Mounted at $isoRoot"
            Get-ChildItem $isoRoot | Select-Object -ExpandProperty Name | Write-Host

            $setup = Join-Path $isoRoot 'Setup\SDKSetup.exe'
            $installed = $false
            if (Test-Path $setup) {
                Write-Stage "Installing the SDK 7.1 compilers"
                $exitCode = Invoke-SdkSetup $setup
                $installed = ($exitCode -eq 0)
                if (-not $installed) {
                    Write-Host "SDKSetup failed. Recent setup logs:"
                    Get-ChildItem $env:TEMP -Filter '*SDK*' -ErrorAction SilentlyContinue |
                        Sort-Object LastWriteTime -Descending | Select-Object -First 5 |
                        ForEach-Object { Write-Host "--- $($_.FullName)"; Get-Content $_.FullName -Tail 40 | Write-Host }
                }
            } else {
                Write-Host "SDKSetup.exe not found on the media."
            }

            if (-not $installed) {
                Write-Stage "Falling back to extracting the compiler payload"
                $extractRoot = Join-Path $DownloadDir 'extracted'
                if (Expand-CompilerMsi $isoRoot $extractRoot) {
                    Write-Host "Extracted tree:"
                    Get-ChildItem $extractRoot -Recurse -Filter 'cl.exe' -ErrorAction SilentlyContinue |
                        Select-Object -ExpandProperty FullName | Write-Host
                }
            }
        } finally {
            Dismount-DiskImage -ImagePath $isoPath | Out-Null
        }

        $vcRoot = Find-InstalledVC10
        $sdkRoot = Find-InstalledSdk71
        Write-Host "VC10 root after install: $vcRoot"
        Write-Host "SDK 7.1 root after install: $sdkRoot"
    }

    if (-not $vcRoot) { throw "The Visual C++ 2010 compilers are not available after provisioning." }
    if (-not $sdkRoot) { throw "The Windows SDK 7.1 headers are not available after provisioning." }

    Write-Stage "Staging the toolchain into $ToolchainDir"
    New-Item -ItemType Directory -Force -Path $ToolchainDir | Out-Null
    Copy-Tree (Join-Path $vcRoot 'bin') (Join-Path $ToolchainDir 'VC\bin')
    Copy-Tree (Join-Path $vcRoot 'include') (Join-Path $ToolchainDir 'VC\include')
    Copy-Tree (Join-Path $vcRoot 'lib') (Join-Path $ToolchainDir 'VC\lib')
    Copy-Tree (Join-Path $sdkRoot 'Include') (Join-Path $ToolchainDir 'SDK\Include')
    Copy-Tree (Join-Path $sdkRoot 'Lib') (Join-Path $ToolchainDir 'SDK\Lib')
}

Write-Stage "Verifying the staged compiler"
$cl = Get-StagedCompiler $ToolchainDir
if (-not $cl) { throw "No cl.exe under $ToolchainDir\VC\bin\amd64." }

$banner = & $cl 2>&1 | Select-Object -First 1
Write-Host "cl.exe banner: $banner"
if ($banner -notmatch 'Version 16\.00') {
    throw "Expected the Visual C++ 2010 compiler (Version 16.00) but found: $banner"
}
if ($banner -notmatch 'x64') {
    throw "Expected the x64 compiler but found: $banner"
}

$libDir = Join-Path $ToolchainDir 'VC\lib\amd64'
$sdkLibDir = Join-Path $ToolchainDir 'SDK\Lib\x64'
foreach ($required in @($libDir, $sdkLibDir, (Join-Path $ToolchainDir 'VC\include'), (Join-Path $ToolchainDir 'SDK\Include'))) {
    if (-not (Test-Path $required)) { throw "Staged toolchain is incomplete, missing: $required" }
}

$toolchain = [ordered]@{
    cl      = $cl
    link    = (Join-Path $ToolchainDir 'VC\bin\amd64\link.exe')
    include = @((Join-Path $ToolchainDir 'VC\include'), (Join-Path $ToolchainDir 'SDK\Include'))
    lib     = @($libDir, $sdkLibDir)
    banner  = "$banner"
}
$toolchainFile = Join-Path $ToolchainDir 'toolchain.json'
$toolchain | ConvertTo-Json -Depth 4 | Set-Content -Path $toolchainFile -Encoding UTF8
Write-Host "Wrote $toolchainFile"

if ($env:GITHUB_OUTPUT) {
    "toolchain=$toolchainFile" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
}
