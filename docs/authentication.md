# Authentication and server mode

> **Status: design only.** This document defines the planned v0.7 authentication
> and server architecture. v0.6.0 is still a desktop application with no
> application-level login, TOTP, or server mode.

## Decisions

- One package with two entry points:
  - `cmdb`: desktop client;
  - `cmdb-server`: Linux server, Fedora and Ubuntu first.
- One Go server process with the existing C core linked through CGO.
- The server owns the encrypted SQLCipher database. The desktop client never
  opens the database directly.
- The database passphrase is known only to the server. Users authenticate with
  their own account and never receive the database key.
- Several accounts with the same inventory privileges.
- First-run admin, then admin-created accounts. No self-registration.
- TOTP and ten one-time recovery codes per user; admin reset is also available.
- Passkeys/WebAuthn are explicitly out of scope for this phase.
- Configurable session lifetime; defaults are 12 hours absolute and 30 minutes
  idle.
- Audit authentication events and all CI/host changes with the username as the
  actor.
- HAProxy terminates TLS; the service binds loopback or a private address and is
  never directly internet-facing.
- Linux first, Windows later.

## Goals

- Require a username and password before the inventory is visible.
- Support TOTP and recovery codes per user.
- Keep the database encryption boundary separate from the login boundary.
- Ensure every change is attributable to a named account.
- Support a local desktop server, a remote desktop client, and a headless
  Fedora/Ubuntu server from the same package.
- Recover access with one-time recovery codes or an admin reset.

## Non-goals

- Passkeys/WebAuthn in this phase.
- Fine-grained roles or per-CI permissions in the first release. All accounts
  have the same inventory privileges; the admin capability is limited to user
  and authentication management.
- Self-registration.
- Email-based password reset.
- SSO with HIBP. HIBP accounts and cmdb-lite accounts remain separate.
- Direct internet exposure of the cmdb-lite service. HAProxy/VPN is the ingress.
- FIPS/HSM-backed authentication or database keys.

## Architecture

```text
Desktop client (cmdb)                 Browser
        │                                │
        │ localhost                      │ HTTPS via HAProxy/VPN
        └───────────────┬────────────────┘
                        ▼
                 cmdb-server (Go)
        ┌───────────────┴────────────────┐
        │ HTTP API, sessions, CSRF       │
        │ users, TOTP, recovery codes    │
        │ audit actor                    │
        └───────────────┬────────────────┘
                        │ CGO calls, serialized
                        ▼
                 C core (cmdb_core)
        inventory · graph · import · audit · SQLCipher
                        │
                        ▼
                 encrypted .cmdb + .meta
```

### Why Go for the server

- Mature password hashing, TOTP, and session libraries.
- HIBP already uses this approach; the design can be reused without sharing
  accounts or the database.
- Avoid implementing password/session security in C.

### C core ownership

The C core remains the only code that opens the SQLCipher database and owns the
schema. Authentication tables are part of the same encrypted database.

The Go server calls a narrow C authentication API for user/session/credential
persistence, or uses the existing `cmdb_app_handle` JSON boundary with new
authentication methods. The C core must **not** be called concurrently: the Go
server serializes every C-core call with a mutex because the current core assumes
a single connection and UI thread.

## Process modes

### Desktop mode

The desktop client supports both modes:

- **Local:** `cmdb` starts `cmdb-server` on `127.0.0.1` with a random port,
  prompts for the database passphrase, and opens the UI from
  `http://localhost:<port>`.
- **Remote:** `cmdb` connects to a configured `cmdb-server` URL instead of
  starting a local server.

In both modes, users log in with their account. The desktop client never opens
the database directly.

### Server mode

- `cmdb-server` runs headless on Fedora or Ubuntu.
- It binds `127.0.0.1` or a configured private address.
- HAProxy terminates TLS and forwards to the loopback/private listener.
- The service requires a database passphrase at startup:
  - interactive start is acceptable for the first implementation;
  - a systemd credential or root-only key file can be added later.
- Browser clients use the same account/TOTP login.

## Data model

Schema v5 adds the following tables to the encrypted database. All timestamps
are ISO 8601 UTC.

```sql
CREATE TABLE users (
    id TEXT PRIMARY KEY NOT NULL,
    username TEXT NOT NULL COLLATE NOCASE UNIQUE,
    password_hash TEXT NOT NULL,          -- Argon2id PHC string
    is_admin INTEGER NOT NULL DEFAULT 0, -- user management only
    disabled INTEGER NOT NULL DEFAULT 0,
    totp_secret TEXT NOT NULL DEFAULT '', -- protected by SQLCipher
    totp_confirmed INTEGER NOT NULL DEFAULT 0,
    totp_last_step INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    password_changed_at TEXT NOT NULL DEFAULT ''
);

CREATE TABLE user_recovery_codes (
    id TEXT PRIMARY KEY NOT NULL,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    code_hash TEXT NOT NULL,              -- Argon2id PHC string
    created_at TEXT NOT NULL,
    used_at TEXT NOT NULL DEFAULT ''
);

CREATE INDEX idx_recovery_user ON user_recovery_codes(user_id, used_at);

CREATE TABLE sessions (
    id TEXT PRIMARY KEY NOT NULL,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,      -- SHA-256/BLAKE2b of cookie token
    csrf_hash TEXT NOT NULL,
    created_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    idle_expires_at TEXT NOT NULL,
    absolute_expires_at TEXT NOT NULL,
    ip TEXT NOT NULL DEFAULT '',
    user_agent TEXT NOT NULL DEFAULT '',
    revoked_at TEXT NOT NULL DEFAULT ''
);

CREATE INDEX idx_sessions_user ON sessions(user_id, revoked_at);
CREATE INDEX idx_sessions_expiry ON sessions(idle_expires_at, absolute_expires_at);
```

`app_meta` additions:

- `auth.setup_token_hash`: hash of the one-time first-run setup token.
- `auth.setup_token_created_at`: creation time.
- `auth.setup_token_expires_at`: expiry time.
- `auth.schema_version`: optional authentication migration marker.

### Notes

- Password and recovery-code hashes use Argon2id with versioned parameters in the
  PHC string.
- TOTP secrets are inside the SQLCipher-encrypted database. An attacker with the
  database key can read them; that is the same trust boundary as the password
  hashes.
- Session tokens are random 256-bit values. Only their hashes are stored.

## First-run admin

1. On first start, `cmdb-server` detects that `users` is empty.
2. It generates a random setup token, stores only its hash in `app_meta`, and
   prints the token and setup URL to the server console.
3. The setup page requires the token and creates the first admin with a
   username/password.
4. The setup token is single-use and expires (default: 24 hours).
5. After the first admin exists, the setup endpoint is permanently disabled.
6. If the token is lost before the admin is created, a host-local command
   (`cmdb-server setup-token --reset`) generates a new one. This command requires
   access to the server console/filesystem.

Desktop mode follows the same flow. Because a desktop app may not have a visible
console, the local server displays the one-time token in a local-only dialog and
the desktop UI opens the setup page. The token must never be written to a
world-readable file or logged after first use.

TOTP enrollment is offered during setup but is not forced in the first release.
The UI must clearly warn that an admin without MFA is less protected.

## Login flows

### Password login

1. `POST /api/v1/auth/login` with username and password.
2. Verify the Argon2id hash in constant time.
3. Apply per-IP and per-username rate limiting and lockout.
4. If TOTP is confirmed, create a short-lived pending-MFA session.
5. On TOTP success, create a full session and rotate the session token.
6. Audit success/failure with username/IP/user-agent, without logging passwords
   or TOTP codes.

### TOTP

- 30-second, 6-digit SHA-1 TOTP, one step of clock skew.
- Store the last accepted step to prevent replay.
- Pending MFA sessions expire after 5 minutes.
- Replacing TOTP requires a completed session or admin reset.
- Recovery codes can complete MFA and are single-use.

### Recovery codes

- Ten codes generated at enrollment or on demand.
- Displayed once; only Argon2id hashes are stored.
- Each code is single-use.
- Using a code to sign in can reset the password and/or MFA according to policy.
- Generating new codes invalidates the previous set after confirmation.

## Sessions and transport security

- Session cookie: random 256-bit token, `HttpOnly`, `SameSite=Strict`, `Secure`
  when HTTPS is used.
- CSRF token per session, required on all state-changing requests.
- Origin/Referer validation for browser requests.
- Absolute and idle expiry checked on every request; defaults 12 hours and 30
  minutes, configurable.
- Logout and admin disable revoke all sessions for the user.
- Rate limiting and lockout for login, TOTP, recovery-code, and setup endpoints.
- The server trusts `X-Forwarded-*` headers only from a configured proxy address
  or CIDR. Otherwise the headers are ignored.
- The server binds `127.0.0.1` or a private address. HAProxy provides TLS and
  internet-facing access.
- No wildcard CORS. The UI is same-origin.

## Audit

Authentication events are written to the existing hash-chained `audit_log` with
`entity_type = 'auth'` and the username as `actor`:

- first-run admin created;
- login succeeded/failed;
- logout;
- session revoked;
- TOTP enrolled/confirmed/reset;
- recovery code generated/used;
- password changed/reset;
- user created/disabled/enabled/deleted;
- admin reset performed.

Existing CI, relationship, import, scan, backup, and export events remain
audited. The actor for authenticated requests is the logged-in username, not
`local`.

## API sketch

```text
GET  /api/v1/auth/setup/status
POST /api/v1/auth/setup
POST /api/v1/auth/login
POST /api/v1/auth/mfa/totp
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
POST /api/v1/users/{id}/reset-password
POST /api/v1/users/{id}/reset-mfa
```

All inventory endpoints require a completed session. The server passes the
authenticated username into the C core audit actor.

## Database passphrase and service startup

- The database passphrase is not stored in the database.
- Server mode prompts on an interactive start, or reads a systemd credential or
  root-only key file.
- Desktop mode prompts when it starts the local server.
- Users never know or type the database passphrase.
- The server refuses to start without the correct passphrase.
- Restarting the service after a reboot requires the configured unlock method.
  Interactive unlock is accepted for the first implementation.

## Migration from v0.6.0

- Schema v4 → v5 adds the authentication tables and `app_meta` setup keys.
- Existing inventory, relationships, diagrams, scan staging, and audit rows are
  preserved.
- On first open with no users, the server enters first-run setup mode.
- The existing database passphrase remains the database encryption passphrase.
- The first admin account is new and separate from the database passphrase.
- Backups remain the `.cmdb` plus `.meta` pair.

## Packaging

- One package containing `cmdb` and `cmdb-server`.
- Fedora and Ubuntu server packaging first:
  - systemd unit for `cmdb-server`;
  - config file for bind address, proxy CIDR, and session timeouts;
  - server data directory and file permissions;
  - log/journal guidance without secrets.
- Desktop package includes the thin client and can start a local server.
- Windows packaging follows after Linux.

## Security test plan

- password hash and verify;
- TOTP replay rejection;
- recovery-code single use;
- session fixation and rotation;
- CSRF rejection;
- login rate limiting and lockout;
- session idle/absolute expiry;
- disabled user/session revocation;
- admin reset authorization;
- auth bypass attempts on inventory endpoints;
- audit completeness for auth and host changes;
- migration from a v0.6.0 database;
- backup/restore of the authentication state.

## Open decisions

- The first release uses an interactive database-passphrase prompt on server
  start. A systemd credential/root-only key file can follow.
- TOTP is optional, including for the first admin, with a prominent warning.
- Recovery-code use can reset both password and MFA; the exact UX is still to be
  finalized.
- The desktop client supports both starting a local server and connecting to a
  configured server URL. The default mode and switch UI still need design.
