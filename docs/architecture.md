# Architecture

## Goals

- One codebase that runs on Linux and Windows.
- One encrypted database file at a user-selected path.
- No server, cloud dependency, telemetry, or remote content.
- A clear boundary between the UI and the security-sensitive core.
- A path from single-user local storage to a future server/RBAC deployment.

## Layers

```text
┌─────────────────────────────────────────────────────────────┐
│ UI: HTML/CSS/JS (src/web)                                   │
│ inventory · CI types · relationships · import · discovery    │
│ diagrams · dependency map · draw.io export                   │
└──────────────────────────┬──────────────────────────────────┘
                           │ JSON bridge (webview)
┌──────────────────────────▼──────────────────────────────────┐
│ Native shell (src/ui)                                       │
│ webview lifecycle · native file dialog · bridge adaptation  │
└──────────────────────────┬──────────────────────────────────┘
                           │ C API
┌──────────────────────────▼──────────────────────────────────┐
│ Application core (src/core)                                 │
│ api · domain · import · graph · drawio · diagram · audit     │
│ scan parser · scan manager · scan store · backup             │
│ SQLCipher · Argon2id · bounded CSV/XML parsers · migrations  │
└──────────────────────────┬──────────────────────────────────┘
                           │
                 SQLCipher database (.cmdb)
                 + KDF sidecar (.meta)
```

### UI

`src/web` is a dependency-free single-page application. It uses DOM APIs and
does not load remote resources. A strict Content-Security-Policy blocks remote
scripts, frames, objects, and network connections.

`packaging/embed_web.py` inlines the CSS and JavaScript into one HTML document,
computes SHA-256 CSP hashes, and generates `src/web_bundle.c`. The executable
loads that embedded document, so there is no separate `web/` folder to ship.
In Debug builds, `CMDB_WEB_DIR` can be set to load `src/web` from disk instead
of the embedded bundle. Release builds always use the embedded UI.

The UI calls a single binding:

```js
await window.__cmdb_bridge(JSON.stringify({ method, params }));
```

The binding returns a parsed JSON response:

```json
{
  "ok": true,
  "id": 1,
  "result": { }
}
```

or:

```json
{
  "ok": false,
  "id": 1,
  "error": {
    "code": "validation",
    "message": "CI name is required",
    "detail": "",
    "line": 0
  }
}
```

### Native shell

`src/ui/main.c` owns the webview lifecycle and adapts the third-party
`webview/webview` C API:

- Linux: GTK3 + WebKitGTK 4.1.
- Windows: Win32 + WebView2.

The shell has no domain logic. It opens native file dialogs and forwards bridge
requests to `cmdb_app_handle`. File dialogs are implemented in:

- `src/ui/platform_dialog_gtk.c`
- `src/ui/platform_dialog_win32.c`

### Core

| File | Responsibility |
|---|---|
| `app.c` | Application object, JSON request parsing, response encoding. |
| `api.c` | Method dispatch and UI-facing JSON contracts. |
| `crypto.c` | libsodium initialization, Argon2id, randomness, hashing, wiping. |
| `meta.c` | KDF sidecar format, checksum, validation. |
| `db.c` | SQLCipher open/create, key setup, pragmas, transactions, savepoints. |
| `schema.c` | Schema DDL, default CI/relationship types, schema version. |
| `types.c` | CI types, category assignment, and custom field definitions. |
| `categories.c` | Managed CI categories, ordering, colors, and assignment cleanup. |
| `ci.c` | CI records, attributes, validation, name/external-ID resolution. |
| `relationships.c` | Typed directed relationships with impact classes. |
| `import.c` | Bounded CSV/TSV parser, preview, transactional import. |
| `graph.c` | Multi-seed, depth-limited BFS traversal with relationship and impact filters. |
| `drawio.c` | Uncompressed `.drawio` XML generation and layout. |
| `diagram.c` | Persisted `.drawio`-XML diagrams in encrypted storage. |
| `scan.c` | nmap validation, fixed-argv process execution, bounded XML parsing. |
| `scan_manager.c` | Background scan thread, cancellation, status handoff. |
| `scan_store.c` | Scan job/asset persistence and exact-match suggestions. |
| `audit.c` | Append-only hash-chained audit log and paged viewer queries. |
| `export.c` | CSV/JSON inventory, relationship, and HIBP Linux host export. |
| `settings.c` | Per-user last/recent database paths without secrets. |
| `util.c` | Strings, paths, files, UTF-8, UUIDs, time, string builder. |

### Scan architecture

```text
scan.start ──► scan_jobs row (running)
   │
   ▼
scan_manager worker thread ──► nmap process group
   │                              │
   │                       -sT [-sV] [-O]
   │                              │
   │                       XML temp file
   │                              ▼
   │                       libexpat parser
   ▼
scan.status poll ──► scan_store: assets + match queries ──► UI review
   │
   ▼
scan.import ──► create / merge / ignore ──► CIs + relationships + audit
```

Cancellation sets an atomic flag polled by the worker; the worker kills the
nmap process group. A job left running at shutdown is marked `interrupted` on
the next database open.

### Diagram architecture

The embedded editor stores `.drawio` XML in the `diagrams` table. Connections
call the normal relationship API, so the diagram is a view over real CMDB
relationships rather than a separate graph. Diagram XML is untrusted for
external files but is only rendered structurally in the UI; it is not executed.

### Backup architecture

`cmdb_db_backup` opens a destination SQLCipher database with the in-memory key,
uses `sqlite3_backup` to copy pages, and writes the matching `.meta` sidecar. The
source database remains open and consistent.

### Theme preference

The UI stores `ui.theme` (`system`, `light`, or `dark`) in `app_meta`, with a
best-effort `localStorage` copy for the unlock screen. The C core exposes
`prefs.get` and `prefs.set`; no sensitive data is stored. CSS variables define
the dark palette, and `html[data-theme="light"]` plus a `prefers-color-scheme`
override define the light/system palettes.

### Flatpak

`packaging/flatpak/io.github.rogerbergling.CmdbLite.yml` builds a self-contained
bundle from the GNOME runtime. SQLCipher, libsodium, libpcap, nmap, and the
webview source are pinned and built inside the sandbox. See
[`flatpak.md`](flatpak.md).

## Data flow: opening a database

```text
UI passphrase
   │
   ▼
api db.open ──► meta_read (<db>.meta) ──► validate KDF bounds + BLAKE2b checksum
   │
   ▼
Argon2id(passphrase, salt, 3, 256 MiB) ──► 32-byte key ──► hex
   │
   ▼
SQLCipher sqlite3_key(hex) ──► SELECT count(*) FROM sqlite_master
   │                                   │
   │                                   ├─ failure: auth error, close
   │                                   └─ success: apply pragmas, validate schema
   ▼
open database
```

The passphrase and key buffers are zeroized in C after use. The webview can
retain its own copies; see the limitations in [`security.md`](security.md).

## Data flow: dependency map

```text
selected CI IDs + depth + direction + relationship filters
   │
   ▼
graph.c BFS (max 500 nodes, max 5 levels, edge cap 5,000)
   │
   ├─► graph JSON ──► SVG rendering in the UI
   │
   └─► drawio.c ──► uncompressed .drawio XML
                       │
                       ├─ native save dialog
                       └─ atomic file write (0600)
```

The graph is built from the database, not from a cached UI model, so it reflects
committed data. Cycles are handled by a visited set. Truncation is explicit in
the response and displayed as a warning.

## Data flow: import

```text
file → TextDecoder (UTF-8) → JSON bridge (≤20 MiB)
  → bounded RFC 4180 parser
  → header normalization/validation
  → row validation + type resolution + duplicate planning
  → preview report (no writes)
  → optional commit:
       BEGIN IMMEDIATE
       create missing types
       create/update CIs (savepoints per row)
       insert attributes
       one import audit entry
       COMMIT
```

A validation error prevents commit. Commit re-validates the file because the
database may have changed since the preview. On any row failure the transaction
is rolled back.

## Threading and concurrency

The application is single-user with one UI thread and one scan worker:

- The webview runs on the UI thread.
- Bridge callbacks run on the UI thread and call the core synchronously.
- The scan manager runs nmap on a dedicated worker thread and communicates
  through a mutex-protected status/result handoff. SQLite is not accessed from
  that thread.
- SQLite is opened in the default threading mode; no connection is shared
  across threads.
- `BEGIN IMMEDIATE` is used for imports; row-level operations use savepoints.

A future multi-user server would introduce a real request/transaction boundary,
authentication, RBAC, connection pooling, and an audit actor model.

## Build system

- CMake 3.20+ with Ninja or Visual Studio.
- C17 core; C++17 only as required by the pinned `webview/webview` dependency.
- `pkg-config` for SQLCipher, libsodium, GTK3, and WebKitGTK on Linux.
- CMake `FetchContent` pins `webview/webview` to a specific commit and SHA-256
  archive hash.
- cJSON 1.7.18 is vendored under `third_party/cjson`.
- SQLCipher, libsodium, expat, GTK/WebKitGTK, OpenSSL, and the vcpkg baseline
  are supplied by the distribution or vcpkg and are not version-pinned on every
  platform.
- Tests use a small in-repo harness and CTest.

## Dependency choices

| Dependency | Why |
|---|---|
| SQLCipher | Mature full-database encryption with a SQLite-compatible API. Linked statically on Windows and, when available, on Linux. |
| libsodium | Argon2id, secure randomness, constant-time comparison, memory wiping. Linked statically on Windows and, when available, on Linux. |
| cJSON | Small, permissively licensed JSON parser/serializer for the local bridge. |
| webview/webview | Thin C API over WebKitGTK and WebView2; avoids maintaining two browser shells. |

## Planned authenticated server mode

The next architecture is a Go server with the C core linked through CGO:

```text
cmdb desktop client / browser
        │
        ▼
cmdb-server (Go)
  accounts · sessions · TOTP · recovery · passkeys · audit actor
        │
        ▼
C core (inventory · graph · import · audit · SQLCipher)
```

- The server owns the database; the desktop client never opens it directly.
- Multiple accounts share inventory privileges; the first account is an admin
  for user management.
- TOTP comes first; passkeys follow in a later phase.
- HAProxy terminates TLS; the service binds loopback or a private address.
- The database passphrase is known only to the server, not to users.

The full design, schema, API, and security tests are in
[`authentication.md`](authentication.md).

## Future: multi-user

The intended evolution is a small server that reuses the domain/API layer:

- authentication (local accounts or OIDC),
- role-based access control,
- per-user audit actors,
- encrypted server-side storage with key management,
- concurrent transactions,
- a client that can run against either the local file or the server.

The current schema avoids auto-increment domain IDs and keeps the API
serializable, which makes that migration practical.
