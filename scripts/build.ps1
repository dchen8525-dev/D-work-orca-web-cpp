# Build orca-cpp on Windows with MSVC.
# English only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.
#
# NOTE: MSBuild (used by the Visual Studio generator) throws MSB6001 when the
# environment contains BOTH HTTPS_PROXY and https_proxy, because .NET builds a
# case-insensitive Hashtable of environment variables. We drop the lowercase
# aliases before invoking cmake/ctest. The uppercase proxy variables stay.

param(
    [string]$BuildType = "Release",
    [string]$Triplet = "x64-windows",
    [string]$VcpkgRoot = "D:\work\vcpkg",
    [string]$Version = "0.1.0-dev",
    [switch]$NoToolchain,
    [switch]$RequireNetworkDeps
)

$ErrorActionPreference = "Stop"
foreach ($name in @("http_proxy", "https_proxy", "all_proxy")) {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue)
if (-not $cmake) {
    $vsCmake = Join-Path $env:ProgramFiles "Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCmake) { $cmake = $vsCmake } else { throw "cmake.exe not found in PATH or Visual Studio" }
} else {
    $cmake = $cmake.Source
}
$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"

$configureArgs = @("-S", $root, "-B", $buildDir, "-DORCA_BUILD_VERSION=$Version")
if (-not $NoToolchain) {
    $toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found: $toolchain" }
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    $configureArgs += "-DVCPKG_TARGET_TRIPLET=$Triplet"
    $configureArgs += "-DVCPKG_MANIFEST_MODE=OFF"
}
if ($RequireNetworkDeps) { $configureArgs += "-DORCA_REQUIRE_NETWORK_DEPS=ON" }

Write-Host "[orca] configure"
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "[orca] build ($BuildType)"
& $cmake --build $buildDir --config $BuildType
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

Write-Host "[orca] tests"
& $ctest --test-dir $buildDir -C $BuildType --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "ctest failed" }

Write-Host "[orca] done"
