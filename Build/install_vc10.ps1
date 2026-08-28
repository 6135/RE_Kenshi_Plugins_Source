<#
.SYNOPSIS
    Provisions the Visual C++ 2010 (v100) x64 toolchain used to build the plugins.

.DESCRIPTION
    KenshiLib plugins must be compiled with the VS2010 compiler: they read game
    objects such as std::string and std::set across the DLL boundary, so the STL
    layout has to match Kenshi's own MSVC 10 build. GitHub hosted runners only
    ship the newest toolset, so this script provisions the compiler from the
    Windows SDK 7.1 media and stages it into a single self-contained directory
    that can be cached between runs.

    SDKSetup.exe itself refuses to run on modern Windows (it exits with code 2 on
    Windows Server 2022), so the media's MSIs are unpacked with administrative
    installs instead. That extracts the payload - compilers, CRT, STL and the
    Win32 headers and libraries - without touching the registry, which is all a
    direct cl.exe/link.exe invocation needs.

    On success the staged directory contains:
        VC\bin\amd64\cl.exe, VC\include, VC\lib\amd64
        SDK\Include, SDK\Lib\x64
        toolchain.json describing those paths
#>
[CmdletBinding()]
param(
    [string]$ToolchainDir = (Join-Path $env:USERPROFILE 'vc10-toolchain'),
    [string]$DownloadDir = (Join-Path $env:USERPROFILE 'vc10-downloads'),
    [string]$IsoUrl = 'https://download.microsoft.com/download/F/1/0/F10113F5-B750-4969-A255-274341AC6BCE/GRMSDKX_EN_DVD.iso'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Payloads worth unpacking: the x86/x64 compiler packages and the SDK itself.
# Itanium, samples and documentation are skipped to keep provisioning short.
$WantedMsi = '^(vc_std(amd64|x86)|winsdk)'
$SkipMsi = '(ia64|sample|intellidoc|redist|netfx|help)'

function Write-Stage([string]$Message) {
    Write-Host ""
    Write-Host "=== $Message" -ForegroundColor Cyan
}

function Get-StagedCompiler([string]$Root) {
    $cl = Join-Path $Root 'VC\bin\amd64\cl.exe'
    if (Test-Path $cl) { return $cl }
    return $null
}

function Expand-SdkMedia([string]$IsoRoot, [string]$Destination) {
    $all = Get-ChildItem -Path $IsoRoot -Recurse -Filter '*.msi' -ErrorAction SilentlyContinue
    Write-Host "MSI packages on the media:"
    $all | ForEach-Object { Write-Host "  $($_.Name)" }

    $wanted = $all | Where-Object { $_.Name -match $WantedMsi -and $_.Name -notmatch $SkipMsi }
    if (-not $wanted) { throw "No usable MSI packages found on the media." }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($msi in $wanted) {
        Write-Host "Extracting $($msi.Name)"
        $process = Start-Process -FilePath 'msiexec.exe' `
            -ArgumentList @('/a', "`"$($msi.FullName)`"", '/qn', "TARGETDIR=`"$Destination`"") `
            -Wait -PassThru -NoNewWindow
        if ($process.ExitCode -ne 0) {
            Write-Host "  msiexec exit code: $($process.ExitCode) (continuing)"
        }
    }
}

# The payload unpacks into "Program Files" and "Program Files(64)" trees, so the
# compiler, the CRT headers and the 64-bit libraries land in sibling copies of
# the same VC directory. Both are merged into one staged tree.
function Get-ExtractedVcRoots([string]$ExtractRoot) {
    return Get-ChildItem -Path $ExtractRoot -Recurse -Directory -Filter 'VC' -ErrorAction SilentlyContinue |
        Where-Object { $_.Parent.Name -like 'Microsoft Visual Studio*' } |
        Select-Object -ExpandProperty FullName
}

function Get-ExtractedSdkRoot([string]$ExtractRoot) {
    $header = Get-ChildItem -Path $ExtractRoot -Recurse -File -Filter 'windows.h' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $header) { return $null }
    return Split-Path (Split-Path $header.FullName -Parent) -Parent
}

function Copy-Merge([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source)) { return $false }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $Source '*') -Destination $Destination -Recurse -Force
    return $true
}

Write-Stage "Checking for a staged toolchain in $ToolchainDir"
if (Get-StagedCompiler $ToolchainDir) {
    Write-Host "Staged compiler already present."
} else {
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

    $extractRoot = Join-Path $DownloadDir 'extracted'
    Write-Stage "Mounting the media"
    $image = Mount-DiskImage -ImagePath $isoPath -PassThru
    try {
        $driveLetter = ($image | Get-Volume).DriveLetter
        $isoRoot = "${driveLetter}:\"
        Write-Host "Mounted at $isoRoot"
        Write-Stage "Unpacking the compiler and SDK payloads"
        Expand-SdkMedia $isoRoot $extractRoot
    } finally {
        Dismount-DiskImage -ImagePath $isoPath | Out-Null
    }

    $vcRoots = @(Get-ExtractedVcRoots $extractRoot)
    $sdkRoot = Get-ExtractedSdkRoot $extractRoot
    Write-Host "Visual C++ trees: $($vcRoots -join ', ')"
    Write-Host "Windows SDK tree: $sdkRoot"
    if (-not $vcRoots) { throw "No Visual C++ payload was unpacked from the media." }
    if (-not $sdkRoot) { throw "No Windows SDK headers were unpacked from the media." }

    Write-Stage "Staging the toolchain into $ToolchainDir"
    New-Item -ItemType Directory -Force -Path $ToolchainDir | Out-Null
    foreach ($vcRoot in $vcRoots) {
        foreach ($part in @('bin', 'include', 'lib')) {
            if (Copy-Merge (Join-Path $vcRoot $part) (Join-Path $ToolchainDir "VC\$part")) {
                Write-Host "  merged $vcRoot\$part"
            }
        }
    }
    foreach ($part in @('Include', 'Lib')) {
        if (Copy-Merge (Join-Path $sdkRoot $part) (Join-Path $ToolchainDir "SDK\$part")) {
            Write-Host "  merged $sdkRoot\$part"
        }
    }

    # The unpacked compiler is not registered, so its own runtime has to sit
    # beside it rather than being resolved from the system directory.
    $binDir = Join-Path $ToolchainDir 'VC\bin\amd64'
    foreach ($runtime in @('msvcr100.dll', 'msvcp100.dll')) {
        if (Test-Path (Join-Path $binDir $runtime)) { continue }
        $found = Get-ChildItem -Path $extractRoot -Recurse -File -Filter $runtime -ErrorAction SilentlyContinue |
            Sort-Object Length -Descending | Select-Object -First 1
        if ($found) {
            Copy-Item $found.FullName $binDir -Force
            Write-Host "  staged $runtime beside cl.exe"
        }
    }
}

Write-Stage "Verifying the staged compiler"
$cl = Get-StagedCompiler $ToolchainDir
if (-not $cl) {
    Write-Host "Contents of the staged tree:"
    Get-ChildItem $ToolchainDir -Recurse -Depth 3 -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName | Write-Host
    throw "No cl.exe under $ToolchainDir\VC\bin\amd64."
}

$required = @{
    'C++ standard library headers' = (Join-Path $ToolchainDir 'VC\include\string')
    'x64 CRT import library'       = (Join-Path $ToolchainDir 'VC\lib\amd64\msvcrt.lib')
    'Win32 headers'                = (Join-Path $ToolchainDir 'SDK\Include\windows.h')
    'x64 Win32 libraries'          = (Join-Path $ToolchainDir 'SDK\Lib\x64\kernel32.lib')
}
foreach ($entry in $required.GetEnumerator()) {
    if (-not (Test-Path $entry.Value)) {
        throw "Staged toolchain is missing the $($entry.Key): $($entry.Value)"
    }
    Write-Host "found $($entry.Key)"
}

# cl.exe writes its banner to stderr and its usage text to stdout, so the whole
# merged output is searched rather than just the first line.
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$clOutput = (& $cl 2>&1 | Out-String)
$ErrorActionPreference = $previousPreference

$versionMatch = [regex]::Match($clOutput, 'Version (\d+)\.(\d+)\.\S+\s+for\s+(\S+)')
if (-not $versionMatch.Success) {
    Write-Host "cl.exe output was:"
    Write-Host $clOutput
    throw "Could not read a version banner from the staged compiler."
}

$banner = $versionMatch.Value
$majorVersion = $versionMatch.Groups[1].Value
$target = $versionMatch.Groups[3].Value
Write-Host "cl.exe banner: $banner"
if ($majorVersion -ne '16') {
    throw "Expected the Visual C++ 2010 compiler (Version 16.xx) but found: $banner"
}
if ($target -ne 'x64') {
    throw "Expected the x64 compiler but found: $banner"
}

$toolchain = [ordered]@{
    cl      = $cl
    link    = (Join-Path $ToolchainDir 'VC\bin\amd64\link.exe')
    include = @((Join-Path $ToolchainDir 'VC\include'), (Join-Path $ToolchainDir 'SDK\Include'))
    lib     = @((Join-Path $ToolchainDir 'VC\lib\amd64'), (Join-Path $ToolchainDir 'SDK\Lib\x64'))
    banner  = "$banner"
}
$toolchainFile = Join-Path $ToolchainDir 'toolchain.json'
$toolchain | ConvertTo-Json -Depth 4 | Set-Content -Path $toolchainFile -Encoding UTF8
Write-Host "Wrote $toolchainFile"
