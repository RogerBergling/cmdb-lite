# Network discovery design (next milestone)

> Status: design only. The vertical slice does not initiate network scans.

## Principles

1. **Authorisation first.** The operator must confirm they own or are permitted
   to scan the target network. The confirmation is recorded in the audit log.
2. **Unprivileged by default.** Use TCP connect discovery and readable OS ARP
   tables. Avoid raw sockets and administrative privileges.
3. **No exploitation.** No brute force, credential guessing, NSE scripts,
   vulnerability exploitation, or default-password attempts.
4. **Review before merge.** Scan results enter a staging queue. A human maps
   each host to an existing CI or creates a new one.
5. **Small, explicit scope.** A single CIDR, a maximum host count, bounded
   concurrency, and timeouts.

Scanning networks without authorisation may be illegal. This tool is not a
penetration-testing or attack platform.

## Scan profiles

### Default: unprivileged discovery

- TCP connect probes to a small set of management/common ports.
- ARP table inspection:
  - Linux: `/proc/net/arp` or netlink.
  - Windows: `GetIpNetTable2`.
- Optional ICMP only if the OS permits it without elevation.
- Optional reverse DNS for discovered addresses.
- No OS fingerprinting beyond a conservative TTL/port heuristic.

### Optional: nmap

If nmap is installed and the operator enables it:

```text
nmap -oX - -n --max-rate <rate> --host-timeout <timeout> <target>
```

Additional flags for service detection are explicit and bounded. The
application must:

- execute nmap with a fixed argv vector, never through a shell;
- validate the target as an IP/CIDR, not a hostname or option;
- reject targets that begin with `-`;
- parse XML with libexpat using a bounded parser;
- display the exact command and scope before execution;
- record the command, scope, start/end time, and result count in the audit log.

Windows may require Npcap for some nmap modes. The application should detect
and explain this rather than silently elevating.

## Staging model

A future schema will add:

```text
scan_jobs(
  id, started_at, finished_at, target_cidr, method,
  operator, status, summary_json
)

discovered_assets(
  id, scan_job_id, ip_address, hostname, mac_address,
  os_guess, open_ports, first_seen, last_seen,
  review_status, matched_ci_id, raw_json
)
```

`review_status`: `pending`, `merged`, `created`, `ignored`.

Nothing in `discovered_assets` is visible in the main inventory until the
operator approves it. Matching suggestions use IP, MAC, hostname, and external
IDs, but never auto-merge.

## Import format

The review queue can also accept an external scan CSV:

```csv
ip_address,hostname,mac_address,os_guess,open_ports,first_seen,last_seen,source
10.20.0.11,app01,AA:BB:CC:DD:EE:01,Linux,22/tcp;443/tcp,2026-11-09T09:00:00Z,2026-11-09T09:05:00Z,arp+tcp
```

Rules:

- `ip_address` is required and validated as IPv4/IPv6.
- `mac_address` is optional and normalised to colon-separated lowercase.
- `open_ports` is a semicolon-separated list of `port/proto`.
- Timestamps are ISO 8601 UTC.
- The `source` value documents how the host was discovered.
- Duplicate `(ip_address, mac_address, source)` within one job is collapsed.

## Rate limits and safety

- Maximum default CIDR: `/24` (254 usable IPv4 addresses).
- Maximum hosts per job: configurable, hard-capped.
- Default per-host timeout: 1 second.
- Default concurrency: 32 TCP connects, not a flood.
- No UDP sweeps, no SNMP brute force, no credentials.
- The UI displays a clear warning and requires an explicit checkbox.

## Matching to the CMDB

For each discovered asset:

1. Exact MAC match to an existing CI's `mac_address` attribute.
2. Exact IP match to `ip_address` or `management_ip`.
3. Exact hostname match to CI name or `hostname` attribute.
4. Fuzzy suggestions are shown but require confirmation.

The operator can:

- merge into an existing CI (only empty fields are filled by default, with an
  explicit overwrite option),
- create a new CI using a user-selected type,
- ignore the asset,
- postpone it.

All actions are audited.

## Security considerations

- A scan can reveal sensitive network topology. Scan results are stored in the
  encrypted database.
- Scan output files are plaintext until imported; delete them after import.
- nmap XML is untrusted input. Parse with a bounded parser and fuzz it.
- Do not put credentials or SNMP community strings in CI attributes. Use a
  secret manager reference.
- A compromised local user can run the scanner; authorisation is recorded but
  cannot be technically enforced.
