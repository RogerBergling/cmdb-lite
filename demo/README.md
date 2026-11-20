# Public demo image

This directory builds a self-contained, disposable demo of `cmdb-lite` for a
container host. It runs the real `cmdb-server`, the real C core through
`cmdb-rpc`, and the real web UI against a fabricated encrypted database.

> **This is not a production deployment.** The demo account, database
> passphrase, and TOTP secret are public. The database contains fake data only
> and is reset automatically. Never point this image at real inventory, real
> credentials, or a real network.

## Demo login

The login page shows these values and the current TOTP code:

| Field | Value |
|---|---|
| Username | `guest` |
| Password | `guest` |
| TOTP secret | `JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP` |
| Recovery codes | `DEMO2AAAAAA2`, `DEMO2BBBBBB3`, `DEMO2CCCCCC4`, `DEMO2DDDDDD5`, `DEMO2EEEEEE6`, `DEMO2FFFFFF7`, `DEMO2GGGGGG2`, `DEMO2HHHHHH3`, `DEMO2IIIIII4`, `DEMO2JJJJJJ5` |

The `guest` account is an administrator so the Users, Applications, and
Security pages can be explored. Its password, TOTP, recovery codes, and enabled
state are protected in the demo build; other user-management flows (creating,
disabling, and resetting other accounts) still work.

The TOTP secret is public by design. Add it to any authenticator app, or use the
current code shown on the login page. Recovery codes are single-use and are
restored when the demo resets.

## What is seeded

- 60 configuration items across servers, virtual machines, network devices,
  applications, databases, cloud services, technical services, and business
  services.
- 102 typed relationships with all five impact classes.
- 8 application-inventory hosts with current software and a before/after
  history, linked to their matching CIs.
- A saved draw.io-compatible dependency diagram.
- Two non-admin accounts (`alice`, disabled `bob`) for the Users page.
- Audit entries from the seeding run.

The Discovery tab uses a fake `nmap` shim. It returns deterministic TEST-NET-1
results so the scan, review, and import flow can be exercised without touching
the network. The container does not include real `nmap`.

## Run locally

From the repository root, either use the convenience script:

```bash
./demo/run.sh
```

or use Compose (copy `demo/.env.example` to `demo/.env` first if you want to
change the proxy CIDR, reset interval, or session timeouts):

```bash
cp demo/.env.example demo/.env
docker compose -f demo/compose.yaml up --build
```

Open <http://127.0.0.1:8080/login> and sign in with `guest` / `guest`. The login
page displays the TOTP secret and the current six-digit code.

The equivalent plain `docker run` is:

```bash
docker build -f demo/Dockerfile -t cmdb-lite-demo .
docker run --rm -p 127.0.0.1:8080:8080 \
  --read-only --tmpfs /tmp:rw,noexec,nosuid,size=64m,mode=1777 \
  --memory 1g --pids-limit 256 \
  --security-opt no-new-privileges:true --cap-drop ALL \
  -v cmdb-demo-data:/data \
  cmdb-lite-demo
```

### Publish on a different host port

The container always listens on **8080**, and the image healthcheck targets
8080. To publish the demo on host port 8012, set these in `demo/.env`:

```dotenv
CMDB_HTTP_BIND=0.0.0.0
CMDB_HTTP_PORT=8012
CMDB_LISTEN=0.0.0.0:8080
```

Do **not** set `CMDB_LISTEN=0.0.0.0:8012`: that changes the container-side port
while the mapping still points at 8080, so the browser and healthcheck cannot
connect. Then recreate the container and test:

```bash
docker compose -f demo/compose.yaml up -d --force-recreate
curl -fsS http://127.0.0.1:8012/login | head
```

For a public host, also allow the port through the host firewall and put a TLS
reverse proxy in front of it.

The image runs as UID 10001 and never as root. Docker initializes the named
volume from the image's `/data` directory, which is owned by 10001. If you use a
bind mount instead, create the host directory with
`install -d -o 10001 -g 10001 -m 0700` so the container can write to it. If you
reuse a volume that was previously created by a root-owned container, remove it
or run a one-off `chown -R 10001:10001` on it.

The named volume is re-used across restarts, but the entrypoint deletes and
recreates the database on every start. `docker compose down -v` removes the
volume entirely.

## Image publishing

CI publishes `ghcr.io/rogerbergling/cmdb-lite-demo` from the same commit:

- `latest` and the version/`major.minor` tags on `v*` releases;
- `edge` and `main` on the default branch;
- `sha-<commit>` on every non-PR build;
- PR builds are compiled but not pushed.

The compose example uses `:main`, so it works before the next release tag. Switch
to `:latest` if you want the newest tagged release instead of the current
default-branch build.

The package must be made public once in the GitHub package settings after the
first push. The workflow uses `GITHUB_TOKEN` with `packages: write`. If you keep
the package private, run `docker login ghcr.io` on the demo host before pulling.
CI currently builds `linux/amd64`; add an `arm64` build platform or build the
image natively if the host is ARM.

Because the demo is part of this repository, authentication changes, schema
changes, and UI changes update the demo image in the same CI run. The production
`cmdb-server` does not contain the demo banner or relaxed login behavior: those
are compiled only with `-tags demo` (CMake option `CMDB_SERVER_DEMO=ON`).

## Deployment notes

- Run **exactly one replica**. `cmdb-server` is single-process, one database,
  and one `cmdb-rpc` child. Sessions are in memory.
- Terminate TLS at the host or a sidecar proxy. `cmdb-server` has no TLS
  listener; do not publish port 8080 to the internet.
- Set `CMDB_PROXY` to the exact proxy CIDR. The server trusts
  `X-Forwarded-For` and `X-Forwarded-Proto` only from those addresses, and uses
  the rightmost forwarded address for rate limiting.
- Keep at least 1 GiB of memory. The database KDF uses 256 MiB and each password
  verification uses 64 MiB.
- Add `X-Robots-Tag: noindex, nofollow` at the proxy. The demo is public and
  should not be indexed.
- Keep the container read-only, drop all capabilities, and mount only `/data`
  and `/tmp`. The image runs as UID 10001. The image does not need outbound
  network access.
- The demo resets every 24 hours by default (`CMDB_DEMO_RESET_HOURS`). Set it to
  `0` to disable timed resets; the database is still recreated on every
  container start (`CMDB_DEMO_RESET_ON_START=0` disables that too).

## Security boundaries

- The demo password and TOTP secret are intentionally public. They must never be
  reused outside the demo.
- The database passphrase is a public constant. The encrypted database is fake;
  do not mount real `.cmdb` files or share a volume with a real deployment.
- The fake `nmap` shim is installed at `/opt/demo/fake-nmap` and selected with
  `CMDB_NMAP`. No real scanning binary is present.
- The `guest` credential-change and disable paths are blocked by the demo build.
  A visitor can still change inventory, import data, create users, reset other
  users, and create agent tokens. The 24-hour reset removes those changes.
- The login page is served with a demo banner; the production binary is built
  without `demo.go`.
- The image is not signed by default beyond GitHub Actions provenance/SBOM
  attestations. Add cosign verification in the deployment pipeline if needed.

## Supported flows and intentional gaps

Supported: password login, TOTP, recovery-code login, sessions, inventory CRUD,
CI types and custom fields, categories, relationships and impact, dependency
map, CSV import preview/commit, diagrams, simulated discovery and review,
applications/agent history and enrollment tokens, audit list/verify, users,
and the Security page.

Not available in server mode (unchanged from the product): create/open
database, backup/restore, lock, native file dialogs, CSV/JSON export, and
`.drawio` download. Those are desktop-local features. The UI hides them after a
server login.
