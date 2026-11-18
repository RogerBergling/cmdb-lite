# Step-by-step installation

This is the end-to-end installation path for the recommended production
topology:

```text
Windows/Linux endpoints
  cmdb-agent (native service)
        │ HTTPS
        ▼
HAProxy / VPN / private ingress
        │ HTTP on loopback or a private address
        ▼
Linux server
  cmdb-server systemd service
    └── cmdb-rpc
          └── encrypted SQLCipher database

Administrator
  browser and/or cmdb desktop client
```

Use a Linux VM or physical server for the CMDB server. Do not run the server and
database as a multi-replica container workload. See
[Docker and container notes](#docker-and-container-notes) for the limited
container case.

The server is Linux-only. Windows is supported for the desktop client and the
application inventory agent.

## 1. Prepare the host

Use a supported Linux server, such as current Ubuntu LTS or Fedora Server, with:

- a stable private IP or DNS name;
- accurate time via chrony or systemd-timesyncd;
- a TLS proxy or VPN ingress;
- local disk for the encrypted database;
- outbound time and package access for installation.

Set a hostname and time zone consistently:

```bash
hostnamectl
timedatectl
```

Do not place the database on an NFS, SMB, or other network filesystem.

## 2. Download and verify the packages

Download the release artifacts for the **same version**:

- server: `cmdb-lite-server_<version>_amd64.deb` or
  `cmdb-lite-server-<version>-*.rpm`;
- agents: `cmdb-lite-agent_<version>_amd64.deb`,
  `cmdb-lite-agent-<version>-*.rpm`, or
  `cmdb-lite-agent-<version>-windows-x64.msi`;
- desktop client: `cmdb-lite_<version>_amd64.deb`,
  `cmdb-lite-<version>-*.rpm`, `cmdb-lite-<version>-linux-x86_64.tar.gz`,
  `cmdb-lite-<version>-windows-x64.msi`, or the Flatpak;
- `SHA256SUMS`.

Verify before installing:

```bash
sha256sum -c SHA256SUMS
```

On Windows:

```powershell
Get-FileHash .\cmdb-lite-agent-<version>-windows-x64.msi -Algorithm SHA256
```

Compare the result with the matching `SHA256SUMS` line.

## 3. Install the server package

Install **either** the desktop+server package **or** the server-only package on
a host, not both. The server-only package is the recommended production choice.

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install ./cmdb-lite-server_<version>_amd64.deb
```

### Fedora

```bash
sudo dnf install ./cmdb-lite-server-<version>-*.rpm
```

Verify the installed build:

```bash
cmdb-server --version
```

The package installs:

```text
/usr/bin/cmdb-server
/usr/bin/cmdb-rpc
/usr/share/cmdb-lite/web/
/usr/lib/systemd/system/cmdb-server.service
/usr/share/doc/cmdb-lite/server.env.example
```

The post-install script creates the unprivileged `cmdb` service user and
`/var/lib/cmdb-lite` with mode `0700`. It does **not** create the passphrase file
or start the service.

## 4. Create the database passphrase file

Create a root-only environment file. The database passphrase is not a user
password and is never given to normal users.

```bash
sudo install -d -o root -g root -m 0700 /etc/cmdb-lite
sudo install -o root -g root -m 0600 \
  /usr/share/doc/cmdb-lite/server.env.example /etc/cmdb-lite/server.env
sudo editor /etc/cmdb-lite/server.env
```

Set a long, unique passphrase:

```bash
CMDB_DB_PASSPHRASE='replace-with-a-long-random-passphrase'
```

Do not put the passphrase in shell history, a command line, a repository, or a
CI variable. The server unsets `CMDB_DB_PASSPHRASE` after reading it.

Record the passphrase in your password manager and store the database backup
separately. It cannot be recovered.

## 5. Create the database and start the server

The server opens an encrypted database; it does not create one implicitly. Create
it once as the `cmdb` service user, using the same passphrase you wrote to
`/etc/cmdb-lite/server.env`:

```bash
sudo -u cmdb /usr/bin/cmdb-server \
  --db /var/lib/cmdb-lite/inventory.cmdb \
  --create-db
```

The command prompts for the database passphrase, creates the `.cmdb` and
`.meta` pair, prints the path, and exits. It refuses to overwrite an existing
database. Keep the passphrase in `/etc/cmdb-lite/server.env` and in your password
manager.

Then enable and start the packaged service:

```bash
sudo systemctl enable --now cmdb-server
sudo systemctl status cmdb-server --no-pager
```

Read the one-time first-run setup token from the journal:

```bash
sudo journalctl -u cmdb-server -n 30 --no-pager
```

The token is printed while no administrator exists. Restrict journal access
during this window.

If you have not configured the TLS proxy yet, use an SSH tunnel for the initial
loopback test:

```bash
ssh -L 8080:127.0.0.1:8080 <user>@<server>
```

Then open `http://127.0.0.1:8080/login` locally.

## 6. Create the first administrator

1. Open the login page through the TLS hostname, or through the SSH tunnel for
   the initial test.
2. Enter the one-time setup token.
3. Create the administrator account with a strong password.
4. Enroll the administrator’s TOTP authenticator.
5. Generate the ten recovery codes and store them offline, separately from the
   database passphrase.

TOTP and at least one unused recovery code are mandatory before inventory access.
The **Security** page enforces this. Create additional accounts from **Users**
only if the deployment needs them.

## 7. Configure TLS and network access

`cmdb-server` has **no TLS listener**. Expose only the HTTPS proxy to users and
agents.

At minimum:

- keep `cmdb-server` on `127.0.0.1:8080` or a private address;
- terminate TLS at HAProxy, nginx, or a VPN ingress;
- set `X-Forwarded-Proto: https`;
- append the client address to `X-Forwarded-For`;
- configure `--proxy` with the proxy CIDR;
- open only the proxy’s HTTPS port to users and agents;
- leave the plain HTTP server port firewalled.

The complete HAProxy example, forwarded-header rules, and hardening checklist
are in [`server.md`](server.md#haproxy-sample).

Do not expose the HTTP listener directly to the internet.

## 8. Install the desktop client (optional)

The server can be administered from a browser. If you also want the native
desktop client:

### Ubuntu / Debian

```bash
sudo apt install ./cmdb-lite_<version>_amd64.deb
cmdb
```

### Fedora

```bash
sudo dnf install ./cmdb-lite-<version>-*.rpm
cmdb
```

### Windows

Install `cmdb-lite-<version>-windows-x64.msi`, or extract the portable ZIP and
run `bin\cmdb.exe`.

### Flatpak

```bash
flatpak install --user ./cmdb-lite.flatpak
flatpak run io.github.rogerbergling.CmdbLite
```

On the unlock screen choose **Connect to server**, enter:

```text
https://cmdb.example.internal
```

The desktop client opens the server UI and does not open a local database. For a
private CA, install the CA in the operating-system trust store.

Do not install the desktop+server package and the server-only package on the same
Linux host.

## 9. Create an agent enrollment token

1. Sign in to the server as an administrator.
2. Open **Applications**.
3. Choose **Enrollment token**.
4. Give the token a label, for example `linux-web` or `windows-servers`.
5. Generate it and copy it immediately.

The token is shown once, expires after one hour, and enrolls one host. The server
stores only its hash.

## 10. Install and enroll Linux agents

Use the package for the target distribution.

### Ubuntu / Kali / Debian family

```bash
sudo apt install ./cmdb-lite-agent_<version>_amd64.deb
```

### Fedora

```bash
sudo dnf install ./cmdb-lite-agent-<version>-*.rpm
```

Enroll as root:

```bash
sudo cmdb-agent enroll \
  --server https://cmdb.example.internal \
  --token '<one-time-token>'
```

For a private CA:

```bash
sudo cmdb-agent enroll \
  --server https://cmdb.example.internal \
  --token '<one-time-token>' \
  --ca-file /etc/pki/ca-trust/source/anchors/cmdb-ca.pem
```

The command writes `/etc/cmdb-lite-agent/agent.json`
(`root:cmdb-agent`, mode `0640`), enables the packaged service, and starts it.
Use `--no-start` if you want to write the configuration without starting the
service.

Verify:

```bash
systemctl status cmdb-agent --no-pager
sudo journalctl -u cmdb-agent -n 30 --no-pager
sudo cmdb-agent status
```

## 11. Install and enroll Windows agents

Install the MSI from an elevated prompt:

```powershell
msiexec /i .\cmdb-lite-agent-<version>-windows-x64.msi
```

Enroll using the installed path:

```powershell
& "C:\Program Files\cmdb-lite-agent\cmdb-agent.exe" enroll `
  --server https://cmdb.example.internal `
  --token "<one-time-token>"
```

The MSI adds the install directory to the system `PATH`; open a new elevated
prompt before using the short `cmdb-agent.exe` command.

Verify:

```powershell
Get-Service cmdb-agent
& "C:\Program Files\cmdb-lite-agent\cmdb-agent.exe" status
```

For unattended deployment, the MSI accepts `SERVER_URL` and
`ENROLLMENT_TOKEN` properties. MSI properties can appear in installation logs;
use a short-lived token, or install without properties and enroll interactively.

Windows 11 and Windows Server 2019 or later are supported. Windows Store/Appx,
per-user `HKCU` applications, Snap, Flatpak, processes, and services are not
collected in this version.

## 12. Verify the installation

On the server:

```bash
sudo systemctl status cmdb-server --no-pager
sudo journalctl -u cmdb-server -n 30 --no-pager
```

In the **Applications** tab:

- the host appears within a few seconds of enrollment;
- the first report shows a non-zero application count;
- the host status is **Active**;
- **Software** shows installed packages or Windows uninstall entries;
- **History** shows the report and change timeline;
- a second report produces a report row and, if software changed, change events.

Run one immediate agent report if needed:

```bash
# Linux
sudo cmdb-agent once

# Windows
& "C:\Program Files\cmdb-lite-agent\cmdb-agent.exe" once
```

A host with a collector warning is marked **Partial** and does not remove
applications that were missing from that report.

## 13. Complete the production checks

Before calling the installation complete:

- [ ] Verify HTTPS through the real proxy; do not use plain HTTP for remote
      clients.
- [ ] Verify the admin TOTP and recovery codes work.
- [ ] Create and revoke a test enrollment token.
- [ ] Enroll one test host from each OS family you will manage.
- [ ] Confirm the agent services restart after a reboot.
- [ ] Set up NTP/chrony and confirm time synchronization.
- [ ] Configure journald retention and access controls.
- [ ] Configure external monitoring. There is no HTTP health endpoint yet; use a
      TCP/proxy check until one exists.
- [ ] Perform a stop/copy/start backup of the `.cmdb` and `.meta` pair.
- [ ] Restore the backup to a test host and verify login, TOTP, recovery codes,
      inventory, and application history.
- [ ] Record the installed versions, commit/build identifiers, and package
      checksums.
- [ ] Test an upgrade and rollback on a copy.
- [ ] Put the database passphrase and recovery codes in separate secure
      locations.
- [ ] Confirm that collecting package inventory is permitted by endpoint and
      privacy policy.

## Backup and upgrade

There is no online backup API. Stop the service before copying the database
pair:

```bash
sudo systemctl stop cmdb-server
sudo install -d -m 0700 /var/backups/cmdb-lite
sudo cp -a /var/lib/cmdb-lite/inventory.cmdb \
           /var/lib/cmdb-lite/inventory.cmdb.meta \
           /var/backups/cmdb-lite/
sudo systemctl start cmdb-server
```

Schedule this for the required RPO and ship the pair off-host. Never `cp` a live
database.

For upgrades, install the new package from the same major release family and
verify:

```bash
sudo systemctl status cmdb-server --no-pager
sudo journalctl -u cmdb-server -n 50 --no-pager
```

Schema migrations create a pre-migration backup and are applied when the
database is opened. See [`upgrading.md`](upgrading.md) and
[`server.md`](server.md#backup-and-restore).

## Troubleshooting

| Symptom | Check |
|---|---|
| Service fails immediately | `journalctl -u cmdb-server -n 50 --no-pager`; verify `/etc/cmdb-lite/server.env` exists, is root-owned `0600`, and contains `CMDB_DB_PASSPHRASE`. |
| Login page unavailable through proxy | Check proxy upstream, `--proxy` CIDR, `X-Forwarded-Proto`, and that the server is listening on the expected loopback/private address. |
| Setup token missing | The server prints it only while no users exist; check the journal at startup. |
| TOTP rejected | Check server time synchronization; only one 30-second step of skew is accepted. |
| Agent enrollment returns 401 | The token expired, was used, or was revoked. Create a new token. |
| Agent enrollment returns 503 | The server has no administrator yet; complete first-run setup. |
| Agent cannot verify TLS | Install the internal CA in the OS trust store or pass `--ca-file`. |
| Agent has no applications | Check the collector warning in **History**; Fedora needs a readable RPM database, and Windows only collects machine-wide uninstall entries. |
| Host is Stale | No report for more than 48 hours; check the agent service and network path. |
| Host is Partial | The collector could not read the complete package database; current applications are retained. Check the report warning. |

## Docker and container notes

Docker is not the recommended production deployment for this release. There is
no container image in the repository, and the server is a single-node stateful
service with a file database, a child `cmdb-rpc` process, a required passphrase,
no TLS listener, no health endpoint, and no online backup API.

If your organisation mandates containers:

- run exactly one replica;
- use a local persistent volume for `/var/lib/cmdb-lite`;
- keep the HTTP port unpublished and terminate TLS at a host or sidecar proxy;
- provide the passphrase through a secret or root-only file;
- include `nmap` in the image if server-side scanning is required;
- stop the container before copying the database pair;
- do not use Kubernetes Deployments, Swarm scaling, or multiple replicas;
- do not run `cmdb-agent` in a container to inventory the host.

For a normal installation, use the systemd service described above.

## Documentation map

- [`server.md`](server.md) — full server runbook, HAProxy, permissions,
  backup/restore, monitoring, hardening.
- [`agent.md`](agent.md) — collector details, enrollment API, service behavior,
  retention, troubleshooting.
- [`security.md`](security.md) — threat model and security controls.
- [`upgrading.md`](upgrading.md) — upgrade and rollback.
- [`windows.md`](windows.md) — Windows desktop and agent packaging details.
