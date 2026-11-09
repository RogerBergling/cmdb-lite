# Windows build, run, and validation

The Windows application is a normal Win32 GUI executable. There is no console
window and no server to start separately.

```text
cmdb.exe
   └── WinMain (src/ui/main.c)
        └── Win32 window + WebView2
             └── web/index.html
                  └── app.js ↔ C JSON bridge ↔ SQLCipher database
```

The executable is built with CMake, Visual Studio 2022, and vcpkg. SQLCipher,
libsodium, OpenSSL, and the C/C++ runtime are linked statically. The only
external runtime component is **Microsoft Edge WebView2**.

## Prerequisites

- Windows 10 or 11, x64.
- Visual Studio 2022 with **Desktop development with C++**.
- CMake on `PATH`.
- vcpkg:
  - set `VCPKG_INSTALLATION_ROOT`, or
  - place it at `C:\vcpkg`, or
  - place it at `%USERPROFILE%\vcpkg`.
- WebView2 Evergreen Runtime. It is included in current Windows 10/11
  installations. Older systems need the bootstrapper:
  <https://developer.microsoft.com/microsoft-edge/webview2/>

## One-command build

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup-windows.ps1
```

The script:

1. Locates vcpkg and Visual Studio.
2. Installs `sqlcipher:x64-windows-static` and
   `libsodium:x64-windows-static`.
3. Configures a Release build with the static triplet.
4. Builds and runs the tests.
5. Creates:
   - `dist\cmdb-lite-windows-x64\bin\cmdb.exe`
   - `dist\cmdb-lite-windows-x64\bin\web\`
   - `dist\cmdb-lite-windows-x64.zip`
   - `dist\cmdb-lite-0.1.0-windows-x64.msi` when WiX Toolset v3 is installed

## Manual build

```powershell
vcpkg install sqlcipher:x64-windows-static libsodium:x64-windows-static

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DCMDB_BUILD_UI=ON `
  -DCMDB_BUILD_TESTS=ON

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Run

From the build tree:

```powershell
.\build\Release\cmdb.exe
```

From the portable package:

```powershell
.\dist\cmdb-lite-windows-x64\bin\cmdb.exe
```

The executable resolves its web assets in this order:

1. `CMDB_WEB_DIR` environment variable.
2. The compile-time source `src/web` directory (useful during development).
3. `web\` next to `cmdb.exe` (installed layout).
4. `..\share\cmdb-lite\web` (Unix-style install fallback).

If the assets cannot be found, the application shows an error page rather than
a blank window.

## Install

The MSI is a per-machine installer and therefore requires administrator
elevation. It installs under `Program Files\cmdb-lite`, creates a Start Menu
shortcut, and registers the application for uninstall.

```powershell
msiexec /i .\dist\cmdb-lite-0.1.0-windows-x64.msi
```

The portable ZIP is also supported:

1. Extract `cmdb-lite-windows-x64.zip` to a local folder, for example:
   `%LOCALAPPDATA%\Programs\cmdb-lite`.
2. Create a shortcut to `bin\cmdb.exe` on the Desktop or Start Menu.

Do not install under a path that is not writable if you intend to keep the
database there. The application asks you to choose the database location
explicitly; the installation folder is separate from the database.

## WebView2 behaviour

- Current Windows 10/11 includes the WebView2 Evergreen Runtime.
- If it is missing, `webview_create` fails and the application displays a
  Windows message box explaining that the WebView2 runtime is required.
- The MSI currently relies on the runtime being present; bundling the Evergreen
  bootstrapper is the next packaging step.
- WebView2 updates through Windows Update/Microsoft Edge, not through the
  application.

## Expected packaged files

```text
cmdb-lite-windows-x64/
  bin/
    cmdb.exe
    web/
      index.html
      app.js
      style.css

dist/
  cmdb-lite-windows-x64.zip
  cmdb-lite-0.1.0-windows-x64.msi
```

There should be **no** `sqlcipher.dll`, `libsodium.dll`, `libcrypto-*.dll`, or
`WebView2Loader.dll` in the package. The static triplet links those libraries,
and `webview/webview` uses its built-in WebView2 loader.

## Validation checklist

Run this on a clean Windows VM before calling the Windows build supported:

- [ ] `cmdb.exe` starts by double-clicking; no console window appears.
- [ ] The unlock screen appears with no JavaScript errors.
- [ ] Create a database in a path containing spaces, for example
      `C:\Users\Test User\Documents\CMDB\inventory.cmdb`.
- [ ] Repeat with a non-ASCII path component.
- [ ] The `.cmdb` and `.cmdb.meta` files are created.
- [ ] Lock and reopen the database; data is still present.
- [ ] Add a CI type and a custom field.
- [ ] Add a CI with the standard fields and a custom attribute.
- [ ] Import the example CSV and relationships files.
- [ ] Create a relationship manually.
- [ ] Generate a dependency map.
- [ ] Export a `.drawio` file and open it in draw.io/diagrams.net.
- [ ] Try a wrong passphrase; it is rejected and the database is not modified.
- [ ] Test on a machine without WebView2; the error message is clear.
- [ ] Verify the ZIP contains no unexpected DLLs.
- [ ] Install the MSI as an administrator; verify the Start Menu shortcut.
- [ ] Launch from the Start Menu and create/open a database.
- [ ] Uninstall the MSI; verify the application files are removed and the
      database is not deleted.
- [ ] Verify SmartScreen behaviour; sign the executable and MSI before public
      release.

## CI

`.github/workflows/ci.yml` runs a Windows build with the static vcpkg triplet,
executes the core tests, creates `cmdb-lite-windows-x64.zip`, builds the WiX MSI,
and uploads both artifacts. The current CI run passes on `windows-2022`; the
package contains `bin/cmdb.exe` and `bin/web/` and no unexpected DLLs. GUI launch
validation is still a manual clean-VM checklist because GitHub-hosted Windows
runners do not provide a reliable interactive desktop for WebView2 window tests.

## Known limitations

- The MSI is per-machine (administrator required); per-user MSI packaging is not
  implemented.
- No code signing yet, so Windows SmartScreen may warn.
- The MSI does not yet bundle the WebView2 Evergreen bootstrapper.
- No automatic update mechanism.
- Database file ACLs inherit from the chosen folder; the POSIX `0600` logic does
  not apply on Windows. Use a per-user folder such as
  `%LOCALAPPDATA%\cmdb-lite` for the database.

## Next Windows tasks

1. Verify the MSI and static package on a real Windows VM.
2. Bundle/check the WebView2 Evergreen bootstrapper in the MSI.
3. Add Authenticode signing and reproducible release artifacts.
4. Add a clean-VM GUI smoke test, ideally with a dedicated Windows runner.
5. Consider a per-user MSI or MSIX package.
