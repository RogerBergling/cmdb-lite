# HIBP Linux host export

The **HIBP Linux hosts — CSV** export feeds Linux inventory from `cmdb-lite`
into [HIBP](https://github.com/RogerBergling/HIBP) without sharing databases,
passphrases, or credentials.

`cmdb-lite` remains the inventory source of truth. HIBP remains the privileged
patch controller and owns SSH access, host keys, patch jobs, and patch facts.

## Selecting hosts

A CI is included when either condition is true:

- its CI type name contains `Linux` (case-insensitive), for example
  `Linux Server` or `Linux VM`; or
- it has a `custom.os_family` attribute whose value is `Linux`
  (case-insensitive).

CIs without a usable host value in `ip_address`, `management_ip`, `hostname`,
`address`, or a hostname-like CI name are skipped. The export reports how many
hosts were written and how many were skipped.

## Columns

```text
name,address,port,username,auth_type,key_path,group,tags,
cmdb_ci_id,external_id,os_family,os_version,status
```

| Column | Source |
|---|---|
| `name` | CI name. |
| `address` | `ip_address`, then `management_ip`, then `hostname`, then `address`, then a hostname-like CI name. |
| `port` | `ssh_port`, then `port`, then the export default. |
| `username` | `ssh_username`, then `username`, then the export default. |
| `auth_type` | `ssh_auth_type`, then `auth_type`, then the export default (`agent`). |
| `key_path` | `ssh_key_path`, then `key_path`, then the export default. |
| `group` | Environment, falling back to support team or hosting location. |
| `tags` | `cmdb-lite`, environment, criticality, support team, and status. |
| `cmdb_ci_id` | The immutable `cmdb-lite` CI UUID. |
| `external_id` | The CI `external_id`, when set. |
| `os_family` | Always `Linux`. |
| `os_version` | `custom.os_version`, falling back to `custom.os`. |
| `status` | CI status, for downstream filtering. |

HIBP currently reads `name`, `address`, `port`, `username`, `auth_type`,
`key_path`, `group`, `tags`, and `cmdb_ci_id`. The remaining columns are
preserved in the file so a future HIBP importer can use them without changing
the `cmdb-lite` export.

## Using the export

1. Add or update the host once in `cmdb-lite`.
2. Choose **Export → HIBP Linux hosts — CSV**.
3. Set the default SSH username, port, authentication method, and key path.
   Per-CI `ssh_*` attributes override these defaults.
4. In HIBP, choose **Import CSV**, preview the rows, and commit.
5. Approve the host key on the first connection.

The export does not contain passwords, private keys, or any other secret. A
default key path is only a reference to a file on the HIBP server; it is not key
material.

## Current HIBP importer limitations

The HIBP importer is still being hardened for this feed:

- It stores `cmdb_ci_id` but currently matches duplicate hosts by `address` and
  `port`, not by `cmdb_ci_id`.
- It requires a non-empty SSH username, and key authentication requires a key
  path. The export supplies defaults so the rows are importable.
- Its `update` policy can overwrite existing SSH settings and host state with
  values from the CSV.

Until those are fixed, use `skip` for re-imports when hosts already exist in
HIBP, or verify the preview before choosing `update`. The intended end state is
idempotent matching by `cmdb_ci_id`, inventory-only updates that preserve SSH
configuration and patch facts, and a one-time "needs setup" state for imported
hosts that do not have credentials yet.
