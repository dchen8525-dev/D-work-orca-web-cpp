# Install the P1+ dependencies into the local vcpkg tree.
# English only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.
#
# Run this in a NORMAL PowerShell window (outside the agent sandbox).
# Two issues were found and handled here:
#   - the proxy cannot reach raw.githubusercontent.com for Boost's LICENSE;
#     the file is already cached in vcpkg downloads (verified SHA512).
#   - vcpkg's Ninja child processes need the Windows SDK rc.exe on PATH;
#     the script locates and injects it below.
#
# x64-windows-static is required: the project links the static CRT (/MT) so
# the release exe is a single file with no VC++ redist and no OpenSSL DLLs.

param(
    [string]$VcpkgRoot = "D:\work\vcpkg",
    [string]$Triplet = "x64-windows-static"
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

& $vcpkg install --triplet $Triplet `
    openssl nlohmann-json libsodium boost-beast
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }

Write-Host "[orca] dependencies installed for $Triplet"
Write-Host "[orca] build with: .\scripts\build.ps1 -Triplet $Triplet -RequireNetworkDeps"
