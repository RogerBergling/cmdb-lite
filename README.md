# cmdb-lite

A local-first, encrypted Configuration Management Database (CMDB) for Linux and
Windows.

> **Status: v0.4.0 feature release.** The repository contains the C17 core,
> SQLCipher storage layer, Argon2id key derivation, CI categories/types/custom
> fields, typed relationships, CSV/TSV import, dependency graph builder,
> `.drawio` export, an embedded diagram editor, persisted nmap discovery with
> matching/merge review, encrypted database backup, and the desktop webview
> shell. Linux, Fedora, Windows, and Flatpak CI builds are defined; the Windows
> job runs a real WebView2 smoke test and produces the portable ZIP and MSI.
> A clean-Windows-VM install/uninstall check is still recommended.

## Screenshots

| Inventory | Network discovery |
|---|---|
| ![Inventory](docs/screenshots/inventory.png) | ![Network discovery](docs/screenshots/discovery.png) |
| **Diagram editor** | **Dependency map** |
| ![Diagram editor](docs/screenshots/diagrams.png) | ![Dependency map](docs/screenshots/dependency-map.png) |
| **CI types and custom fields** | **Light theme** |
| ![CI types](docs/screenshots/ci-types.png) | ![Light theme](docs/screenshots/inventory-light.png) |

The screenshots use sample inventory data.

## Installation

A Git clone contains source only; executables are not committed. Download the
latest release from:

<https://github.com/RogerBergling/cmdb-lite/releases/latest>

Release assets:

| Platform | Files |
|---|---|
| Debian/Ubuntu | `cmdb-lite_0.4.0_amd64.deb` |
| Fedora/RHEL | `cmdb-lite-0.4.0-*.rpm` |
| Portable Linux | `cmdb-lite-0.4.0-linux-x86_64.tar.gz` |
| Flatpak (Linux) | `cmdb-lite.flatpak` |
| Windows portable | `cmdb-lite-windows-x64.zip` |
| Windows installer | `cmdb-lite-0.4.0-windows-x64.msi` |
| Checksums | `SHA256SUMS` |

Verify the download before running it:

```bash
sha256sum -c SHA256SUMS
```

On Windows:

```powershell
Get-FileHash .\cmdb-lite-windows-x64.zip -Algorithm SHA256
```

Compare the output with the matching line in `SHA256SUMS`.

### Flatpak (recommended for a self-contained Linux install)

The Flatpak uses the GNOME runtime for GTK3/WebKitGTK and bundles SQLCipher,
libsodium, nmap, and the application. It runs on Fedora, Debian, Ubuntu, and
other Flatpak hosts without installing development packages. The GNOME runtime
must be installed (Flatpak fetches it from Flathub if needed).

```bash
flatpak install --user ./cmdb-lite.flatpak
flatpak run io.github.rogerbergling.CmdbLite
```

The sandbox permits network access for authorised scans and home-directory
access so you can choose a database file. OS detection (`nmap -O`) still needs
root privileges and is unavailable in the standard Flatpak sandbox; service
detection (`-sV`) works normally. See [`docs/flatpak.md`](docs/flatpak.md).

### Debian / Ubuntu

Install the dependencies first, then the package:

```bash
sudo apt update
sudo apt install -y libgtk-3-0t64 libwebkit2gtk-4.1-0 libssl3t64
sudo apt install ./cmdb-lite_0.4.0_amd64.deb
cmdb
```

The `.deb` declares its runtime dependencies. SQLCipher and libsodium are
linked statically when the build environment provides their `.a` archives;
expat, GTK3, WebKitGTK, and OpenSSL remain system libraries. The Flatpak is the
fully self-contained option.

### Fedora / RHEL

Install with `dnf` so dependencies are resolved automatically:

```bash
sudo dnf install ./cmdb-lite-0.4.0-*.rpm
cmdb
```

Manual dependency installation is normally unnecessary. The RPM requires GTK3,
WebKitGTK 4.1, expat, and the system OpenSSL libraries.

### Windows

**Portable ZIP**

1. Unzip `cmdb-lite-windows-x64.zip`.
2. Run `bin\cmdb.exe`.
3. If Windows reports a missing WebView2 runtime, install the Microsoft
   Edge WebView2 Evergreen Runtime. It is present on current Windows 10/11.

No DLLs, `web/` folder, or additional runtime files are required. SQLCipher,
libsodium, OpenSSL, expat, and the C/C++ runtime are linked statically.

**MSI installer**

```powershell
msiexec /i .\cmdb-lite-0.4.0-windows-x64.msi
```

The MSI is per-machine, requires administrator elevation, and creates a Start
Menu shortcut. Uninstall with:

```powershell
msiexec /x .\cmdb-lite-0.4.0-windows-x64.msi
```

### Portable Linux tarball

```bash
tar -xzf cmdb-lite-0.4.0-linux-x86_64.tar.gz
./cmdb-lite-0.4.0-linux-x86_64/bin/cmdb
```

The ELF requires GTK3, WebKitGTK 4.1, expat, and the system OpenSSL 3
`libcrypto.so.3`. SQLCipher and libsodium are linked statically when the build
environment provides their `.a` archives.

### First launch

1. Choose **Create database**.
2. Select a path for the encrypted `.cmdb` file. A `.cmdb.meta` sidecar is
   created next to it.
3. Enter a master passphrase of at least 12 characters.
4. Use **Backup** in the top bar to create a consistent encrypted copy. The
   `.cmdb` and `.cmdb.meta` files must be kept together. The passphrase cannot be
   recovered.

The application never uploads data. The database remains at the path you chose.
See [`docs/backup.md`](docs/backup.md) for backup and restore details.

### Network discovery prerequisite

Network discovery invokes an installed nmap executable:

```bash
# Debian/Ubuntu
sudo apt install nmap

# Fedora
sudo dnf install nmap
```

On Windows, install nmap from <https://nmap.org/download.html> and put
`nmap.exe` on `PATH`. If it is installed elsewhere, set `CMDB_NMAP` to the full
path before starting the application. The Flatpak bundles nmap.

`-sV` service/version detection works unprivileged. `-O` OS detection requires
root on Linux or an elevated process on Windows, and is unavailable in the
standard Flatpak sandbox. See
[`docs/scanning.md`](docs/scanning.md) for the safety model, exact command,
persisted staging, matching/merge review, cancellation, and troubleshooting.

### Upgrading

Application upgrades replace the executable only. Your database and `.meta`
sidecar stay where they are. Before upgrading, lock the application and copy
both files together, or use the in-app **Backup** button.

The v0.4.0 release migrates the schema from version 1 to version 2 (CI
categories and persisted scan staging). The migration is applied in place when
the database is opened; back up first.

```bash
# Debian/Ubuntu
sudo apt install ./cmdb-lite_0.4.0_amd64.deb

# Fedora
sudo dnf upgrade ./cmdb-lite-0.4.0-*.rpm
```

On Windows, install the new MSI or replace the portable executable and open the
same database.

Full backup, rollback, and post-upgrade checks:
[`docs/upgrading.md`](docs/upgrading.md).

Or build locally with `./setup.sh` (Debian/Ubuntu), `./setup-fedora.sh`
(Fedora), or `setup-windows.ps1` (Windows).

## Why this exists

Most CMDB products are server applications. `cmdb-lite` is a single-user desktop
application that keeps the inventory in one encrypted database file that you
choose and control. It is offline-first: no account, no cloud service, and no
telemetry.

The intended data is ordinary CMDB data — servers, IP addresses, serial numbers,
operating systems, owners, business services, applications, network devices,
virtual machines, and the relationships between them. Passwords, API keys, and
other secrets **must not** be stored in the CMDB; store a reference to your
password vault instead.

## Features

### Implemented

- Desktop webview shell for Linux (GTK3 + WebKitGTK 4.1) and Windows
  (WebView2), with one shared C17 core and one shared HTML/CSS/JS user
  interface.
- Encrypted SQLCipher database at a user-selected path.
- Master passphrase with Argon2id key derivation (libsodium).
- Sidecar key-derivation metadata file with an integrity checksum.
- Managed CI categories with stable IDs, ordering, colors, and CI type
  assignment.
- User-defined CI types and per-type custom fields.
- Dark, light, and follow-system themes with persisted preference.
- CI records with the standard CMDB fields listed below.
- Typed relationships with duplicate and self-reference protection.
- CSV and TSV import with validation preview and transactional commit.
- Relationship import from CSV/TSV.
- Multi-seed dependency traversal with direction and depth controls.
- Dependency map rendered in the UI.
- `.drawio` export generated from the dependency graph.
- **Embedded diagram editor** with draggable CIs, persistent diagrams stored as
  `.drawio` XML inside the encrypted database, and connections that create real
  typed relationships.
- **nmap-based network discovery** with an authorisation gate, `/24` scope limit,
  fixed argv, private XML output, optional `-sV` service detection, optional
  `-O` OS detection (privileged), cancellation, and an overall timeout.
- **Persisted scan staging** with exact-match suggestions against existing CIs
  by IP, MAC, hostname, and CI name. The operator chooses create, merge (fill
  empty fields or overwrite scan fields), or ignore for each host.
- **Encrypted database backup** from the application; the backup is consistent
  while the database is open and includes the matching `.meta` sidecar.
- Append-only audit log with a hash chain.
- **Single-file executables**: a Linux ELF and a Windows EXE with the HTML/CSS/JS
  UI embedded; no separate `web/` folder is needed.
- Linux `.deb`/RPM/TGZ, a **Flatpak using the GNOME runtime and bundling nmap**,
  and Windows portable ZIP plus WiX-generated MSI.
- Core test suite, including a live nmap test in Linux/Fedora CI, parser
  fixtures, asynchronous scan-manager cancellation, backup, category, theme,
  and migration checks.
- MIT license.

### Distribution model

The application ships as one executable per OS, with no `web/` asset folder:

| Platform | Artifact | Runtime requirements |
|---|---|---|
| Windows | `cmdb.exe` | WebView2 runtime (present on current Windows 10/11). SQLCipher, libsodium, OpenSSL, expat, and the C/C++ runtime are linked statically. |
| Linux (.deb/RPM/TGZ) | `cmdb` ELF | GTK3, WebKitGTK 4.1, expat, and system OpenSSL. SQLCipher and libsodium are linked statically when `.a` archives are available. |
| Linux (Flatpak) | `cmdb-lite.flatpak` | Uses the GNOME runtime for GTK3/WebKitGTK and bundles SQLCipher, libsodium, and nmap. The GNOME runtime is installed separately by Flatpak. |

The encrypted database and its `.meta` sidecar are intentionally separate files
chosen by the user. During development, `CMDB_WEB_DIR=src/web` can load the
external UI files instead of the embedded bundle. Set `CMDB_STATIC_CRYPTO=OFF`
to link SQLCipher/libsodium dynamically on Linux.

### Next milestones

- Passphrase rotation and automatic encrypted backups.
- Windows code signing and signed release artifacts/SBOM publication.
- Optional official draw.io web editor bundle (the current editor uses
  `.drawio`-compatible XML and the built-in canvas).

## CI record fields

Every CI has the following fields. `CI ID` and `Last Updated` are system-managed;
the rest are editable.

| Field | Required | Notes |
|---|---|---|
| CI ID | system | UUID generated by the application; stable for the life of the record. |
| CI Name | yes | Human-readable name, for example `app01` or `Payments API`. |
| CI Type | yes | Must reference a user-defined CI type. |
| Description | no | Free text, up to 4 KiB. |
| Environment | no | Examples: Production, Staging, Test, Development. |
| Status | no | Examples: Active, Planned, Maintenance, Retired. |
| Business Service | no | The service this CI supports. |
| Technical Owner | no | Person or team accountable for the technical component. |
| Support Team | no | Team that operates/supports the CI. |
| Criticality | no | Examples: Critical, High, Medium, Low. |
| Hosting / Location | no | Data centre, cloud region, rack, office, or site. |
| Dependencies | derived | Computed from typed relationships; never stored as free text. |
| Last Updated | system | UTC timestamp, updated whenever the record changes. |
| External ID | optional | Stable identifier from another system; used for idempotent imports. |
| Custom fields | optional | Defined per CI type; stored as key/value attributes. |

The UI treats Environment, Status, and Criticality as free text with suggested
values. This avoids hard-coding an organisation's taxonomy. A future version can
promote them to configurable lookup lists.

### CI types

CI types are user-defined and grouped into managed categories such as Compute,
Network, Applications, Services, or categories you create in the **Categories**
dialog. The application seeds editable examples:

- Linux Server, Windows Server, Virtual Machine
- Router, Switch, Firewall, Network
- Web Application, Database, Business Service, Cloud Service
- Other

Each type can define custom fields with a key, label, data type (`text`,
`number`, `date`, `boolean`, `url`, `select`), required flag, and select options.
The seeded types include useful fields such as `ip_address`, `serial_number`,
`os_version`, `vendor`, `model`, `url`, and `version`.

## Relationship model

Relationships are directed, typed edges between two CIs:

```text
Application --runs_on--> Server
Application --uses-----> Database
Server      --hosted_in-> Cloud
Application --supports-> Business Service
Server      --depends_on-> Network
```

Seeded relationship types:

| Type | Meaning | Inverse |
|---|---|---|
| `runs_on` | Application or service runs on a host. | `hosts` |
| `uses` | Component uses another component. | `used_by` |
| `hosted_in` | CI is hosted in a location or platform. | `hosts` |
| `supports` | CI supports a business service. | `supported_by` |
| `depends_on` | CI depends on another CI. | `depended_on_by` |
| `connects_to` | Network or physical connection. | `connects_to` |
| `member_of` | CI is a member of a group/service. | `contains` |

The dependency map can traverse outgoing, incoming, or both directions, up to
five levels deep, and can be filtered by relationship type. Cycles are handled
safely. Selecting multiple CIs produces the union of their dependency
neighbourhoods.

## Import file formats

Detailed rules and more examples are in
[`docs/import-format.md`](docs/import-format.md). The short version follows.

### General rules

- Encoding: UTF-8. A UTF-8 BOM is accepted.
- CSV: comma-separated, RFC 4180 quoting, header row required.
- TSV/TXT: tab-separated, same header rules. A `.txt` file is treated as TSV.
- Dates: ISO 8601 UTC, for example `2026-11-09T10:00:00Z`.
- Empty cells are stored as empty values, not as the literal string `NULL`.
- Maximum import size: 20 MiB; maximum 10,000 data rows; maximum 1 MiB per field.
- Import is preview-first. Nothing is written until you press **Commit**.

### CI CSV/TSV

Required headers:

```text
ci_name,ci_type
```

Optional headers:

```text
ci_id,description,environment,status,business_service,
technical_owner,support_team,criticality,hosting_location,
last_updated,external_id,custom.<key>
```

Example `cis.csv`:

```csv
ci_name,ci_type,description,environment,status,business_service,technical_owner,support_team,criticality,hosting_location,external_id,custom.ip_address,custom.serial_number,custom.os_version
app01,Linux Server,Primary payments application server,Production,Active,Payments,Alice Example,Linux Platform Team,High,dc1-rack42,SRV-001,10.20.0.11,SN-8891,Ubuntu 26.04
db01,Database,Payments primary database,Production,Active,Payments,Alice Example,Database Team,Critical,dc1-rack43,DB-001,10.20.0.21,SN-2210,PostgreSQL 18
payments-api,Web Application,Payments REST API,Production,Active,Payments,Bob Example,Payments Team,High,cloud-eu-west-1,APP-001,,,v4.12.0
```

Rules:

- `ci_id` is optional. If present, it must be a valid UUID and is used to update
  an existing record or to create a record with a caller-supplied ID.
- `external_id` is optional but must be unique. It is the recommended stable key
  for repeated imports from another system.
- Duplicate handling is explicit: `error`, `skip`, or `update`. The importer
  matches by `ci_id` first, then by `external_id`. It never silently merges
  records by name.
- `ci_type` must match an existing CI type (case-insensitive). The import screen
  has an optional **Create missing CI types** checkbox. When enabled, unknown
  type names are created automatically.
- Custom columns use the prefix `custom.` followed by a lowercase key matching
  `[a-z][a-z0-9_]{0,63}`. Example: `custom.ip_address`.
- Unknown `custom.*` keys are accepted and stored as attributes, even if the CI
  type does not declare them. This keeps imports from being rejected by schema
  drift; the UI can display all attributes.
- `last_updated` is optional on import and, when supplied, is validated but the
  application's own `updated_at` timestamp is authoritative.

### Relationship CSV/TSV

Required headers:

```text
source_ci,relationship_type,target_ci
```

Optional header: `description`.

Example `relationships.csv`:

```csv
source_ci,relationship_type,target_ci,description
app01,runs_on,app01,Application server hosts the application
payments-api,runs_on,app01,Payments API runs on app01
payments-api,uses,db01,Payments API uses the primary database
payments-api,supports,payments-service,Payments API supports the Payments business service
```

`source_ci` and `target_ci` may contain a CI ID, an external ID, or a CI name.
Resolution is deterministic:

1. Exact CI ID.
2. Exact external ID.
3. Exact CI name, but only if exactly one CI has that name. Ambiguous names are
   an error and must be replaced with an ID.

Relationship types are matched case-insensitively. A relationship cannot point a
CI at itself, and duplicate `(source, type, target)` triples are rejected or
skipped according to the duplicate policy.

### Network discovery import

The **Discovery** tab runs nmap and stores the results in the encrypted database
before anything enters the inventory:

```text
target CIDR -> nmap scan (optional -sV / -O) -> persisted staging
            -> match against existing CIs -> create / merge / ignore
```

A staged host contains:

```text
ip_address, hostname, mac_address, vendor, os_guess, services, open_ports
```

Scans are asynchronous: **Cancel scan** stops nmap, and the staged job remains
available after restarting the application. Jobs that were running when the
application closed are marked `interrupted`.

For every pending host, the application suggests existing CIs by exact `IP
address`, `management_ip`, `mac_address`, CI name, or `hostname` attribute. The
match and the reason are shown, but nothing is selected automatically. The
operator chooses one action per host:

- **Create new CI** with the selected CI type.
- **Merge into an existing CI**. By default only empty scan-derived fields are
  filled; select **Overwrite scan fields when merging** to replace IP, hostname,
  MAC, vendor, OS, services, or open ports. Standard CI fields are preserved.
- **Ignore**.

Imported CIs receive `ip_address`, `hostname`, `mac_address`, `vendor`,
`os_guess`, `services`, `open_ports`, `discovery_method=nmap`, and
`discovery_target` attributes. The hostname is used as the CI name when
available, otherwise the IP address. Per-host failures are reported and the
staged host remains pending. See [`docs/scanning.md`](docs/scanning.md).

### CSV injection

Exports must neutralise cells that begin with `=`, `+`, `-`, or `@` because
spreadsheet applications may interpret them as formulas. Imports treat all cell
content as literal data; they never evaluate formulas or commands.

## Diagrams, dependency maps, and draw.io

The **Dependency Map** tab accepts one or more CI selections, a direction
(`outgoing`, `incoming`, `both`), a depth (1–5), and optional relationship-type
filters. The backend performs a breadth-first traversal in C and returns nodes
and edges.

The **Diagrams** tab is an embedded offline editor:

- Add CIs from the encrypted inventory.
- Drag nodes to arrange the diagram.
- Select two nodes and a relationship type, then connect them. The connection
  creates a real typed relationship, so the Dependency Map and graph traversal
  update immediately.
- Save diagrams as `.drawio` XML inside the encrypted database.
- Open the same XML in diagrams.net or draw.io Desktop if needed.

**Export .drawio** in the Dependency Map tab writes an uncompressed `.drawio`
file through a native save dialog. The embedded editor is a lightweight
`.drawio`-compatible canvas, not the official draw.io web application. See
[`docs/diagrams.md`](docs/diagrams.md).

## Security model

### Encryption at rest

- The database is encrypted with **SQLCipher 4** (AES-256-CBC with per-page
  HMAC-SHA512 authentication; SQLCipher's own PBKDF2-HMAC-SHA512 derivation uses
  its default 256,000 iterations).
- The master passphrase is processed with **Argon2id13** via libsodium using
  `OPSLIMIT_MODERATE` (3 passes) and `MEMLIMIT_MODERATE` (256 MiB).
- A random 16-byte salt and the KDF parameters are stored in a sidecar file named
  `<database>.meta`. The sidecar contains no secret material.
- The sidecar includes a BLAKE2b-256 checksum. KDF parameters are validated
  against minimum safe bounds on every open.
- The Argon2id output is hex-encoded and used as the SQLCipher passphrase.
  SQLCipher then applies its own PBKDF2-HMAC-SHA512 key derivation.
- The application never stores the passphrase or derived key on disk.
- SQLCipher is configured with `cipher_memory_security=ON`,
  `secure_delete=ON`, `foreign_keys=ON`, and `temp_store=MEMORY`.
- Database and sidecar files are set to mode `0600` on POSIX systems where
  possible. The containing directory's permissions still matter; do not place
  the database in a world-writable directory.

### Threat model

Protected against:

- Theft or unauthorised copying of the database file or backups, assuming a
  strong passphrase.
- Casual inspection of the file by another local user.
- Accidental plaintext leakage through temporary files (SQLite temp store is
  memory-only).
- Malformed CSV or imported data causing SQL injection or memory corruption
  (bounded parsers, prepared statements, a CSV fuzz target).

Not protected against:

- Malware running as the same OS user while the database is unlocked.
- A weak or reused passphrase.
- An attacker who can observe the screen or keyboard.
- A malicious OS administrator or compromised operating system.
- Traffic analysis of network scans from the local network.
- Full database substitution by an attacker who can replace the file; the
  application will detect it as a different/corrupt database, but cannot prove
  provenance without a signature.

If the database is lost and the passphrase is forgotten, the data cannot be
recovered. Back up both the database and its `.meta` sidecar together.

### Network scanning

The scanner is implemented with nmap and is deliberately constrained:

- Unprivileged TCP connect scans (`-sT`) by default.
- Optional `-sV` service/version detection and optional `-O` OS detection. `-O`
  requires root/administrator privileges and is rejected with a clear error when
  the application is not elevated. The standard Flatpak sandbox does not grant
  that privilege.
- A fixed argv vector; the application never invokes a shell.
- Strict IPv4/CIDR validation, a `/24` maximum scope, a 256-port cap, rate
  limits, retries, per-host timeouts, and an overall process timeout.
- nmap XML parsed by libexpat with DTD/entity rejection and bounded host, port,
  and nesting limits. The standard `<!DOCTYPE nmaprun>` prolog is accepted;
  internal subsets and external identifiers are rejected.
- Private temporary XML output (`0600` on POSIX) removed after parsing.
- Explicit authorisation confirmation before the API starts a scan; the target,
  command, timestamps, and host count are audited.
- Asynchronous execution with a **Cancel scan** action. Cancellation kills the
  nmap process group, and job state is persisted across restarts.
- Results are matched against existing CIs but never merged automatically. The
  operator chooses create, merge, or ignore per host.

Scanning networks without authorisation may be illegal. Use the scanner only on
networks you own or are explicitly permitted to test.

### Database backups

The in-app backup uses SQLite's online backup API to create a consistent
encrypted copy while the database is open. It writes the matching `.meta`
sidecar alongside the backup. The derived SQLCipher key is retained in process
memory while the database is open so backup can run without asking for the
passphrase again; it is zeroized on close and never written to disk. Backups
remain encrypted and require the same passphrase. See
[`docs/backup.md`](docs/backup.md).

### Application security

- The UI is an offline HTML/CSS/JS document embedded in the executable. No
  remote content, analytics, or telemetry is loaded.
- A strict Content-Security-Policy blocks remote scripts, frames, objects, and
  network connections. The UI uses DOM `textContent`, not untrusted
  `innerHTML`, for data rendering.
- The JS-to-C bridge accepts validated JSON only. Request and response sizes are
  bounded.
- All SQL uses prepared statements.
- Passphrases are zeroized with `sodium_memzero` after key derivation. The
  JavaScript string and the webview's memory cannot be guaranteed to be
  zeroized; this is a documented limitation of webview-based UIs.
- Release builds disable webview developer tools. Debug builds enable them only
  when `CMDB_DEVTOOLS=1`.
- The application does not store secrets, and it does not log passphrases, keys,
  SQL parameters, or full database contents.

### Auditability

The database contains an append-only `audit_log` table. Each row includes a
timestamp, action, entity type/ID, optional JSON detail, the previous row's
hash, and a BLAKE2b-256 hash over the row. The chain is tamper-evident for
accidental edits but is not a substitute for signed, off-device logs. A local
attacker who knows the key can rewrite the chain.

## Architecture

```text
┌────────────────────────────────────────────────────────────┐
│  HTML/CSS/JS UI (src/web)                                  │
│  inventory · types · relationships · import · discovery      │
│  diagrams · dependency map                                   │
└──────────────────────────┬─────────────────────────────────┘
                           │ JSON binding (webview)
┌──────────────────────────▼─────────────────────────────────┐
│  C17 application core (src/core)                            │
│  api · domain model · SQLCipher · Argon2id · import · graph │
└──────────────────────────┬─────────────────────────────────┘
                           │
        ┌──────────────────┴──────────────────┐
        │ Linux                               │ Windows
        │ webview (GTK3 + WebKitGTK 4.1)      │ webview (WebView2)
        └─────────────────────────────────────┘
```

The native shell is intentionally thin. `webview/webview` is a pinned
third-party dependency (MIT) that provides a C API over WebKitGTK/WebView2. The
application code is C17; the webview dependency itself is C++14 and is compiled
into the executable. SQLCipher, libsodium, and cJSON are the other runtime
dependencies.

### Design decisions

- **C17 core + one HTML/JS UI.** The domain, crypto, import, graph, and audit
  code is portable C. A single web UI avoids maintaining separate Linux and
  Windows interfaces.
- **Native webview, not a bundled browser.** The app uses the OS webview
  (WebKitGTK on Linux, WebView2 on Windows), keeping the binary smaller and the
  rendering engine updated by the OS. The trade-off is a dependency on the
  system webview runtime.
- **SQLCipher rather than field-level encryption.** Full-database encryption
  protects indexes, metadata, and relationships, not just selected columns.
- **Explicit import previews.** Imports never write silently, and discovery will
  never auto-merge.
- **Data model ready for a future server.** UUID domain IDs, an audit actor
  column, and a JSON API boundary keep a future multi-user server practical.

### Repository layout

```text
include/cmdb/        Public C headers for the core
src/core/            Crypto, database, domain, import, graph, scan, diagrams, API
src/ui/              Native webview shell and native file dialogs
src/web/             Offline HTML/CSS/JS UI source
src/web_bundle.c     Generated embedded UI (do not edit)
tests/               C unit and integration tests
docs/                Data model, import, security, architecture, scanning, backup
docs/screenshots/    README screenshots
third_party/         Vendored cJSON (MIT)
packaging/           Icons, embed script, Windows resources, Flatpak, installers
.github/workflows/   Linux, Fedora, Flatpak, and Windows CI builds
```

## Building on Linux

### Dependencies

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
  fakeroot dpkg-dev \
  libsqlcipher-dev libsodium-dev libexpat1-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev \
  nmap \
  clang lld valgrind cppcheck
```

Fedora:

```bash
./setup-fedora.sh
```

Or manually:

```bash
sudo dnf install -y \
  gcc gcc-c++ cmake ninja-build pkgconf-pkg-config git python3 \
  rpm-build findutils \
  sqlcipher-devel libsodium-devel libsodium-static \
  openssl-devel zlib-devel expat-devel \
  gtk3-devel webkit2gtk4.1-devel nmap

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMDB_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure

cpack --config build/CPackConfig.cmake -G RPM -B build/dist
cpack --config build/CPackConfig.cmake -G TGZ -B build/dist
```

The RPM is per-machine and installs the single `cmdb` ELF, desktop entry, icon,
and documentation. Its license field is currently `Proprietary` as a placeholder
until the project license is selected.

The Windows and Linux GUI builds use the `webview/webview` library, which is
fetched by CMake at configure time from a pinned 0.12.0 release tarball with a
SHA-256 hash. The first configure therefore needs network access. For offline or
Flatpak builds, pass
`-DCMDB_FETCH_WEBVIEW=OFF -DCMDB_WEBVIEW_SOURCE_DIR=/path/to/webview-src`.

### Configure, build, test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

After editing anything in `src/web`, regenerate the embedded UI:

```bash
python3 packaging/embed_web.py
```

Run the application:

```bash
./build/cmdb
```

Build Linux packages:

```bash
cd build
cpack --config CPackConfig.cmake -G DEB
cpack --config CPackConfig.cmake -G RPM
cpack --config CPackConfig.cmake -G TGZ
```

The DEB/RPM install the single `cmdb` ELF under `/usr/bin`, the desktop entry,
and the icon. The UI assets are embedded in the executable.

Build the self-contained Flatpak:

```bash
flatpak-builder --user --force-clean --repo=repo build-flatpak \
  packaging/flatpak/io.github.rogerbergling.CmdbLite.yml
flatpak build-bundle repo cmdb-lite.flatpak \
  io.github.rogerbergling.CmdbLite
```

See [`docs/flatpak.md`](docs/flatpak.md) for runtime installation and
limitations.

### AddressSanitizer / UndefinedBehaviorSanitizer

```bash
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMDB_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

### Core-only build (no GUI)

Useful on a headless CI runner or when the GUI dependencies are unavailable:

```bash
cmake -S . -B build-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMDB_BUILD_UI=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Building on Windows

Install Visual Studio 2022 (Desktop development with C++), CMake, and vcpkg.
Then run the Windows setup script from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup-windows.ps1
```

It installs the static vcpkg dependencies, builds, tests, and creates:

```text
dist\cmdb-lite-windows-x64.zip
dist\cmdb-lite-0.4.0-windows-x64.msi
dist\cmdb-lite-windows-x64\bin\cmdb.exe
```

The MSI is built when WiX Toolset v3 is installed and creates a Start Menu
shortcut. It is a per-machine installer, so it requires administrator elevation.

Run the packaged application with:

```powershell
.\dist\cmdb-lite-windows-x64\bin\cmdb.exe
```

The first configure fetches the pinned `webview/webview` dependency and the
Microsoft WebView2 SDK through CMake. Windows 10/11 include the WebView2 runtime
in current installations. For older Windows versions, distribute the Evergreen
WebView2 Runtime bootstrapper.

See [`docs/windows.md`](docs/windows.md) for the manual build, WebView2
behaviour, packaging layout, and the clean-VM validation checklist.

Release builds should be code-signed; see [`docs/security.md`](docs/security.md).
Cross-compiling the Windows GUI from Linux is not supported. Use Windows or the
GitHub Actions Windows runner.

### Notes for vcpkg users

- The SQLCipher CMake package is `sqlcipher` and the target is
  `sqlcipher::sqlcipher`.
- The libsodium CMake package is `unofficial-sodium` and the target is
  `unofficial-sodium::sodium`.
- The expat CMake package is `expat` and the target is `expat::expat`.
- The project includes `<sqlcipher/sqlite3.h>` so the same source works with the
  Linux `libsqlcipher-dev` layout and the vcpkg layout.
- The static triplet (`x64-windows-static`) avoids shipping separate SQLCipher,
  libsodium, and OpenSSL DLLs. The WebView2 runtime remains a system component.

## Documentation

- [`docs/import-format.md`](docs/import-format.md) — CSV/TSV contracts, examples,
  duplicate policy, and error reports.
- [`docs/data-model.md`](docs/data-model.md) — tables, constraints, and the
  future multi-user path.
- [`docs/security.md`](docs/security.md) — encryption, threat model, audit log,
  hardening, and limitations.
- [`docs/architecture.md`](docs/architecture.md) — layers, bridge, data flows,
  and dependency choices.
- [`docs/drawio.md`](docs/drawio.md) — dependency traversal and `.drawio` format.
- [`docs/diagrams.md`](docs/diagrams.md) — embedded diagram editor and
  relationship-creating connections.
- [`docs/themes.md`](docs/themes.md) — dark, light, system theme, and preference
  storage.
- [`docs/scanning.md`](docs/scanning.md) — implemented nmap discovery, safety
  model, service/OS detection, persisted staging, matching/merge review, and
  troubleshooting.
- [`docs/backup.md`](docs/backup.md) — in-app encrypted backup and restore.
- [`docs/flatpak.md`](docs/flatpak.md) — Flatpak build, install, sandbox, and
  nmap limitations.
- [`docs/upgrading.md`](docs/upgrading.md) — backups, database compatibility,
  package upgrades, rollback, and post-upgrade checks.
- [`docs/windows.md`](docs/windows.md) — Windows build, WebView2 behaviour,
  portable package layout, and clean-VM validation checklist.

## Validation

The repository is validated with:

```bash
# Embedded UI is in sync with src/web
python3 packaging/embed_web.py --check

# Formatting/whitespace checks
git diff --check

# Configure and build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Sanitizer build
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMDB_ENABLE_ASAN=ON
cmake --build build-asan

# Tests
ctest --test-dir build --output-on-failure
ctest --test-dir build-asan --output-on-failure

# Static analysis (when installed)
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
  --suppress=missingIncludeSystem src include
```

### Current validation status

The Linux core build was configured with CMake and compiled with strict
warnings (`-Wall -Wextra -Wpedantic`, shadow/format/cast/prototype checks,
`-Werror`). The full CTest suite passed:

```text
test_crypto                      ok
test_csv                         ok
test_types                       ok
test_ci                          ok
test_relationships               ok
test_graph                       ok
test_drawio                      ok
test_import                      ok
test_api                         ok
test_scan                        ok
0 failure(s)
```

The test suite includes an encryption-at-rest assertion: it creates a database,
writes known CI names and descriptions, verifies the plaintext does not appear in
either the main database file or the WAL file, closes it, and checks the main
file again. It also verifies that opening with a wrong passphrase returns an
authentication error. SQLCipher intentionally logs HMAC
failure lines to stderr during that negative test; they are expected.

The HTML/JS UI was exercised in a headless DOM harness with a stubbed bridge:
inventory rendering, CI modal/custom fields, type detail, import preview, and
dependency-map SVG rendering all completed without JS errors. The Discovery
screenshots in this README were rendered from the real embedded UI under
WebKitGTK with a stubbed bridge and sample data.

The scanner tests cover IPv4/CIDR and port validation, nmap XML parsing
(including service detection, DTD rejection, and malformed input), synchronous
and asynchronous fake-nmap process runs, cancellation, and a persisted
staging/matching/import test. Linux and Fedora CI install nmap and run a live
scan against a localhost listener (`CMDB_TEST_LIVE_NMAP=1`). The backup and
diagram tests create a real encrypted database, copy it with the online backup
API, reopen it, and verify data and `.meta` handling.

A real system WebKitGTK smoke run (headless Broadway backend) also loaded the
embedded UI, executed `app.js`, answered `app.info` through the C JSON bridge,
and created an encrypted SQLCipher database through the bridge. The GTK3/WebKitGTK system build and test suite pass with GCC/CMake on
the development host.

The Debian/Ubuntu CI job builds the `.deb` and TGZ packages. A Fedora container
job builds and tests the RPM and TGZ packages. A Flatpak job builds a
self-contained Linux bundle with the GNOME runtime and nmap. The Windows CI job
(Visual Studio 2022, `x64-windows-static`) builds, tests, and packages
`cmdb-lite-windows-x64.zip` and a WiX MSI successfully. The package contains a
single `bin/cmdb.exe` with no unexpected DLLs (verified by CI). GUI launch, MSI
install/uninstall, and WebView2 behaviour must still be validated on a clean
Windows VM using the checklist in
[`docs/windows.md`](docs/windows.md).

## Import examples

Ready-to-use examples are in [`examples/`](examples/):

- `examples/cis.csv`
- `examples/relationships.csv`
- `examples/cis.tsv`

## Limitations and open decisions

- Single-user local application. The schema, audit log, and API boundary are
  designed to allow a future server/RBAC version, but v1 has no authentication
  beyond the database passphrase.
- No password/secret storage. Use references to an external vault.
- Discovery never merges automatically; the operator must choose every action.
- The embedded diagram editor is a lightweight `.drawio`-compatible canvas, not
  the official draw.io web application. The official draw.io bundle is not
  included.
- `nmap -O` needs root/administrator and is unavailable in the standard Flatpak
  sandbox.
- No multi-user concurrent access. Do not share the database file between
  machines while it is open.
- The passphrase cannot be recovered. Back up the database and `.meta` sidecar
  together, or use the in-app backup.
- The UI is webview-based; JavaScript/webview memory cannot be fully zeroized.

## License

MIT — free for everyone. See [`LICENSE`](LICENSE). Third-party components
retain their own licenses; see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
