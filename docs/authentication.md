# Authentication and server mode

> **Status: implemented in v0.7.0 (unreleased).** The Go `cmdb-server`, the
> `cmdb-rpc` bridge, first-run setup, password login, TOTP, recovery codes,
> sessions, admin user management, and server-mode audit actors are in the
> codebase. v0.6.0 remains the latest published release.

## Decisions

- One source tree with two Linux entry points:
  - `cmdb`: desktop client;
  - `cmdb-server`: headless Linux server for Fedora and Ubuntu.
- The Go server owns the encrypted SQLCipher database. Browsers and the desktop
  client never open the database file when they use server mode.
- The server talks to the C core through the `cmdb-rpc` child process over
  standard input/output. There is no CGO link and no shared C library ABI.
- The database passphrase is known only to the server. Users authenticate with
  their own account and never receive the database key.
- Several accounts have the same inventory privileges. Administrators can only
  manage accounts and reset credentials.
- First-run admin, then admin-created accounts. No self-registration.
- TOTP is mandatory for every account before inventory access. Ten one-time
  recovery codes are required before inventory access; admins can reset both.
- Admin-created and admin-reset passwords are temporary and must be changed at
  first login.
- Passkeys/WebAuthn are explicitly out of scope for this phase.
- Session defaults are 12 hours absolute and 30 minutes idle, configurable at
  server start.
- Authentication events and inventory changes use the authenticated username as
  the audit actor.
- HAProxy terminates TLS; the service binds loopback or a private address and is
  never directly internet-facing.
- Windows server support is future work. The Windows desktop build remains a
  local client.

## Architecture

```text
Desktop client (cmdb)                 Browser
        │                                │
        │ CMDB_SERVER_URL                │ HTTPS via HAProxy/VPN
        └───────────────┬────────────────┘
                        ▼
                 cmdb-server (Go)
        ┌───────────────┴────────────────┐
        │ HTTP API, sessions, CSRF       │
        │ users, TOTP, recovery codes    │
        │ audit actor                    │
        └───────────────┬────────────────┘
                        │ JSON lines over stdio, serialized
                        ▼
                 cmdb-rpc (C)
                        │
                        ▼
                 C core (cmdb_core)
        inventory · graph · import · audit · SQLCipher
                        │
                        ▼
                 encrypted .cmdb + .meta
```

Why the child process instead of CGO:

- the C core is single-threaded and assumes one open database connection;
- a process boundary makes the serialization explicit;
- the server can be built with the normal Go toolchain and no cgo headers;
- a crash in the database layer does not take down the HTTP listener silently.

`cmdb-server` serializes HTTP API requests and `cmdb-rpc` serializes every
C-core call. Do not run two `cmdb-rpc` processes against the same database.
The full production runbook is in [`server.md`](server.md).

## Process modes

### Local desktop mode

`cmdb` without `CMDB_SERVER_URL` is the original local application:

- the user chooses a `.cmdb` database and enters the database passphrase;
- the C core opens SQLCipher directly in-process;
- there is no application-level login, TOTP, or per-user audit actor.

This mode remains useful for offline/single-user use. It cannot enforce
per-user accountability because anyone with the database file and passphrase
has full access.

### Desktop server mode

Choose **Connect to server** on the desktop unlock screen and enter the server
URL, or set `CMDB_SERVER_URL`:

```bash
CMDB_SERVER_URL=http://127.0.0.1:8080 cmdb
```

The desktop client loads the server UI in its webview, unbinds the local bridge,
and uses the same login/session flow as a browser. It does not start the server
automatically yet; start `cmdb-server` first. Use HTTPS for any non-loopback
server. If the server uses a self-signed certificate, configure the operating
system trust store.

### Server mode

```bash
# Interactive first validation: the server prompts for the database passphrase.
cmdb-server \
  --db /var/lib/cmdb-lite/inventory.cmdb \
  --listen 127.0.0.1:8080
```

For a service, provide the passphrase through a root-owned environment file or
the packaged systemd unit; do not put it in shell history or a command line.

Flags:

| Flag | Default | Purpose |
|---|---|---|
| `--db` | required | Encrypted database path. |
| `--listen` | `127.0.0.1:8080` | HTTP listen address. |
| `--rpc` | beside `cmdb-server` | Path to `cmdb-rpc`. |
| `--web-dir` | auto-detected | Directory with `index.html`, `app.js`, `style.css`. |
| `--proxy` | `127.0.0.1/32,::1/128` | Trusted proxy CIDRs for `X-Forwarded-*`. |
| `--session-idle` | `30m` | Idle session timeout. |
| `--session-absolute` | `12h` | Absolute session timeout. |
| `--reset-auth` | off | Break-glass: clear accounts/TOTP/recovery state, write an audit entry, and exit. Requires the database passphrase and a stopped server. |

The database passphrase is read from `CMDB_DB_PASSPHRASE` or prompted on a TTY.
The variable is unset after reading. An interactive start is acceptable for the
first implementation; a systemd credential or root-only key file can follow.

Installed layout:

```text
/usr/bin/cmdb
/usr/bin/cmdb-server
/usr/bin/cmdb-rpc
/usr/share/cmdb-lite/web/{index.html,app.js,style.css}
```

`cmdb-server` resolves the web directory relative to its executable:
`web`, then `../share/cmdb-lite/web`, then the source-tree fallback.

## Storage model

Authentication state is **not** a schema version 5 set of SQL tables. Schema v4
stays unchanged. The server stores one JSON document in the existing `app_meta`
table under the key `auth.state`:

```json
{
  "version": 1,
  "users": [
    {
      "id": "uuid",
      "username": "admin",
      "password_hash": "$argon2id$…",
      "is_admin": true,
      "disabled": false,
      "totp_secret": "BASE32",
      "totp_confirmed": true,
      "totp_last_step": 12345678,
      "recovery_codes": [
        {"hash": "$argon2id$…", "used_at": ""}
      ],
      "created_at": "2026-11-11T17:27:33Z",
      "updated_at": "2026-11-11T17:27:33Z"
    }
  ]
}
```

Because `auth.state` is inside SQLCipher, a normal `.cmdb` + `.meta` backup
contains accounts, password hashes, TOTP secrets, and recovery-code hashes.
Sessions are **memory-only** and are lost on server restart. A restart logs
everyone out but does not remove accounts.

The JSON blob is deliberately simple for this phase. It is bounded to 1 MiB by
the C API. If account counts or audit requirements grow, replace it with typed
tables and a schema migration.

### Credential details

- Passwords: Argon2id, 64 MiB, 3 iterations, 2 lanes, 32-byte key, random
  16-byte salt, PHC-encoded parameters. Minimum 12 characters, maximum 1024
  bytes. A password change must use a different value. Verifier parameters are
  bounded to reject a tampered hash.
- Recovery codes: 10 codes, 12 Base32 characters, Argon2id-hashed like
  passwords, single use.
- TOTP: 30-second period, 6 digits, SHA-1, 20-byte secret, one step of clock
  skew. The last accepted step is persisted to prevent replay.
- Session tokens: 256-bit random values. Only SHA-256 hashes are kept in memory.
- CSRF tokens: 256-bit random values, required on state-changing API requests.

## First-run admin

1. On first start with no users, `cmdb-server` generates a random setup token,
   prints it and the setup URL to the console, and keeps only its SHA-256 hash
   in memory.
2. The token expires after 24 hours.
3. Open `/login`, enter the token, username, and password. The first account is
   an administrator.
4. The setup endpoint is permanently disabled once a user exists.
5. The new admin is redirected to `/security` and must confirm TOTP and generate
   recovery codes before the inventory pages and RPC API become available.
6. If the token is lost before setup, restart the server. While no users exist,
   a new token is printed on every start.

The setup token is not stored in the database. It is not written to a file or
logged after the startup line.

## Login flows

### Password login

1. `POST /api/v1/auth/login` with JSON `username` and `password`.
2. The server applies per-IP (10 per 15 minutes) and per-username (5 per 15
   minutes) limits. Current-password re-authentication for password changes,
   TOTP setup, and recovery-code generation is limited per user/IP.
3. Verify Argon2id in constant time. Unknown usernames still run a dummy hash.
4. If TOTP is confirmed, create a pending-MFA session valid for 5 minutes.
5. Otherwise create a full session and rotate the cookie.
6. Audit success and failure without logging passwords or codes.
7. A full session is still restricted until the user changes a temporary
   password, confirms TOTP, and generates at least one unused recovery code.

### TOTP

1. `POST /api/v1/auth/mfa` with the 6-digit code.
2. The server checks the pending session, validates the code, and rejects a
   step that was already used.
3. Rotate to a full session and audit the event.

### Recovery codes

1. `POST /api/v1/auth/recovery` with one unused code.
2. On success, mark it used, rotate to a full session, and audit the event.
3. Recovery codes are generated from `/security` after re-entering the current
   password; generating a new set replaces all previous codes. TOTP setup also
   requires the current password.

## Sessions and transport security

- Cookie: `cmdb_session`, random 256-bit token, `HttpOnly`,
  `SameSite=Strict`, `Secure` when the request is HTTPS or a trusted proxy says
  `X-Forwarded-Proto: https`.
- Absolute and idle expiry are checked on every request. Idle expiry slides
  forward but never beyond the absolute expiry.
- Logout deletes the session. Changing a password revokes the user's other
  sessions. Disabling a user, resetting a password, or resetting MFA revokes all
  of that user's sessions.
- State-changing inventory and account API calls require `X-CSRF-Token`
  matching the session. Login, first-run setup, MFA completion, recovery-code
  login, and logout rely on `SameSite=Strict` cookies and, for setup, the
  one-time token; they do not take a CSRF header.
- The server sends `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`,
  `Referrer-Policy: no-referrer`, and `Cache-Control: no-store`.
- Login and account pages use a restrictive Content-Security-Policy.
- `X-Forwarded-For` and `X-Forwarded-Proto` are trusted only when the direct
  peer matches `--proxy`. Otherwise the headers are ignored. The rightmost
  `X-Forwarded-For` entry is used so that a client-supplied header cannot
  override the address appended by the trusted proxy.
- The service has no wildcard CORS. The UI is same-origin.
- There is no TLS listener. Terminate TLS at HAProxy/VPN and bind
  `cmdb-server` to loopback or a private interface.

## Audit

Authentication events are written to the existing hash-chained `audit_log` with
`entity_type = 'user'` or `'auth'` and the account name as `actor`:

- setup and first admin creation;
- login success, password-stage success, failure, MFA failure;
- logout;
- TOTP enrollment and admin reset;
- recovery-code generation, use, and failure;
- password change and admin password reset;
- user create, enable, and disable.

Inventory requests pass the authenticated username as the top-level `actor`
field in the JSON request to the C core. `cmdb_app_handle` sets the audit actor
for that request, so CI, relationship, import, scan, export, and backup events
are attributed to the logged-in user instead of `local`. Local desktop mode
continues to use `local`.

## HTTP API

Pages:

```text
GET /login
GET /security
GET /users
GET /
GET /app.js
GET /style.css
```

Authentication and account API:

```text
GET  /api/v1/auth/status
POST /api/v1/auth/setup
POST /api/v1/auth/login
POST /api/v1/auth/mfa
POST /api/v1/auth/recovery
POST /api/v1/auth/logout
GET  /api/v1/auth/session
POST /api/v1/auth/password
POST /api/v1/auth/totp/setup
POST /api/v1/auth/totp/confirm
POST /api/v1/auth/recovery/generate
GET  /api/v1/users
POST /api/v1/users
POST /api/v1/users/{id}/disable
POST /api/v1/users/{id}/enable
POST /api/v1/users/{id}/reset-password
POST /api/v1/users/{id}/reset-mfa
```

Inventory proxy:

```text
POST /api/v1/rpc     {"method": "...", "params": {...}}
                     → {"ok": true, "result": ...}
                     → {"ok": false, "error": {...}}
```

The proxy uses an explicit allowlist of inventory and account-safe C methods.
It does not expose process/database-management methods, filesystem dialogs,
local exports/saves, local settings, `app.info`, `db.status`, or the auth-state
methods. In particular, `app.info`/`db.status` would reveal the server-side
database path. New C API methods are not reachable through server mode until
they are reviewed and added to the allowlist with the authenticated actor.

The server holds one `cmdb-rpc` child and one open database connection for its
lifetime. Restart the server to release the database.

## Database passphrase and service startup

- The database passphrase is not stored in the database.
- Server mode reads `CMDB_DB_PASSPHRASE` or prompts on a TTY.
- Desktop local mode prompts in the unlock screen.
- Users never know or type the database passphrase.
- The server refuses to start without the correct passphrase.
- Restarting after a reboot requires the configured unlock method. Interactive
  unlock is accepted for the first implementation.

## Migration from v0.6.0

- Schema version stays at 4. There is no v5 authentication schema.
- Existing inventory, relationships, diagrams, scan staging, and audit rows are
  preserved.
- On first server start with no `auth.state`, the server enters first-run setup.
- The existing database passphrase remains the database encryption passphrase.
- The first admin account is new and separate from the database passphrase.
- Backups remain the `.cmdb` plus `.meta` pair and include `auth.state`.
- Opening a server-managed database in local desktop mode bypasses application
  logins. The database passphrase is the only gate in that mode. Treat local
  desktop access as equivalent to knowing the database key.

## Packaging and deployment

- The Linux package contains `cmdb`, `cmdb-server`, `cmdb-rpc`, and the web
  assets under `share/cmdb-lite/web`.
- Flatpak builds only the desktop client (`-DCMDB_BUILD_SERVER=OFF`).
- Windows packages contain only the desktop client.
- The server-only package installs a systemd unit. There is no separate
  flags/config file yet, so edit paths and timeouts in the unit and provide the
  passphrase through a root-only environment file or TTY.
- HAProxy should terminate TLS, forward to the loopback listener, and preserve
  the client address. Add the proxy address/CIDR to `--proxy`.

## Security test plan

Implemented tests cover password hashing/verification, TOTP validation, and
recovery-code single use. Before a production rollout, also exercise:

- session fixation and rotation;
- CSRF rejection;
- login rate limiting and lockout;
- session idle/absolute expiry;
- disabled user/session revocation;
- admin reset authorization;
- auth bypass attempts on inventory endpoints;
- audit completeness for auth and host changes;
- migration from a v0.6.0 database;
- backup/restore of the authentication state;
- restart behavior: sessions are gone, accounts remain.

## Known gaps

- Sessions are memory-only, so a restart logs everyone out.
- The auth state is a single JSON document rather than typed tables.
- The first-run setup token is printed to standard output/logs. Restrict access
  to the server console and journal while the instance has no users.
- The database passphrase is a Go string in the server process and cannot be
  reliably wiped. Restrict core dumps and swap for server deployments.
- No account deletion, password-history policy, or email-based recovery.
- Admin-created and admin-reset passwords are known to the administrator until
  the user changes them. The temporary-password flag forces the change before
  inventory access.
- The Users page can create administrators and non-admin users, but there is no
  role change or user deletion after creation.
- No active-session list or global session-revocation UI. Per-user revocation is
  triggered by disable, password reset, and MFA reset.
- Admin password/MFA reset actions do not require re-authentication; a stolen
  admin session is effectively full administrative control.
- No RBAC beyond the admin flag; all non-admin accounts have full inventory
  privileges.
- No online backup, restore, or export/download in server mode.
- No health/readiness endpoint, metrics, request log, or graceful-shutdown
  handler. The server-only package installs a systemd unit, but there is no
  separate flags/config file; paths and timeouts live in the unit.
- Login rate limits are in memory, cleared on restart, and keyed by username and
  IP. A known username can be temporarily locked out by repeated failures. There
  is no persistent lockout.
- Failed and successful logins are audited without source IP or user-agent.
- Login, setup, MFA, and recovery require `Content-Type: application/json` and
  use `SameSite=Strict` cookies, which blocks ordinary cross-site form login
  CSRF. `Origin`/`Referer` validation is not implemented, and logout does not
  require a CSRF header. Add origin validation if the threat model requires it.
- TOTP requires NTP-synchronized server time; only one 30-second step of skew is
  accepted.
- Desktop local mode has no application-level login.
- The desktop client does not start a local server automatically.
- No passkey/WebAuthn support.
- Windows server support is not implemented.
