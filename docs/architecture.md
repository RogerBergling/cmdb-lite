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
│ inventory · CI types · relationships · import · dependency  │
│ map · draw.io export                                        │
└──────────────────────────┬──────────────────────────────────┘
                           │ JSON bridge (webview)
┌──────────────────────────▼──────────────────────────────────┐
│ Native shell (src/ui)                                       │
│ webview lifecycle · native file dialog · bridge adaptation  │
└──────────────────────────┬──────────────────────────────────┘
                           │ C API
┌──────────────────────────▼──────────────────────────────────┐
│ Application core (src/core)                                 │
│ api · domain · import · graph · drawio · audit · SQLCipher  │
│ Argon2id · bounded CSV parser · schema/migrations           │
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
`CMDB_WEB_DIR` can be set during development to load `src/web` from disk instead.

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
| `types.c` | CI types and custom field definitions. |
| `ci.c` | CI records, attributes, validation, name/external-ID resolution. |
| `relationships.c` | Typed directed relationships. |
| `import.c` | Bounded CSV/TSV parser, preview, transactional import. |
| `graph.c` | Multi-seed, depth-limited BFS dependency traversal. |
| `drawio.c` | Uncompressed `.drawio` XML generation and layout. |
| `audit.c` | Append-only hash-chained audit log. |
| `util.c` | Strings, paths, files, UTF-8, UUIDs, time, string builder. |

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

The vertical slice is single-threaded and single-user:

- The webview runs on the UI thread.
- Bridge callbacks run on the UI thread and call the core synchronously.
- SQLite is opened in the default threading mode; no connection is shared
  across threads.
- `BEGIN IMMEDIATE` is used for imports; row-level operations use savepoints.

A future multi-user server would introduce a real request/transaction boundary,
authentication, RBAC, connection pooling, and an audit actor model.

## Build system

- CMake 3.20+ with Ninja or Visual Studio.
- C17 core; C++17 only as required by the pinned `webview/webview` dependency.
- `pkg-config` for SQLCipher, libsodium, GTK3, and WebKitGTK on Linux.
- CMake `FetchContent` pins `webview/webview` to tag `0.12.0`.
- cJSON 1.7.18 is vendored under `third_party/cjson`.
- Tests use a small in-repo harness and CTest.

## Dependency choices

| Dependency | Why |
|---|---|
| SQLCipher | Mature full-database encryption with a SQLite-compatible API. |
| libsodium | Argon2id, secure randomness, constant-time comparison, memory wiping. |
| cJSON | Small, permissively licensed JSON parser/serializer for the local bridge. |
| webview/webview | Thin C API over WebKitGTK and WebView2; avoids maintaining two browser shells. |

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
