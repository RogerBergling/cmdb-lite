# Network discovery with nmap

> Status: implemented with asynchronous execution, persisted staging, matching,
> merge/create/ignore review, optional service and OS detection, and cancellation.

Scanning networks without authorisation may be illegal. Use this feature only
on networks you own or have explicit written permission to test.

## Prerequisites

`cmdb-lite` invokes an installed `nmap` executable. The application does not
bundle nmap on Linux `.deb`/RPM/TGZ or Windows; the Flatpak does.

| Platform | Install |
|---|---|
| Debian/Ubuntu | `sudo apt install nmap` |
| Fedora/RHEL | `sudo dnf install nmap` |
| Windows | Install nmap from <https://nmap.org/download.html> and ensure `nmap.exe` is on `PATH`. |
| Flatpak | Bundled; no host installation needed. |

Verify the installation:

```bash
nmap --version
```

If nmap is installed somewhere outside `PATH`, set `CMDB_NMAP` to the full
executable path before starting `cmdb`:

```bash
CMDB_NMAP=/opt/nmap/bin/nmap ./cmdb
```

On Windows:

```powershell
$env:CMDB_NMAP = "C:\Program Files (x86)\Nmap\nmap.exe"
.\cmdb.exe
```

The application checks executable availability before starting a scan and
returns a clear error if nmap is missing.

## Safety model

1. **Authorisation first.** The UI requires an explicit confirmation checkbox.
   The API rejects `scan.start` unless `authorized` is `true`. The confirmation
   and scan metadata are written to the audit log.
2. **No shell.** nmap is started with a fixed argv vector. The target and port
   specification are validated before execution, so they cannot become options
   or shell metacharacters.
3. **Small scope.** A target must be an IPv4 address or a CIDR from `/24` to
   `/32`. At most 256 addresses and 256 distinct ports are allowed.
4. **Unprivileged by default.** The scan uses `-sT` (TCP connect), not raw
   packets. It does not require root/administrator rights unless OS detection is
   explicitly enabled.
5. **No exploitation.** No NSE scripts, brute force, credential guessing,
   default-password attempts, or vulnerability checks are performed.
6. **Review before write.** Results are persisted in staging tables and matched
   to existing CIs. Nothing is inserted or merged until the operator chooses an
   action for each host.

## Exact command

The scanner runs the equivalent of:

```text
nmap -sT [-sV] [-O] -Pn -n --open \
  --max-rate 500 --max-retries 1 --host-timeout 30s \
  -p <validated-ports> -oX <private-temp-file> <validated-target>
```

- `-sT` uses an unprivileged TCP connect.
- `-sV` is optional service/version detection.
- `-O` is optional OS detection and requires root on Linux or an elevated
  process on Windows. The API rejects it before starting nmap when the process is
  not privileged. It is unavailable in the standard Flatpak sandbox.
- `-Pn` skips host discovery. A host is reported only when at least one selected
  port is open because `--open` is used.
- `-n` disables reverse DNS. nmap XML hostnames are still parsed if present.
- `--max-rate 500`, `--max-retries 1`, and `--host-timeout 30s` bound traffic and
  time.
- The overall process has a user-selected timeout (10–600 seconds, default 180).
  If it expires, the nmap process group is killed.
- XML is written to a private temporary file (`0600` on POSIX), parsed, and
  deleted. Parsed results are stored in the encrypted database staging tables.

## Asynchronous execution and cancellation

`scan.start` validates the request and returns a job ID immediately. The worker
runs nmap on a background thread; the UI polls `scan.status` once per second.
**Cancel scan** sets a cancellation flag; the worker terminates the nmap process
group and records the job as `cancelled`.

If the application exits while a scan is running, the next database open marks
the job `interrupted`. The staged hosts are retained for review. A new scan
cannot start while another scan is active.

## Persisted staging and review

Two schema tables hold discovery state:

```text
scan_jobs(id, target, ports, command, service_detection, os_detection,
          status, started_at, finished_at, host_count, truncated, error, ...)
discovered_assets(id, scan_job_id, ip_address, hostname, mac_address, vendor,
                  os_guess, services, open_ports, review_status, matched_ci_id, ...)
```

`review_status` is `pending`, `created`, `merged`, or `ignored`.

For each pending host the application computes exact-match suggestions against
the inventory:

1. `ip_address` or `management_ip` attribute equals the discovered IP.
2. `mac_address` attribute matches case-insensitively.
3. CI name equals the discovered hostname (case-insensitive).
4. `hostname` attribute equals the discovered hostname.

Matches are suggestions only. To avoid accidental duplicates, hosts with
existing matches start unselected with the action **Ignore**; unmatched hosts
start selected with **Create new CI**. The operator must explicitly choose and
select every merged or created record.

The operator chooses one action per host:

- **Create new CI** using the selected CI type.
- **Merge into an existing CI**, filling empty scan-derived fields only, unless
  **Overwrite scan fields when merging** is checked.
- **Ignore**.

Create and merge actions are audited. Failed actions stay pending and report the
validation error. Imported/merged attributes are:

| Attribute | Value |
|---|---|
| `ip_address` | IPv4 address |
| `hostname` | Hostname reported by nmap, if any |
| `mac_address` | Normalised lowercase colon-separated MAC, if any |
| `vendor` | MAC vendor, if any |
| `os_guess` | nmap OS match, if any |
| `services` | Semicolon-separated service results, for example `22/tcp=ssh OpenSSH 9.6` |
| `open_ports` | Comma-separated list such as `22/tcp,443/tcp` |
| `discovery_method` | `nmap` |
| `discovery_target` | The scanned CIDR or address |

The hostname is used as the CI name when available, otherwise the IP address.
There is no automatic merge.

## UI workflow

1. Open the encrypted database.
2. Select **Discovery**.
3. Enter a target such as `10.20.0.0/24` or `10.20.0.17`.
4. Adjust ports, timeout, and optional `-sV`/`-O`.
5. Confirm the authorisation checkbox.
6. Press **Run nmap scan**.
7. Use **Cancel scan** if needed.
8. Review the staged hosts, match suggestions, and choose an action per host.
9. Press **Apply selected actions**.
10. Use **Discard scan** to remove the staged job. Imported CIs are unaffected.

## API

### `scan.start`

```json
{
  "method": "scan.start",
  "params": {
    "target": "10.20.0.0/24",
    "ports": "22,80,443,8000-8100",
    "timeout_seconds": 180,
    "service_detection": true,
    "os_detection": false,
    "authorized": true
  }
}
```

Returns `job_id`, `status: "running"`, target, ports, and detection flags.

### `scan.status`

```json
{ "method": "scan.status", "params": { "job_id": "..." } }
```

Returns `status: "running"` while active, or a completed job with hosts and
matches. A failed/cancelled job includes the error. Jobs persisted by a previous
process are loaded from the database.

### `scan.cancel`

```json
{ "method": "scan.cancel", "params": { "job_id": "..." } }
```

Requests cancellation. Poll `scan.status` until it reports `cancelled`.

### `scan.jobs`, `scan.job`, `scan.discard`

- `scan.jobs` lists staged jobs with pending counts.
- `scan.job` returns one job and its assets/matches. Omit `job_id` for the latest.
- `scan.discard` deletes a staged job and its assets. It refuses a running job.

### `scan.import`

```json
{
  "method": "scan.import",
  "params": {
    "job_id": "...",
    "type_id": "11111111-1111-4111-8111-111111111111",
    "environment": "Production",
    "status": "Active",
    "criticality": "Medium",
    "decisions": [
      { "asset_id": "...", "action": "create" },
      { "asset_id": "...", "action": "merge", "ci_id": "...", "overwrite": false },
      { "asset_id": "...", "action": "ignore" }
    ]
  }
}
```

Returns `created`, `merged`, `ignored`, `failed`, and per-host result items.

## Validation and limits

- IPv4 only. A single address is normalised to `/32`.
- CIDR prefix must be `/24` or longer.
- Octets must be decimal `0`–`255`; leading zeros are rejected.
- Ports must be numeric, `1`–`65535`, with optional ascending ranges.
- At most 256 distinct ports and 256 discovered hosts.
- nmap XML is untrusted input:
  - maximum size 8 MiB;
  - XML depth limited to 64;
  - DOCTYPE internal subsets, external identifiers, and entity declarations are rejected; the standard `<!DOCTYPE nmaprun>` prolog is accepted;
  - host, port, and service output bounded;
  - only `nmaprun` output is accepted.

## Tests

`src/core/scan.c` parses XML independently of nmap, so fixtures do not require a
live scanner. The suite also covers:

- IPv4/CIDR and port validation;
- `-sV` service parsing and `-O` privilege checks;
- fake-nmap synchronous and asynchronous process runs;
- cancellation of a running process group;
- persisted staging, match suggestions, create/merge/ignore import;
- a live nmap scan against a localhost listener in Linux/Fedora CI
  (`CMDB_TEST_LIVE_NMAP=1`).

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `nmap is not installed or is not on PATH` | Install nmap or set `CMDB_NMAP`. |
| `OS detection (-O) requires root/administrator privileges` | Run elevated or disable `-O`. This is expected in the Flatpak sandbox. |
| `Scanning is limited to /24 or smaller networks` | Use `/24`–`/32`. |
| `At most 256 distinct ports can be scanned` | Reduce the port list or ranges. |
| `The nmap scan timed out` | Raise the timeout, reduce ports, or narrow the target. |
| `A scan is already running; cancel it first` | Cancel the active scan before starting another. |
| No hosts found | Ports are closed/filtered, the network blocks probes, or the host is down. Try one address and one known open port. |
| Flatpak scan fails | Confirm the Flatpak has `--share=network`; released bundles do. |
