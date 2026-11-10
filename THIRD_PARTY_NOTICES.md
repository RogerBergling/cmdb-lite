# Third-party notices

`cmdb-lite` is built with the following third-party components. Their licenses
apply to those components and their redistributions. This file is informational
and does not replace the license text shipped with each dependency.

## Vendored in this repository

### cJSON 1.7.18

- Location: `third_party/cjson`
- License: MIT
- Copyright: Dave Gamble and cJSON contributors
- License text: `third_party/cjson/LICENSE`

## Fetched at build time

### webview/webview 0.12.0

- Location: fetched by CMake (`FetchContent`)
- License: MIT
- Copyright: Serge Zaitsev, Steffen André Langnes, and contributors
- Purpose: C API over GTK3/WebKitGTK on Linux and Win32/WebView2 on Windows.
- The application code uses the C API; the dependency itself is implemented in
  C++14 and is compiled into the executable.

## Linked system/runtime libraries

### SQLCipher

- License: BSD-style (SQLCipher Community Edition)
- Purpose: encrypted SQLite database engine.
- Copyright: Zetetic LLC and contributors.
- Distributions should ship the SQLCipher license text with the binary.

### libsodium

- License: ISC
- Purpose: Argon2id, randomness, BLAKE2b, constant-time comparison, memory
  wiping.
- Copyright: Frank Denis and contributors.

### libexpat

- License: MIT
- Purpose: bounded parsing of nmap XML output.
- Copyright: Expat maintainers and contributors.
- Linux packages normally depend on the distribution's expat library; Windows
  builds link it statically.

### GTK 3 and WebKitGTK 4.1 (Linux)

- GTK: LGPL-2.1-or-later
- WebKitGTK: LGPL-2.1-or-later and BSD-style components
- Purpose: native window and web rendering on Linux.
- These are dynamically linked system libraries; redistribution requirements
  depend on how the application is packaged.

### Microsoft Edge WebView2 (Windows)

- The WebView2 runtime is a Microsoft redistributable; the SDK headers and
  loader are subject to Microsoft's license terms.
- The WebView2 Evergreen Runtime is typically present on Windows 10/11. Older
  systems require the bootstrapper.
- The application uses the WebView2 API through `webview/webview`; the runtime is
  not bundled by this repository.

### OpenSSL / libcrypto

- License: Apache-2.0
- Purpose: cryptographic backend used by SQLCipher.
- Linux packages normally depend on the distribution's OpenSSL. Windows builds
  must ship a compatible OpenSSL library and its notices, or build SQLCipher
  against an approved crypto provider.

### nmap (optional runtime)

- License: Nmap Public Source License (NPSL) for the current release.
- Purpose: optional network discovery; invoked as an external executable.
- nmap is **not** bundled by the `.deb`/RPM/ZIP packages. The Flatpak bundles
  nmap; review the NPSL before redistributing it. Users install it separately
  for other packages.

### libpcap (Flatpak)

- License: BSD-3-Clause
- Purpose: packet capture library required by the bundled nmap.
- Bundled only in the self-contained Flatpak.

## Build and test tools

CMake, Ninja, GCC/Clang, MSVC, pkg-config, cppcheck, Valgrind, and GitHub Actions
are build-time tools and are not redistributed with the application.
