# Application inventory agent

> **Status: implemented in v0.7.0 (unreleased).** The agent reports installed
> application inventory to a `cmdb-server` and stores current software plus a
> bounded change history in the encrypted database.

For the complete server-to-agent walkthrough, including first-run setup and TLS,
see [`installation.md`](installation.md). This document is the detailed
reference for the agent itself.

## Purpose

`cmdb-agent` is an unattended service for Windows and Linux hosts. It collects
installed applications and reports them to one `cmdb-server`. Administrators see
managed hosts, current software, and an added/removed/updated timeline in the
**Applications** tab.

The agent is intentionally read-only on the host. It does not accept remote
commands, install or remove software, read application data, or collect files,
passwords, tokens, command lines, or user documents.

## Supported platforms

| Platform | Collector | Package |
|---|---|---|
| Windows 11, Windows Server 2019 and later | 64-bit and 32-bit uninstall registry views (`HKLM`) | MSI and portable ZIP |
| Ubuntu, Kali, Debian family | `/var/lib/dpkg/status` installed stanzas | DEB, TGZ |
| Fedora | `rpm -qa` with a fixed query format | RPM, TGZ |

CI builds x86_64 artifacts. Windows Store/Appx, Snap, Flatpak, per-user `HKCU`
applications, running processes, and installed services are **not** collected in
this version. Windows applications installed only for one user are therefore
invisible to the service account.

## Data collected

Each report contains host facts and an application list:

- host: display name, hostname, machine ID (`/etc/machine-id` or Windows
  `MachineGuid`), OS family/name/version, architecture, agent version;
- application: stable source key, name, version, publisher, source (`deb`,
  `rpm`, or `windows`), architecture, install date where available, install
  location where available, and size in KiB.

Linux package keys are `deb:<package>:<architecture>` and
`rpm:<name>:<architecture>`. Windows keys are
`win:<architecture>:<uninstall-subkey>`, so a version upgrade updates the same
record instead of creating a duplicate. Windows updates, hotfixes, system
components, and child update entries are filtered out.

Reports are limited to 20,000 applications and 32 MiB per request. Application
fields are bounded, and control/Unicode format characters are removed or
rejected before storage.

## Architecture

```text
cmdb-agent (Windows service or systemd)
  collector ──► bounded report JSON
       │
       │ HTTPS + Bearer per-host token
       ▼
cmdb-server (Go)
  /api/v1/agent/enroll  (one-time enrollment token)
  /api/v1/agent/report  (per-host token)
       │ JSON lines over stdio
       ▼
cmdb-rpc ──► C core ──► SQLCipher database
  agents · agent_reports · agent_apps · agent_app_events
```

The server exposes no generic RPC method to agents. Enrollment and reporting use
dedicated handlers; the C core authenticates the token and performs the diff and
retention in one transaction.

## Enrollment and installation

Agents require server mode. A desktop-local database has no listener, although a
copy of a server database can still be opened locally to inspect agent data.

1. Sign in to `cmdb-server` as an administrator.
2. Open **Applications** and choose **Enrollment token**.
3. Create a token. It is shown once, expires after one hour, and enrolls one
   host.
4. Run the enrollment command on the host.

The agent exchanges the one-time enrollment token for a unique per-host token.
The server stores only the token hash. Re-enrolling the same machine ID rotates
the token and preserves the existing host record, current software, history, and
CI link.

### Linux

Install the packaged service, then enroll as root:

```bash
sudo apt install ./cmdb-lite-agent_0.7.0_amd64.deb
# or: sudo dnf install ./cmdb-lite-agent-0.7.0-*.rpm

sudo cmdb-agent enroll \
  --server https://cmdb.example.internal \
  --token <one-time-token>
```

The `enroll` command writes `/etc/cmdb-lite-agent/agent.json`
(`root:cmdb-agent`, mode `0640`), enables the packaged
`cmdb-agent.service`, and starts it. Use `--no-start` to write the configuration
without starting the service. A private CA can be supplied with
`--ca-file /etc/pki/...`.

The service runs as the unprivileged `cmdb-agent` user with an empty capability
set, `NoNewPrivileges`, a read-only system view, and a restricted address
family list. It reads the local package database and sends HTTPS requests.

The DEB/RPM post-install scripts create the `cmdb-agent` user/group and the
`/etc/cmdb-lite-agent` directory. If you deploy the TGZ manually, create the
same user/group before running `enroll` as root; otherwise the configuration
cannot be made readable by the service.

### Windows

Install the MSI and enroll from an elevated prompt:

```powershell
msiexec /i .\cmdb-lite-agent-0.7.0-windows-x64.msi
& "C:\Program Files\cmdb-lite-agent\cmdb-agent.exe" enroll `
  --server https://cmdb.example.internal `
  --token <one-time-token>
```

The MSI adds the install directory to the system `PATH`; open a new elevated
prompt before using the short `cmdb-agent.exe` command.

The command writes `%ProgramData%\cmdb-lite\agent.json` with an ACL restricted to
`SYSTEM` and `Administrators`, creates the `cmdb-agent` Windows service, and
starts it. The service runs as `LocalSystem` because it reads the machine-wide
uninstall registry.

For automated deployment, the MSI also accepts public properties:

```powershell
msiexec /i .\cmdb-lite-agent-0.7.0-windows-x64.msi /qn `
  SERVER_URL="https://cmdb.example.internal" `
  ENROLLMENT_TOKEN="<one-time-token>"
```

MSI properties can appear in installation logs. Use a short-lived one-time token
and revoke it if the log is retained. For high-security deployments, install
without properties and run the installed `cmdb-agent.exe enroll` interactively
instead.

The MSI stops a running agent during an upgrade, replaces the signed binary, and
restarts the service without re-enrolling. Uninstall stops and removes the
service and deletes `%ProgramData%\cmdb-lite\agent.json` on a full uninstall.

### Portable archive

The Windows ZIP and Linux TGZ contain `cmdb-agent` without a service
registration. Use them for testing or for configuration management that owns
service registration itself.

## Service behaviour

- A report is sent immediately on service start.
- The server advertises a reporting interval (default 24 hours); the agent
  applies ±10% jitter and stores the interval in its configuration.
- Failed reports retry after 5 minutes, doubling to a maximum of 60 minutes.
- Revoked tokens return HTTP 401. The service logs the failure and keeps
  retrying at the capped interval; re-enroll the host with a new token.
- The agent logs to the systemd journal or the Windows event log through the
  service manager. Tokens are never logged.

## Server API

Unauthenticated agent endpoints are deliberately narrow:

| Method | Path | Authentication |
|---|---|---|
| `POST` | `/api/v1/agent/enroll` | one-time enrollment token in the JSON body |
| `POST` | `/api/v1/agent/report` | `Authorization: Bearer <per-host-token>` |

The enrollment token and per-host token are 256-bit random values, hex encoded.
Only their BLAKE2b-256 hashes are stored. The server rate-limits enrollment and
reporting, rejects oversized bodies, validates every field, and returns generic
authentication errors.

TLS is required for non-loopback URLs. Terminate TLS at HAProxy/VPN as described
in [`server.md`](server.md). The agent verifies the system trust store and can
append a private CA bundle with `--ca-file`. Plain HTTP is accepted only for
`localhost`/loopback development.

Administrators manage enrollment and agent records through the existing
authenticated RPC path:

| RPC method | Admin | Purpose |
|---|---|---|
| `agents.token_create` | yes | Create a one-time enrollment token. |
| `agents.token_list` | yes | List token metadata, never plaintext tokens. |
| `agents.token_revoke` | yes | Revoke an unused enrollment token. |
| `agents.list` | no | List managed hosts. |
| `agents.get` | no | Host detail and current applications. |
| `agents.history` | no | Reports and application change events. |
| `agents.link_ci` | no | Link or unlink a CI. |
| `agents.revoke` | yes | Revoke a host token. |
| `agents.prune` | yes | Apply retention immediately. |

## History and retention

The database keeps:

- the current application row for each host and application key;
- one compact metadata row per report (counts and collector warnings);
- append-only events for applications that were added, removed, or updated;
- stable first-seen and last-seen timestamps for each current application.

Defaults are **365 days** and **90 reports per host**. The server applies these
values on every report; change them with `--agent-retention-days` and
`--agent-reports-per-host`. Current application rows are not deleted; only old
report and event history is pruned.

The **Applications** tab shows the host list, current software with search,
linked/suggested CI, the change timeline, and the report list. Removing an
application from a host generates a `removed` event and removes it from current
software; reinstalling it generates a new `added` event. Reports that could not
read the complete package database are marked **partial** and do not remove
applications that were absent from that report.

## Security notes

- The agent is a read-only collector. It executes only `rpm` with a fixed argv
  (Linux) and reads registry keys directly (Windows); it never invokes a shell.
- XML/CSV readers are not involved. Package data is parsed with bounded loops
  and field limits.
- Report data is untrusted input. The server validates sizes and characters; the
  C core also validates and rejects duplicate keys.
- The agent token is scoped only to reporting for one host. It cannot read the
  CMDB, create CIs, start scans, or change other hosts.
- A stolen agent token can only poison that host's own reported inventory. It
  cannot modify unrelated CIs or read server data.
- Agent reports are self-asserted. The server does not prove that a host has the
  software it claims. Protect host integrity and the agent configuration.
- The Windows config ACL and the Linux `0640 root:cmdb-agent` mode protect the
  token from ordinary users. Root/Administrator or malware on the host can read
  it, as with any local service credential.
- An agent token does not expire automatically; revoke it when decommissioning a
  host. Re-enrollment rotates the token.
- Application names and versions are displayed as text; the UI never interprets
  them as HTML.

## Troubleshooting

| Symptom | Check |
|---|---|
| `enroll` returns 401 | Token expired, already used, or revoked. Create a new token. |
| `enroll` returns 503 | The server has no administrator yet; complete first-run setup. |
| TLS certificate error | Install the internal CA in the OS trust store or pass `--ca-file`. |
| Service does not start on Linux | `/etc/cmdb-lite-agent/agent.json` must exist; check `systemctl status cmdb-agent`. |
| No applications on Fedora | `rpm -qa` must work for the `cmdb-agent` user and the RPM database must be healthy. |
| No applications on Windows | Only machine-wide uninstall entries are collected; per-user apps are out of scope. |
| Host status is Stale | No report for more than 48 hours. Check service status and network access. |
| History is missing | Retention defaults to 365 days / 90 reports; adjust the server flags. |

## Packages and artifacts

| Artifact | Contents |
|---|---|
| `cmdb-lite-agent_<version>_amd64.deb` | `/usr/bin/cmdb-agent`, systemd unit, docs; post-install user/group setup. |
| `cmdb-lite-agent-<version>-*.rpm` | Same layout for Fedora. |
| `cmdb-lite-agent-<version>-linux-x86_64.tar.gz` | Binary, unit, docs; no package scripts. |
| `cmdb-lite-agent-<version>-windows-x64.zip` | `cmdb-agent.exe` and docs; no service registration. |
| `cmdb-lite-agent-<version>-windows-x64.msi` | Per-machine installer with optional `SERVER_URL`/`ENROLLMENT_TOKEN` enrollment. |

Upgrading the agent package replaces the binary and unit. The configuration and
server-side history are unchanged. Uninstalling the Linux package disables and
stops the service; the configuration is removed on Debian purge and RPM erase.
