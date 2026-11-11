# Server deployment

This document is the operator guide for running `cmdb-server` in production. It
covers a single-node Linux deployment. The server is not a clustered or
high-availability product: one `cmdb-server` process owns one database through
one `cmdb-rpc` child.

> **Status:** v0.7.0 ships server-only DEB/RPM/TGZ packages with a hardened
> systemd unit and a passphrase example. It does not yet ship a separate
> flags/config file, an online backup API, or a health endpoint. The samples
> below cover the packaged unit and the remaining operational gaps. Validate
> them on your target distribution before go-live.

## What is installed

A normal Linux package built with the defaults installs:

```text
/usr/bin/cmdb                 desktop client
/usr/bin/cmdb-server          Go HTTP/auth server
/usr/bin/cmdb-rpc             C stdio bridge used by the server
/usr/share/cmdb-lite/web/     index.html, app.js, style.css
```

All four must come from the same release. The server auto-detects `cmdb-rpc`
next to its executable and the web assets under `../share/cmdb-lite/web`.
Override them with `--rpc` and `--web-dir` only when necessary.

Flatpak and Windows packages contain only the desktop client. A Windows desktop
client can connect to a Linux server with `CMDB_SERVER_URL`; Windows server
support is not implemented.

## Install the server-only package

v0.7.0 adds server-only Linux packages that omit the desktop client and
GTK/WebKit dependencies:

```bash
# Debian/Ubuntu
sudo apt install ./cmdb-lite-server_0.7.0_amd64.deb

# Fedora
sudo dnf install ./cmdb-lite-server-0.7.0-*.rpm
```

The package installs the systemd unit at
`/usr/lib/systemd/system/cmdb-server.service`, the passphrase example at
`/usr/share/doc/cmdb-lite/server.env.example`, and creates the `cmdb` service
user plus `/var/lib/cmdb-lite` (mode `0700`). It does not create the passphrase
file or start the service automatically. Follow the first-start procedure
below.

Do not install the desktop+server package and the server-only package on the
same host: they share `cmdb-server`, `cmdb-rpc`, web assets, and the systemd
unit.

## Roles and prerequisites

- A dedicated, unprivileged service account, for example `cmdb`.
- A root-owned environment file for the database passphrase, or an interactive
  start for the first validation.
- A private state directory such as `/var/lib/cmdb-lite`, owned by `cmdb:cmdb`
  and mode `0700`.
- HAProxy, nginx, or a VPN ingress that terminates TLS. `cmdb-server` has no TLS
  listener.
- Accurate system time, for example `chrony` or `systemd-timesyncd`. TOTP accepts
  only one 30-second step of clock skew.
- `nmap` on the server if network discovery will be used. Unprivileged
  `-sT`/`-sV` works; `-O` requires root and is normally unavailable to the
  service account.

Do not expose the HTTP listener directly to the internet.

## File permissions

The C core sets the database and sidecar to `0600` after opening, but the
process umask still matters for newly created WAL/SHM files and backups. Run the
service with `UMask=0077` and keep the parent directory `0700`.

```bash
sudo install -d -o cmdb -g cmdb -m 0700 /var/lib/cmdb-lite
sudo install -d -o root -g root -m 0700 /etc/cmdb-lite
sudo install -o root -g root -m 0600 /dev/null /etc/cmdb-lite/server.env
```

`/etc/cmdb-lite/server.env` contains the database passphrase:

```bash
CMDB_DB_PASSPHRASE='replace-with-a-long-random-phrase'
```

Keep the file root-owned and mode `0600`; systemd reads it as root and passes
the variable to the service. Never put the passphrase in a shell profile,
command line, repository, or CI variable.
The server unsets `CMDB_DB_PASSPHRASE` after reading it. A systemd credential or
root-only key file is a future improvement.

## systemd sample

This unit is hardened for a server that does not need `nmap -O` or elevated
capabilities. If you need active OS detection, do not run the whole service as
root; use a separate privileged scanner workflow instead.

```ini
[Unit]
Description=cmdb-lite authenticated CMDB server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=cmdb
Group=cmdb
WorkingDirectory=/var/lib/cmdb-lite
StateDirectory=cmdb-lite
StateDirectoryMode=0700
UMask=0077
EnvironmentFile=/etc/cmdb-lite/server.env
ExecStart=/usr/bin/cmdb-server \
  --db /var/lib/cmdb-lite/inventory.cmdb \
  --listen 127.0.0.1:8080 \
  --proxy 127.0.0.1/32 \
  --session-idle 30m \
  --session-absolute 12h
Restart=on-failure
RestartSec=5s
TimeoutStopSec=30s
NoNewPrivileges=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/lib/cmdb-lite
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6
RestrictNamespaces=yes
RestrictSUIDSGID=yes
LockPersonality=yes
CapabilityBoundingSet=
SystemCallArchitectures=native
LimitNOFILE=8192
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

`StateDirectory` creates `/var/lib/cmdb-lite` with the configured ownership and
mode. Adjust the path and proxy CIDR for your host. If HAProxy runs on a
different host, replace `127.0.0.1/32` with the proxy address or network and
update `ReadWritePaths` if you change the database path. Validate the unit with
`systemd-analyze verify` on the target host.

There is no `ExecReload`; restarting the service logs every user out because
sessions are memory-only. The C core checkpoints the WAL when the child exits.

## HAProxy sample

The packaged unit binds `127.0.0.1:8080` for a same-host TLS proxy. If you need
direct access from a private LAN without a proxy, change `--listen` to the
server's private IP and add that network to your firewall; this sends passwords
and session cookies over plain HTTP. Prefer HAProxy/VPN with TLS even on a
private network.

The server needs the proxy to set `X-Forwarded-Proto` so the session cookie is
marked `Secure`, and to append the client address to `X-Forwarded-For` so rate
limiting sees the real client. Add the proxy address/CIDR to `--proxy`.

```haproxy
global
  log /dev/log local0
  maxconn 2048

defaults
  mode http
  option httplog
  option forwardfor
  timeout connect 5s
  timeout client 1m
  timeout server 5m

frontend cmdb_https
  bind :443 ssl crt /etc/haproxy/certs/cmdb.pem alpn h2,http/1.1
  http-request set-header X-Forwarded-Proto https
  # Add VPN/source restrictions here if the service is not VPN-only.
  default_backend cmdb_server

backend cmdb_server
  mode http
  http-reuse never
  server cmdb1 127.0.0.1:8080 check inter 5s fall 3 rise 2
```

Notes:

- `option forwardfor` appends the client address; the server uses the rightmost
  entry. Do not let untrusted clients reach the backend directly.
- `http-request set-header X-Forwarded-Proto https` is required for Secure
  cookies when TLS terminates at HAProxy.
- Keep the backend on loopback or a private interface. Firewall port 8080 from
  everything except the proxy.
- Add HSTS, TLS policy, and access controls at the proxy. The server does not set
  HSTS itself.
- There is no application health endpoint yet. The `check` above is a TCP check.
  An HTTP-specific check can be added later.
- Long imports and scans are serialized with all other API requests. Keep proxy
  server timeouts generous enough for legitimate large imports.

## First start and accounts

1. Start the service while the database has no users.
2. Read the one-time setup token from the journal:

   ```bash
   journalctl -u cmdb-server -n 20 --no-pager
   ```

   The setup token is printed on every start while no users exist. Restrict
   journal access during this window.

3. Open `https://your-host/login`, enter the token, and create the first
   administrator.
4. Create normal accounts from the **Users** page.
5. Enroll TOTP and generate recovery codes from **Security** for every
   administrator. Store recovery codes offline, separately from the database
   passphrase.

Current account gaps:

- The Users page can create administrator or non-admin accounts, but there is
  no role change or user deletion after creation. This deployment uses one
  administrator; document and test a break-glass procedure for a lost password,
  lost TOTP device, and exhausted recovery codes.
- TOTP and at least one unused recovery code are mandatory before inventory
  access. The security page guides enrollment and blocks the inventory until
  both are complete.
- Admin-created and admin-reset passwords are temporary and force a change at
  first login. The administrator knows the temporary password until the user
  replaces it.
- There is no user deletion, role change, active-session list, or forced
  session revocation UI. Disabling a user, resetting a password, or resetting
  MFA revokes that user's sessions.
- Sessions are memory-only. Restarting the service logs everyone out.
- There is no RBAC beyond the admin flag. All non-admin accounts can edit the
  full inventory.

## Break-glass account reset

This deployment has one administrator. If the password, TOTP device, and all
recovery codes are lost, the only safe recovery path is an offline auth-state
reset using the database passphrase. This preserves inventory and audit data but
deletes all accounts, TOTP secrets, and recovery-code hashes.

```bash
sudo systemctl stop cmdb-server
# Run interactively so the server prompts for the passphrase. Do not put the
# passphrase on the command line.
sudo -u cmdb /usr/bin/cmdb-server \
  --db /var/lib/cmdb-lite/inventory.cmdb --reset-auth
sudo systemctl start cmdb-server
sudo journalctl -u cmdb-server -n 20 --no-pager
```

If you must run non-interactively, use `systemd-run --wait --pipe` with
`--property=EnvironmentFile=/etc/cmdb-lite/server.env` and `--uid=cmdb`; do not
expand the passphrase into the command line.

The command does not start the HTTP listener. It clears `auth.state`, writes a
`reset-auth-state` audit entry with actor `break-glass`, and exits. On the next
start the server prints a new first-run setup token. The new administrator must
repeat TOTP enrollment and recovery-code generation.

Protect the database passphrase accordingly: anyone who can run this command can
take over the inventory. Test the procedure on a copy before relying on it, and
store recovery codes offline separately from the passphrase.

## Backup and restore

There is no online backup API in this release. A consistent backup requires a
short service stop:

```bash
sudo systemctl stop cmdb-server
sudo install -d -m 0700 /var/backups/cmdb-lite
sudo cp -a /var/lib/cmdb-lite/inventory.cmdb \
           /var/lib/cmdb-lite/inventory.cmdb.meta \
           /var/backups/cmdb-lite/
sudo systemctl start cmdb-server
```

The pair includes `auth.state` inside the encrypted database: accounts, Argon2id
hashes, TOTP secrets, and recovery-code hashes. Sessions are not included.

For a 24-hour RPO, schedule this stop/copy/start window daily (cron, systemd timer,
or your backup system) and ship the pair off-host. Because sessions are
memory-only, every backup window also logs users out.

Test a restore on a separate path and a separate server instance before relying
on the procedure. Never copy a live database with `cp`; use the stop/copy
procedure or a filesystem snapshot that is consistent with SQLite WAL.

## Upgrade and rollback

1. Stop the service.
2. Back up the `.cmdb` and `.meta` pair.
3. Install the new package.
4. Start the service and watch the journal.
5. Log in, verify inventory and relationships, and run **Audit verify**.
6. Keep the previous package and backup until validation is complete.

The v0.7.0 schema is unchanged. Future schema migrations are performed by the C
core on open and create a pre-migration backup next to the database before
migrating; the server logs the `db.open` result. Restarting the server invalidates
all sessions. Rollback means restoring the database pair and the previous
package; never open a migrated database with an older binary.

## Monitoring, logs, and availability

- The server logs to stderr, normally captured by journald. There is no
  structured request log or access log by default.
- Authentication and inventory changes are in the encrypted `audit_log` table,
  not in the system log. Export or forward audit events if policy requires it.
- There is no health or readiness endpoint. Use a TCP check at the proxy until
  one is added.
- There is no metrics endpoint, request ID, or tracing.
- One server process handles one database. API requests are serialized; a long
  import, scan start, or audit verification can block other users. There is no
  horizontal scaling or active/active deployment.
- The session and rate-limit maps are in memory and are not capped. A restart
  clears rate limits and sessions.
- The database is a single SQLite file. Do not place it on NFS, SMB, or a shared
  volume. Do not run two server processes against it.

## Capacity notes

- Login password verification uses Argon2id with 64 MiB and two lanes.
- The database passphrase uses Argon2id through libsodium with 256 MiB.
- `auth.state` is capped at 1 MiB. Each user consumes one Argon2id password hash
  plus up to ten recovery-code hashes. Plan the account count accordingly.
- The audit log grows without automatic retention.
- Imports are capped at 20 MiB / 10,000 rows. API requests are capped at 64 MiB.
- Graph traversal is capped at 500 nodes, 5 levels, and 5,000 edges.

## Hardening checklist

- [ ] Dedicated service account; no shell; no root.
- [ ] `/var/lib/cmdb-lite` mode `0700`; process `UMask=0077`.
- [ ] Root-only passphrase file or interactive start.
- [ ] Service bound to loopback or a private interface.
- [ ] TLS termination at HAProxy/VPN; HTTP port firewalled.
- [ ] `--proxy` includes the exact proxy address/CIDR.
- [ ] Proxy sets `X-Forwarded-Proto: https` and appends `X-Forwarded-For`.
- [ ] System time synchronized; TOTP skew monitored.
- [ ] One administrator with a documented, tested break-glass procedure (or a
      second administrator if you change the decision).
- [ ] TOTP and recovery codes enrolled for every administrator.
- [ ] Recovery codes stored offline; database passphrase stored separately.
- [ ] Backup and restore drill completed, including login and audit verification.
- [ ] Server upgrade and rollback drill completed.
- [ ] Journald retention and access restricted; setup token not shipped off-host.
- [ ] Firewall and VPN access reviewed; no direct internet exposure.
- [ ] Dependency and Go vulnerability scanning in the release process.
- [ ] Load and failure testing with expected user and inventory counts.
- [ ] Application health/readiness monitoring added or accepted as a gap.
- [ ] Verify the packaged unit with `systemd-analyze verify` on the target host.
