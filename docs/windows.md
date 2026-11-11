# Windows build, run, and validation

The Windows application is a normal Win32 GUI executable. There is no console
window and no server to start separately.

```text
cmdb.exe
   └── WinMain (src/ui/main.c)
        └── Win32 window + WebView2
             └── embedded HTML/CSS/JS bundle
                  └── app.js ↔ C JSON bridge ↔ SQLCipher database
```

The executable is built with CMake, Visual Studio 2022, and vcpkg. SQLCipher,
libsodium, OpenSSL, expat, and the C/C++ runtime are linked statically. The only
external runtime component is **Microsoft Edge WebView2**. Network discovery
additionally requires an installed `nmap.exe`; it is not bundled.

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
2. Installs `sqlcipher:x64-windows-static`,
   `libsodium:x64-windows-static`, and `expat:x64-windows-static`.
3. Configures a Release build with the static triplet.
4. Builds and runs the tests.
5. Creates:
   - `dist\cmdb-lite-windows-x64\bin\cmdb.exe`
   - `dist\cmdb-lite-windows-x64.zip`
   - `dist\cmdb-lite-0.7.0-windows-x64.msi` when WiX Toolset v3 is installed

## Manual build

```powershell
vcpkg install sqlcipher:x64-windows-static libsodium:x64-windows-static expat:x64-windows-static

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

The UI is embedded in `cmdb.exe`. No `web\` folder is required.

For development, `CMDB_WEB_DIR` can point at `src\web` in a **Debug build** to
load the external assets instead of the embedded bundle. Release builds always
use the embedded UI. If the embedded UI cannot be loaded, the application shows
an error page rather than a blank window.

## Install

The MSI is a per-machine installer and therefore requires administrator
elevation. It installs under `Program Files\cmdb-lite`, creates a Start Menu
shortcut, and registers the application for uninstall.

```powershell
msiexec /i .\dist\cmdb-lite-0.7.0-windows-x64.msi
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

dist/
  cmdb-lite-windows-x64.zip
  cmdb-lite-0.7.0-windows-x64.msi
```

There should be **no** `sqlcipher.dll`, `libsodium.dll`, `libcrypto-*.dll`,
`expat.dll`, or `WebView2Loader.dll` in the package. The static triplet links
those libraries, and `webview/webview` uses its built-in WebView2 loader.

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
- [ ] Create a category, assign the CI type to it, and verify the type list
      grouping.
- [ ] Switch between System, Light, and Dark themes; restart and verify the
      preference persists.
- [ ] Add a CI with the standard fields and a custom attribute.
- [ ] Import the example CSV and relationships files.
- [ ] Create a relationship manually and confirm the impact defaults to the
      relationship type's value.
- [ ] Create a `manages` relationship with `management` impact and confirm the
      default Dependency Map impact view does not include it.
- [ ] Create a `Service` CI for DNS, add a `provided_by` relationship to a
      server or router, and confirm the service fields render.
- [ ] Generate a dependency map.
- [ ] Create a diagram, add two CIs, connect them, save, and verify the
      relationship appears in the Dependency Map.
- [ ] Export a `.drawio` file and open it in draw.io/diagrams.net.
- [ ] Use **Backup**, reopen the backup with the same passphrase, and verify
      the inventory and `.meta` sidecar.
- [ ] Restore the backup to a new path and open it.
- [ ] Verify that the last database is pre-filled after restarting the app.
- [ ] Export CI CSV, relationship CSV, and JSON; re-import the CI CSV.
- [ ] Open the Audit tab, page through entries, and verify the hash chain.
- [ ] With nmap on `PATH`, scan a single authorised test host with `-sV` and
      import a selected result.
- [ ] Without nmap, run a scan and verify the clear "not installed" error.
- [ ] Try a wrong passphrase; it is rejected and the database is not modified.
- [ ] Test on a machine without WebView2; the error message is clear.
- [ ] Verify the ZIP contains no unexpected DLLs.
- [ ] Install the MSI as an administrator; verify the Start Menu shortcut.
- [ ] Launch from the Start Menu and create/open a database.
- [ ] Uninstall the MSI; verify the application files are removed and the
      database is not deleted.
- [ ] Verify SmartScreen behaviour; sign the executable and MSI before public
      release.

## Code signing

Signing is **not used for the current rollout**. Unsigned Windows applications
may trigger SmartScreen warnings. The CI workflow retains optional Authenticode
support if that decision changes; it uses these repository secrets when
configured:

- `WINDOWS_CERT_PFX_BASE64` — base64-encoded `.pfx` code-signing certificate.
- `WINDOWS_CERT_PASSWORD` — password for the `.pfx`.

When present, CI signs `cmdb.exe` before packaging and signs the MSI after
creation, using SHA-256 and a DigiCert timestamp. When absent, the build still
completes and produces unsigned artifacts.

For local signing, install the Windows SDK and run:

```powershell
signtool sign /f your-cert.pfx /p your-password `
  /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
  build\Release\cmdb.exe
signtool sign /f your-cert.pfx /p your-password `
  /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
  dist\cmdb-lite-0.7.0-windows-x64.msi
```

Never commit the certificate or password.

## CI

`.github/workflows/ci.yml` runs a Windows build with the static vcpkg triplet,
executes the core tests, creates `cmdb-lite-windows-x64.zip`, builds the WiX MSI,
and uploads both artifacts. The current CI run passes on `windows-2022`; the
package contains a single `bin/cmdb.exe` and no unexpected DLLs. The CI job
also runs a Release WebView2 smoke test (`CMDB_SMOKE_TEST=1`) that loads the UI
and creates an encrypted database through the bridge. MSI install/uninstall and
some interactive WebView2 behaviour still require the manual clean-VM checklist.

## Known limitations

- The MSI is per-machine (administrator required); per-user MSI packaging is not
  implemented.
- Windows artifacts are unsigned by rollout decision. SmartScreen may warn; use
  `SHA256SUMS` for integrity verification.
- The MSI does not bundle the WebView2 Evergreen bootstrapper.
- No automatic update mechanism.
- Database file ACLs inherit from the chosen folder; the POSIX `0600` logic does
  not apply on Windows. Use a per-user folder such as
  `%LOCALAPPDATA%\cmdb-lite` for the database.

## Next Windows tasks

1. Verify the MSI and static package on a real clean Windows 10/11 VM.
2. Bundle or check the WebView2 Evergreen bootstrapper in the MSI.
3. Accept/record the unsigned-artifact SmartScreen behaviour for the release.
4. Consider a per-user MSI or MSIX package.
