# cmdb-lite Windows setup: install dependencies, build, test, and create a
# portable ZIP.
#
# Run from the repository root:
#   powershell -ExecutionPolicy Bypass -File .\setup-windows.ps1
#
# Requirements:
#   - Visual Studio 2022 with "Desktop development with C++"
#   - CMake on PATH
#   - vcpkg (VCPKG_INSTALLATION_ROOT set, or C:\vcpkg, or %USERPROFILE%\vcpkg)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "== cmdb-lite Windows setup ==" -ForegroundColor Cyan
Write-Host ""

# Locate vcpkg.
$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkgRoot) {
    foreach ($candidate in @("C:\vcpkg", (Join-Path $env:USERPROFILE "vcpkg"))) {
        if (Test-Path (Join-Path $candidate "vcpkg.exe")) {
            $vcpkgRoot = $candidate
            break
        }
    }
}
if (-not $vcpkgRoot) {
    throw @"
vcpkg was not found.
Install it from https://github.com/microsoft/vcpkg, run bootstrap-vcpkg.bat,
set VCPKG_INSTALLATION_ROOT, and run this script again.
"@
}
$vcpkg = Join-Path $vcpkgRoot "vcpkg.exe"
if (-not (Test-Path $vcpkg)) {
    throw "vcpkg.exe not found at $vcpkg"
}
Write-Host "vcpkg: $vcpkg"

# Locate Visual Studio 2022. CMake will report a clear error if it is missing,
# but this gives a better message up front.
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) {
        throw "Visual Studio 2022 with the C++ desktop workload was not found."
    }
    Write-Host "Visual Studio: $vsPath"
}

Write-Host ""
Write-Host "== Installing vcpkg dependencies ==" -ForegroundColor Cyan
& $vcpkg install `
    "sqlcipher:x64-windows-static" `
    "libsodium:x64-windows-static"
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg install failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "== Configuring ==" -ForegroundColor Cyan
$cmakeConfigure = @(
    "-S", ".",
    "-B", "build",
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DCMDB_BUILD_UI=ON",
    "-DCMDB_BUILD_TESTS=ON"
)
& cmake @cmakeConfigure
if ($LASTEXITCODE -ne 0) {
    throw "cmake configure failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "== Building Release ==" -ForegroundColor Cyan
& cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "== Running tests ==" -ForegroundColor Cyan
& ctest --test-dir build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Tests failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "== Creating portable package ==" -ForegroundColor Cyan
$packageRoot = Join-Path $PSScriptRoot "dist\cmdb-lite-windows-x64"
$packageZip = Join-Path $PSScriptRoot "dist\cmdb-lite-windows-x64.zip"
if (Test-Path $packageRoot) {
    Remove-Item -Recurse -Force $packageRoot
}
if (Test-Path $packageZip) {
    Remove-Item -Force $packageZip
}
& cmake --install build --config Release --prefix $packageRoot
if ($LASTEXITCODE -ne 0) {
    throw "cmake install failed with exit code $LASTEXITCODE."
}
Compress-Archive -Path "$packageRoot\*" -DestinationPath $packageZip -Force -ErrorAction Stop

Write-Host ""
Write-Host "== Done ==" -ForegroundColor Green
Write-Host "Run:     .\build\Release\cmdb.exe"
Write-Host "Package: $packageZip"
Write-Host ""
Write-Host "Windows 11 and current Windows 10 include the WebView2 runtime."
Write-Host "On older systems, install the Evergreen WebView2 Runtime first:"
Write-Host "https://developer.microsoft.com/microsoft-edge/webview2/"
