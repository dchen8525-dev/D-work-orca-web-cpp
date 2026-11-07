# Install the delivered (static) dependency set into the local vcpkg tree.
# English only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.
#
# Run this in a NORMAL PowerShell window (outside the agent sandbox).
# Two issues were found and handled here:
#   - the proxy cannot reach raw.githubusercontent.com for Boost's LICENSE;
#     the file is already cached in vcpkg downloads (verified SHA512).
#   - vcpkg's Ninja child processes need the Windows SDK rc.exe on PATH;
#     the script locates and injects it below.
#
# x64-windows-static is required: the gateway links static Qt + static CRT
# (/MT), so the released orca-gateway-<version>-windows-amd64.exe is one file
# with no VC++ redist, no Qt DLLs and no platform plugin directory.
#
# NOTE: qtbase on x64-windows-static is compiled FROM SOURCE. The first run can
# take 1-2 hours and several GB of disk. vcpkg caches the built packages, so
# subsequent runs are fast. Use -SkipQt to install only the network libraries
# while waiting for a full build.

param(
    [string]$VcpkgRoot = "D:\work\vcpkg",
    [string]$Triplet = "x64-windows-static",
    [switch]$SkipQt
)

$ErrorActionPreference = "Stop"
$vcpkg = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path $vcpkg)) { throw "vcpkg.exe not found: $vcpkg" }

# vcpkg's Ninja-based compiler probes call `rc` through cmd.exe. When the
# Windows SDK bin directory is missing from PATH they fail with
# "RC Pass 1: ... no such file or directory" (MSB6001 / CMakeTestCXXCompiler).
# Find the newest installed SDK and prepend it for this process only.
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
if (Test-Path $sdkRoot) {
    $sdkBin = Get-ChildItem -Path $sdkRoot -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName "x64\rc.exe") } |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($sdkBin) {
        $rcDir = Join-Path $sdkBin.FullName "x64"
        $env:PATH = "$rcDir;$env:PATH"
        Write-Host "[orca] added SDK resource compiler to PATH: $rcDir"
    }
}

$packages = @("openssl", "nlohmann-json", "libsodium", "boost-beast")
if (-not $SkipQt) {
    $packages += "qtbase"
    Write-Host "[orca] building static Qt from source; this can take 1-2 hours..."
}

& $vcpkg install --triplet $Triplet @packages
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }

Write-Host "[orca] dependencies installed for $Triplet"
Write-Host "[orca] build with: .\scripts\build.ps1 -Triplet $Triplet -RequireNetworkDeps -RequireQt"
