# Security design

This document describes the security properties of `cmdb-lite`, what it
protects against, and what it does not. It is written for operators and for
reviewers of the codebase.

## Assets

- The CMDB database: inventory records, relationships, diagrams, and audit log.
- The database passphrase.
- The derived encryption key.
- Import files and network scan results before and during ingestion.
- The integrity of exported `.drawio` files and backups.

The CMDB should not contain passwords, API keys, private keys, tokens, or other
secrets. Store a reference to an external secrets manager instead.

## Encryption at rest

- **Database format:** SQLCipher 4, AES-256-CBC with per-page HMAC-SHA512
  authentication.
- **Key derivation:** Argon2id13 via libsodium, with `OPSLIMIT_MODERATE` (3
  passes) and `MEMLIMIT_MODERATE` (256 MiB). The derived 32-byte value is
  hex-encoded and used as the SQLCipher passphrase; SQLCipher then applies its
  own PBKDF2-HMAC-SHA512 derivation and per-database salt.
- **Sidecar metadata:** `<database>.meta` contains only the KDF algorithm,
  Argon2id parameters, a random 16-byte salt, a version, and a BLAKE2b-256
  checksum. It contains no passphrase or derived key.
- **Parameter validation:** the sidecar is rejected if its KDF parameters are
  below the compiled minimum. This prevents a downgrade to cheaper Argon2id
  settings. Because the parameters are inputs to key derivation, tampering with
  them also makes the database fail to open.
- **File permissions:** the database and sidecar are set to mode `0600` on
  POSIX. The containing directory's permissions still matter.
- **SQLite settings:** `cipher_memory_security=ON`, `secure_delete=ON`,
  `foreign_keys=ON`, `temp_store=MEMORY`, `trusted_schema=OFF`, WAL journaling
  with full synchronous writes.
- **No key persistence:** the passphrase and derived key are not written to disk.
  The C core zeroizes its key buffers with `sodium_memzero` after use.
- **Limitation:** the webview's JavaScript engine and the OS may retain copies of
  the passphrase string in memory. A webview-based UI cannot guarantee complete
  zeroization. This is documented rather than hand-waved away.

The `.meta` sidecar is required to open the database. Back up the database and
the sidecar together. A database without its sidecar cannot be opened, and a
sidecar without its database is useless.

## Passphrase guidance

- Use a unique passphrase of at least 12 characters; a long random phrase is
  better.
- A passphrase manager is recommended.
- There is no recovery mechanism. Losing the passphrase means losing the data.
- Do not reuse the passphrase for other systems.
- The UI clears the passphrase fields after a successful open/create. It never
  logs the passphrase.

## Threat model

### Protected against

- Theft or unauthorised copying of the database file, assuming a strong
  passphrase and a non-compromised machine.
- Casual inspection of the file by another local user.
- Plaintext leakage through SQLite temporary files, because temp storage is
  memory-only.
- SQL injection through imported values, because all SQL uses prepared
  statements.
- Spreadsheet formula injection in CSV exports: cells starting with `=`, `+`,
  `-`, or `@` are prefixed with a single quote.
- Malformed CSV causing unbounded memory use, because rows, columns, fields, and
  file size are bounded.
- Accidental modification of the audit hash chain, which is detected by
  `cmdb_audit_verify`.
- A downgrade of Argon2id parameters through sidecar tampering.
- Silent auto-merge of network discovery data, because discovery is staged for
  review.

### Not protected against

- Malware or a compromised OS running as the same user while the database is
  unlocked.
- A weak, reused, or phished passphrase.
- Screen capture, keyboard loggers, or a compromised webview.
- A malicious OS administrator.
- Full replacement of the database and sidecar with a different database. The
  application will reject a mismatched/corrupt pair, but it cannot prove
  provenance without a signature.
- An attacker who knows the passphrase and can write to the database. The audit
  chain is tamper-evident against accidental edits, not against a key-holding
  attacker; they can rewrite the chain.
- Traffic analysis or legal consequences of network scanning. Authorisation is
  the operator's responsibility.

## Audit log

Every create, update, and delete of a CI, CI type, type field, or relationship
writes an `audit_log` row. Imports write one summary row. Each row contains:

```text
ts, actor, action, entity_type, entity_id, detail,
prev_hash, hash
```

`hash` is BLAKE2b-256 over the previous hash, timestamp, actor, action, entity,
and detail, separated by unit separators. `prev_hash` chains the rows in
insertion order. `cmdb_audit_verify` detects a broken chain and is exposed to the
UI through the `audit.verify` API method.

The audit log lives inside the encrypted database. It is not an off-device,
signed log. For high-assurance environments, export audit events to an external
append-only system.

## Application surface

- Desktop-local mode is offline. There is no remote content, telemetry, or
  update check.
- Server mode is same-origin. The UI loads only from the configured server and
  uses its authenticated RPC proxy.
- The Content-Security-Policy blocks remote scripts, frames, objects, and
  arbitrary network connections.
- The JS bridge accepts JSON only. Request size is capped at 64 MiB. Import
  content is capped at 20 MiB.
- User data is rendered with DOM `textContent`; the UI does not inject imported
  strings as HTML.
- SQL uses prepared statements and bound parameters.
- CSV exports prefix cells beginning with `=`, `+`, `-`, or `@` with a single
  quote to prevent spreadsheet formula execution. The prefix is part of the
  exported text.
- File dialogs are native. The C core writes exports atomically.
- Release builds do not enable webview developer tools and do not honor
  `CMDB_WEB_DIR` or `CMDB_UI_URL`; those overrides are compiled out with
  `NDEBUG`. `CMDB_SERVER_URL` is a supported release-mode setting.
- The application does not execute a shell. nmap is launched with a fixed argv
  vector, validated target/port arguments, a private XML output file, and a hard
  timeout.

## Server authentication

Server mode moves the database behind a Go server with:

- username/password accounts and Argon2id password hashes;
- session tokens kept in memory and stored only as hashes;
- mandatory TOTP and at least one unused recovery code before inventory
  access; passkeys are out of scope for this phase;
- ten one-time recovery-code hashes per user;
- admin user management and MFA reset;
- per-request audit actor;
- configurable session/idle expiry;
- HAProxy/VPN ingress only, with the service bound to loopback or a private
  address.

The database passphrase is known only to the server; users authenticate to the
server and never receive the database key. Authentication state is JSON in
`app_meta` inside SQLCipher. Sessions are memory-only. The complete design,
storage format, and API are in [`authentication.md`](authentication.md); the
deployment runbook is in [`server.md`](server.md).

Server-specific requirements and limitations:

- TLS termination is mandatory. `cmdb-server` has no TLS listener; a direct HTTP
  listener exposes passwords and session cookies.
- Proxy forwarding headers are trusted only from `--proxy` CIDRs. The proxy must
  set `X-Forwarded-Proto` for Secure cookies and append the client to
  `X-Forwarded-For`. The rightmost forwarded address is used.
- TOTP requires accurate server time. Only one 30-second step of skew is
  accepted. Use NTP/chrony.
- Login, TOTP, and recovery rate limits are in memory and cleared by restart.
  There is no persistent lockout or global rate limit.
- The server API is serialized: one process, one database, one `cmdb-rpc` child.
  A long import, scan, or audit verification blocks other users.
- There is no health/readiness endpoint, metrics, structured request log, or
  graceful-shutdown handler in this release. Use a TCP check and external
  supervision.
- Server accounts and MFA material are inside the database backup. Protect the
  backup and passphrase separately. The server has no online backup API; stop
  the service before copying the database pair.
- The server mode UI hides the desktop Backup action and blocks export/save
  methods. There is no browser download of inventory or `.drawio` files.

Desktop-local mode still has no application-level login. Anyone who can open the
database file and knows the passphrase has full access. Opening a server-managed
database locally therefore bypasses server accounts.

## Memory-safety posture

The core is C17, which is memory-unsafe. The project compensates with:

- strict compiler warnings (`-Wall -Wextra -Wpedantic`, shadow, format, cast,
  and prototype checks) and `-Werror` in development presets;
- compiler and linker hardening where available:
  `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=3`, PIE/ASLR, `-z relro`,
  `-z now`, non-executable stack, stack-clash protection, and control-flow
  protection;
- ASan/UBSan build configuration;
- bounded parsers and explicit length checks;
- prepared SQL statements and no shell execution;
- a libFuzzer target for the CSV importer, plus future fuzzing for nmap XML;
- local cppcheck/clang-tidy/Valgrind workflows. CodeQL, cppcheck, ASan/UBSan,
  and fuzzing are **not yet enforced in CI**;
- a vendored cJSON tree and a pinned `webview/webview` tarball hash. SQLCipher,
  libsodium, expat, GTK/WebKitGTK, OpenSSL, and the vcpkg baseline are not
  version-pinned on every platform.

This reduces risk but does not make C memory-safe. A security-critical
deployment should include independent review and fuzzing of the import, API, and
scanner parsers.

## Network scanning

The scanner is a separate, permissioned workflow:

1. The operator confirms that they are authorised to scan the target network.
2. The target is restricted to an IPv4 address or a CIDR from `/24` to `/32`,
   with at most 256 addresses. At most 256 distinct ports may be scanned.
3. The scan uses unprivileged nmap TCP connect discovery (`-sT`) and a fixed set
   of numeric ports. Optional `-sV` service detection is available; `-O` OS
   detection requires root/administrator and is rejected when not elevated. No
   NSE scripts, brute force, credential guessing, or exploitation.
4. nmap runs with a fixed argv vector, no shell, and XML output parsed by
   libexpat. DOCTYPE internal subsets, external identifiers, and entity declarations are
   rejected; the standard `<!DOCTYPE nmaprun>` prolog is accepted. Nesting,
   host, port, and input sizes are bounded.
5. Rate limits (`--max-rate 500`), one retry, a per-host timeout, and an overall
   process timeout are enforced. Cancellation kills the nmap process group.
6. Results are persisted in encrypted staging tables. Nothing is written to the
   inventory until the operator chooses create, merge, or ignore for each host.
   Matches are suggestions only; there is no automatic merge. Merge defaults to
   filling empty scan-derived fields, with an explicit overwrite option.
7. Scan metadata (target, ports, command, start/end time, host count, status) is
   audited. Each created CI, merge, and import is audited separately.

Scanning networks without authorisation may be illegal. The application is not a
penetration-testing tool.

## Backups and recovery

- Back up the database and the `.meta` sidecar together.
- Backups remain encrypted; protect the passphrase separately.
- The in-app **Backup** action uses the SQLite online backup API and writes the
  matching `.meta` sidecar. It is consistent while the database is open.
- Test restore procedures before relying on them.
- The derived SQLCipher key is retained in process memory while the database is
  open so backup can run without re-entering the passphrase. It is zeroized on
  close and never written to disk.
- Do not copy a live database by hand while it is open; use **Backup** or stop
  the application first.

## Hardening checklist for operators

- Use full-disk encryption in addition to the CMDB encryption.
- Keep the operating system, webview runtime, SQLCipher, and libsodium patched.
- Restrict the database directory to the application user.
- Do not store the database on a world-writable share.
- Use a unique passphrase and a password manager.
- Lock the application when leaving the workstation; there is no idle auto-lock
  yet.
- Review imports before committing.
- Authorise scans explicitly and keep scan scope tight.
- Export audit events to an external log if required by policy.
- Treat Windows CI artifacts as unsigned. Signing and SBOMs are not part of the
  current rollout decision; verify `SHA256SUMS`.
- Private vulnerability reporting and `SECURITY.md` are configured. Define a
  supported-version and patch-response policy.

## Reporting a vulnerability

Do not open a public issue containing a working exploit or sensitive data.
Contact the repository owner privately with:

- the affected version/commit,
- a description of the impact,
- reproduction steps or a proof of concept,
- any suggested mitigation.

GitHub private vulnerability reporting is enabled on the public repository.
The monitored fallback contact is `github@bergling.net`. See
[`SECURITY.md`](../SECURITY.md) for the current policy.

## Known limitations

- Server mode has no RBAC beyond the admin flag; all non-admin accounts share
  inventory privileges.
- Desktop-local mode remains single-user with no application-level login.
- Sessions are memory-only: a server restart logs everyone out.
- No passphrase rotation/rekey, local auto-lock, or database passphrase attempt
  rate limiting. Server login/TOTP/recovery endpoints are rate-limited.
- No configurable automatic or scheduled backups; the current release has manual
  backup and automatic pre-migration backup only.
- The derived key is not `sodium_mlock`ed. Swap, hibernation, and core dumps are
  OS-level concerns; zeroization happens on normal close, not on a crash.
- The Go server holds the database passphrase in a Go string. Go strings cannot
  be reliably wiped, so the passphrase may remain in the server process memory
  until exit. Restrict core dumps and swap for server deployments.
- Server mode has no persistent lockout, RBAC, or session list. TOTP and at
  least one unused recovery code are required before inventory access;
  admin-created/reset passwords are forced to change. Treat it as an
  authenticated inventory for a trusted team, not a hardened multi-tenant
  service.
- Server mode has no health/readiness endpoint or graceful shutdown handler.
  Use a TCP check, external supervision, and a maintenance window for backups
  and restarts.
- Webview memory cannot be fully zeroized.
- The audit chain is local and key-holder-rewritable; there is no off-device
  audit export or forwarding yet.
- No single-instance guard; do not open the same database from two processes or
  from a shared/network filesystem.
- The embedded diagram editor is a lightweight `.drawio`-compatible canvas, not
  the official draw.io web application. Dependency-map exports are plaintext
  files chosen by the user.
- The nmap XML parser is tested with fixtures and the process runner, but is not
  yet part of continuous fuzzing.
- `nmap -O` is unavailable without elevation and in the standard Flatpak
  sandbox.
- Signing, SBOMs, and build provenance are not used for the current rollout.
  Windows artifacts are unsigned and may trigger SmartScreen; `SHA256SUMS` is the
  published integrity check.
- The `main` branch and release tags are not protected by repository rulesets.
  GitHub Actions are referenced by tag, the repository allows all actions, and
  SHA pinning is not required. The vcpkg baseline is not pinned.
- The Flatpak sandbox grants broad home-directory and network access.
- No macOS, ARM64, 32-bit, or Windows server build is produced by CI. Linux
  packages include `cmdb-server`; Flatpak and Windows packages are desktop-only.
- No FIPS 140, Common Criteria, HSM-backed key, or hardware-token support is
  claimed.
