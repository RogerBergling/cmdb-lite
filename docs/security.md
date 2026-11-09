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
- There is no recovery mechanism in the vertical slice. Losing the passphrase
  means losing the data.
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

- The UI is local and offline. There is no remote content, telemetry, or update
  check.
- The Content-Security-Policy blocks remote scripts, frames, objects, and
  network connections.
- The JS bridge accepts JSON only. Request size is capped at 64 MiB. Import
  content is capped at 20 MiB.
- User data is rendered with DOM `textContent`; the UI does not inject imported
  strings as HTML.
- SQL uses prepared statements and bound parameters.
- File dialogs are native. The C core writes exports atomically.
- Release builds do not enable webview developer tools by default.
- The application does not execute a shell. A future nmap integration must use
  a fixed argv vector, not a shell string.

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
- static analysis (clang-tidy, cppcheck, CodeQL) as part of the hardening
  workflow and CI as it matures;
- vendored/pinned third-party dependencies and a pinned webview tarball hash.

This reduces risk but does not make C memory-safe. A security-critical
deployment should include independent review and fuzzing of the import, API, and
scanner parsers.

## Network scanning (planned)

The scanner is designed as a separate, permissioned workflow:

1. The operator confirms they are authorised to scan the target network.
2. The target is restricted to an explicit CIDR, with a maximum host count.
3. Unprivileged TCP connect discovery and common-port checks are used by default.
   ICMP is used only where the OS permits it without elevation.
4. Optional nmap runs with a fixed argument vector, no shell, and XML output
   parsed by libexpat. No NSE scripts, brute force, credential guessing, or
   exploitation.
5. Rate limits and timeouts are enforced.
6. Results go to a staging/review queue. Nothing is written to the CMDB until a
   human approves each host or batch.
7. Scan metadata (target, time, method, operator confirmation) is audited.

Scanning networks without authorisation may be illegal. The application is not a
penetration-testing tool.

## Backups and recovery

- Back up the database and the `.meta` sidecar together.
- Backups remain encrypted; protect the passphrase separately.
- Test restore procedures before relying on them.
- A future release will provide an encrypted backup command and integrity check.
- Do not copy a live database while it is open unless you use a SQLite/SQLCipher
  backup API or stop the application first.

## Hardening checklist for operators

- Use full-disk encryption in addition to the CMDB encryption.
- Keep the operating system, webview runtime, SQLCipher, and libsodium patched.
- Restrict the database directory to the application user.
- Do not store the database on a world-writable share.
- Use a unique passphrase and a password manager.
- Review imports before committing.
- Authorise scans explicitly and keep scan scope tight.
- Export audit events to an external log if required by policy.
- Code-sign Windows release binaries and publish checksums/SBOM for Linux
  packages.

## Reporting a vulnerability

Do not open a public issue containing a working exploit or sensitive data.
Contact the repository owner privately with:

- the affected version/commit,
- a description of the impact,
- reproduction steps or a proof of concept,
- any suggested mitigation.

## Known limitations

- Single-user local application; no RBAC or server-side authentication.
- Webview memory cannot be fully zeroized.
- The audit chain is local and key-holder-rewritable.
- The embedded draw.io editor is not yet implemented; current exports are
  plaintext files chosen by the user.
- Network scanning and the staging queue are not yet implemented.
- Windows and Linux packaging/signing workflows are not yet complete.
