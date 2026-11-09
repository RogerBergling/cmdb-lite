# Network discovery with nmap

> Status: implemented. The **Discovery** tab runs an authorised, unprivileged
> nmap TCP connect scan, stages the results in the UI, and imports selected
> hosts as CIs.

Scanning networks without authorisation may be illegal. Use this feature only
on networks you own or have explicit written permission to test.

## Prerequisites

`cmdb-lite` invokes an installed `nmap` executable. The application does not
bundle nmap.

| Platform | Install |
|---|---|
| Debian/Ubuntu | `sudo apt install nmap` |
| Fedora/RHEL | `sudo dnf install nmap` |
| Windows | Install nmap from <https://nmap.org/download.html> and ensure `nmap.exe` is on `PATH`. |

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
   The API rejects `scan.nmap` unless `authorized` is `true`. The confirmation
   and scan metadata are written to the audit log.
2. **No shell.** nmap is started with a fixed argv vector. The target and port
   specification are validated before execution, so they cannot become options
   or shell metacharacters.
3. **Small scope.** A target must be an IPv4 address or a CIDR from `/24` to
   `/32`. At most 256 addresses and 256 distinct ports are allowed.
4. **Unprivileged.** The scan uses `-sT` (TCP connect), not raw packets. It does
   not require root/administrator rights.
5. **No exploitation.** No NSE scripts, brute force, credential guessing,
   default-password attempts, or vulnerability checks are performed.
6. **Review before write.** Results are staged in the Discovery tab. Nothing is
   inserted into the inventory until the operator selects hosts and presses
   **Import selected**.

## Exact command

The scanner runs the equivalent of:

```text
nmap -sT -Pn -n --open \
  --max-rate 500 --max-retries 1 --host-timeout 30s \
  -p <validated-ports> -oX <private-temp-file> <validated-target>
```

Notes:

- `-sT` uses an unprivileged TCP connect.
- `-Pn` skips host discovery. A host is reported only when at least one selected
  port is open because `--open` is used.
- `-n` disables reverse DNS. nmap XML hostnames are still parsed if present.
- `--max-rate 500` and `--max-retries 1` bound the scan's traffic.
- `--host-timeout 30s` bounds time spent per host.
- The overall process has a user-selected timeout (10–600 seconds, default 180).
  If it expires, nmap is killed.
- XML is written to a private temporary file (`0600` on POSIX), parsed, and
  deleted. Scan results themselves stay in the application UI until imported.

## UI workflow

1. Open the encrypted database.
2. Select **Discovery**.
3. Enter a target such as `10.20.0.0/24` or `10.20.0.17`.
4. Adjust the port list. The default is `22,80,443,445,3389,8080`.
5. Confirm the authorisation checkbox.
6. Press **Run nmap scan**.
7. Review the staged hosts: IP address, hostname, MAC address, open ports, and
   OS guess. Use the checkboxes to exclude hosts you do not want.
8. Choose the CI type for imported records.
9. Press **Import selected**.

Imported CIs use the hostname when available, otherwise the IP address, as the
CI name. The import adds these custom attributes:

| Attribute | Value |
|---|---|
| `ip_address` | IPv4 address |
| `hostname` | Hostname reported by nmap, if any |
| `mac_address` | Normalised lowercase colon-separated MAC, if any |
| `vendor` | MAC vendor, if any |
| `os_guess` | nmap OS match, if any |
| `open_ports` | Comma-separated list such as `22/tcp,443/tcp` |
| `discovery_method` | `nmap` |
| `discovery_target` | The scanned CIDR or address |

The import does not auto-merge with existing CIs. Duplicate names or validation
failures are reported per host; successfully imported hosts are removed from
the staged list. Review failures, adjust the names or the selected CI type, and
retry the remaining hosts. To avoid duplicate records, check the inventory
before importing.

## API

The bridge exposes two methods. Both require an open database.

### `scan.nmap`

```json
{
  "method": "scan.nmap",
  "params": {
    "target": "10.20.0.0/24",
    "ports": "22,80,443,8000-8100",
    "timeout_seconds": 180,
    "authorized": true
  }
}
```

The result contains `target`, `method`, `command`, `started_at`, `finished_at`,
`host_count`, `truncated`, and a `hosts` array. Each host contains `ip_address`,
`hostname`, `mac_address`, `vendor`, `os_guess`, `status`, `open_ports` (array),
`port_count`, and `ports_truncated`.

### `scan.import`

```json
{
  "method": "scan.import",
  "params": {
    "target": "10.20.0.0/24",
    "type_id": "11111111-1111-4111-8111-111111111111",
    "name_prefix": "",
    "environment": "Production",
    "status": "Active",
    "criticality": "Medium",
    "hosting_location": "DC Amsterdam",
    "hosts": [
      {
        "ip_address": "10.20.0.11",
        "hostname": "app01",
        "mac_address": "aa:bb:cc:dd:ee:01",
        "vendor": "Dell Inc.",
        "os_guess": "Linux 5.15",
        "open_ports": ["22/tcp", "443/tcp"]
      }
    ]
  }
}
```

The result contains `created`, `failed`, and an `items` array with per-host
`ok`, `id`, and `error` fields. Each created CI is written with its own audit
entry, and the import itself is also audited.

## Validation and limits

- IPv4 only. A single address is normalised to `/32`.
- CIDR prefix must be `/24` or longer. `10.0.0.0/16` is rejected.
- Octets must be decimal `0`–`255`; leading zeros are rejected.
- Ports must be numeric, `1`–`65535`, with optional ascending ranges.
- At most 256 distinct ports are allowed.
- At most 256 discovered hosts are returned.
- nmap XML is untrusted input:
  - maximum size 8 MiB;
  - XML depth is limited to 64;
  - DTDs and entity declarations are rejected;
  - only known host/address/hostname/port/OS elements are consumed;
  - host and per-host port counts are bounded.

## XML parser and tests

`src/core/scan.c` uses libexpat. The parser is independent of nmap, so tests use
fixtures and do not require a live scanner. The test suite also creates a fake
`nmap` executable on POSIX to exercise the process runner, temporary file, and
XML parsing end to end.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `nmap is not installed or is not on PATH` | Install nmap or set `CMDB_NMAP` to the full path. |
| `Scanning is limited to /24 or smaller networks` | Use a `/24`, `/25`, `/26`, `/27`, `/28`, `/29`, `/30`, `/31`, or `/32` target. |
| `At most 256 distinct ports can be scanned` | Reduce the port list or ranges. |
| `The nmap scan timed out` | Raise the process timeout, reduce the port range, or use a smaller target. |
| No hosts found | The selected ports are closed/filtered, the network blocks probes, or the host is down. Try a single address and a known open port. |
| Permission errors on Windows | Some nmap modes require Npcap or elevation. This scanner uses TCP connect and does not elevate; install/repair Npcap only if you intentionally use another nmap mode outside the application. |
