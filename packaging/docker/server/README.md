# Production server image

This directory builds the server-only production image:

```text
ghcr.io/rogerbergling/cmdb-lite-server
```

It is not the demo image. It contains no demo account, fixtures, fake `nmap`,
or reset loop. It contains the real Go server, `cmdb-rpc`, the web UI, and
`nmap` for discovery.

The deployment target is a single-node Linux server behind an external TLS
proxy. One container owns one SQLCipher database through one `cmdb-rpc` child.
Do not run multiple replicas against the same volume.

## Quick start

From this directory:

```bash
cp .env.example .env
install -d -m 0700 secrets
openssl rand -base64 48 | install -m 0444 /dev/stdin secrets/cmdb_db_passphrase
docker compose run --rm cmdb-server \
  --db /data/inventory.cmdb --create-db
docker compose up -d
docker compose logs cmdb-server | grep "FIRST-RUN SETUP TOKEN"
```

Open `https://your-host/login` through the TLS proxy, enter the setup token,
and create the first administrator. The token is printed while no users exist;
restrict access to the container logs during that window.

The secret file must be readable by the container user. The Compose secret is
mounted read-only under `/run/secrets/cmdb_db_passphrase`; the entrypoint reads
it into `CMDB_DB_PASSPHRASE` and the server unsets its copy after opening the
database.

## External TLS proxy

The image has no TLS listener and must not be published directly. Bind it to
loopback or a private address and let the proxy terminate TLS.

The proxy must:

- set `X-Forwarded-Proto: https` so the session cookie is marked `Secure`;
- append the client address to `X-Forwarded-For`;
- not log the `Authorization` header or agent report bodies.

Set `CMDB_PROXY` to the proxy source address or CIDR **as seen by the
container**. A proxy on the same Docker host often appears as
`172.17.0.1`; a proxy container appears with its container-network address.
The example `.env` uses `172.16.0.0/12`; narrow it to the actual proxy.

See [`docs/server.md`](../../../docs/server.md) for the HAProxy sample, systemd
hardening, permissions, and the full production checklist.

## Ports

The container always listens on **8080**. Use `CMDB_HTTP_PORT` for the host
port:

```dotenv
CMDB_HTTP_BIND=127.0.0.1
CMDB_HTTP_PORT=8012
CMDB_LISTEN=0.0.0.0:8080
```

Do not change `CMDB_LISTEN` to the host port. The healthcheck follows the
container port automatically, but the Compose mapping still targets 8080.

## Optional passkeys

Set both values in the `.env` file copied from `.env.example` to the public
HTTPS origin browsers use:

```dotenv
CMDB_WEBAUTHN_RP_ID=cmdb.bergling.net
CMDB_WEBAUTHN_ORIGIN=https://cmdb.bergling.net
```

`CMDB_WEBAUTHN_RP_ID` is the hostname only, without a scheme or port.
`CMDB_WEBAUTHN_ORIGIN` must be the exact public HTTPS origin. Changing either
value makes previously registered passkeys unusable. Comment both out to
disable passkeys. TOTP and recovery codes remain mandatory and available as
fallbacks.

## Health and shutdown

- `GET /healthz` and `GET /readyz` return `{"ok":true}` without authentication
  and without exposing inventory data.
- The image healthcheck uses `/healthz`.
- `SIGTERM` now performs an HTTP shutdown with a 10-second grace period before
  the process exits. Compose allows 30 seconds.

## Data, backup, and restore

The database is `/data/inventory.cmdb` plus the required
`/data/inventory.cmdb.meta` sidecar on the `cmdb-data` volume. A consistent
backup requires a short stop:

```bash
docker compose stop cmdb-server
docker compose run --rm --no-deps --entrypoint sh cmdb-server \
  -c 'tar -C /data -czf - .' > cmdb-backup.tgz
docker compose start cmdb-server
```

Restore into an empty or stopped deployment:

```bash
docker compose stop cmdb-server
docker compose run --rm --no-deps --entrypoint sh cmdb-server \
  -c 'tar -C /data -xzf -' < cmdb-backup.tgz
docker compose start cmdb-server
```

The backup includes accounts, TOTP secrets, recovery-code hashes, agent tokens,
and the audit log. Store it separately from the passphrase. Never copy a live
SQLite database with `cp`; stop the service first.

## Upgrade

1. Back up the volume pair.
2. Change the image tag to the new immutable version or digest.
3. `docker compose pull`
4. `docker compose up -d`
5. Watch the logs for the migration result and verify login/inventory/audit.

Sessions are memory-only, so every restart logs users out. Schema migrations
run automatically on open and create a pre-migration backup next to the
database.

## Discovery and nmap

`nmap` is installed and the container runs as UID 10001, so:

- `-sT` and `-sV` discovery work without extra capabilities;
- OS detection (`-O`) is unavailable because it requires root;
- the container needs network reachability to the targets you are authorized
  to scan;
- restrict container egress with the host firewall or a dedicated network.

Do not grant `NET_RAW`, `NET_ADMIN`, or root to enable `-O`. Use a separate
scanner workflow if OS detection is required.

## Image tags

- `main` / `edge`: builds from the default branch, for testing;
- `sha-<commit>`: immutable build;
- `0.7.3`, `0.7`, `latest`: published from release tags;
- pin a version or digest in production; do not use `latest` blindly.

The package is public. The image contains no secrets; the database passphrase
is supplied at runtime through the Compose secret.

## Security checklist

- [ ] External TLS proxy; no direct public access to 8080.
- [ ] `CMDB_PROXY` narrowed to the actual proxy address/CIDR.
- [ ] Secret file readable by the container user, not committed, not in `.env`.
- [ ] Named volume on local storage; not NFS/SMB/shared.
- [ ] Exactly one replica.
- [ ] Container read-only, capabilities dropped, memory/PID limits set.
- [ ] Logs restricted while the first-run setup token exists.
- [ ] Backup and restore drill completed.
- [ ] Upgrade/rollback drill completed.
- [ ] Host firewall restricts discovery egress to authorized networks.
