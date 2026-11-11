# Security policy

## Supported versions

Security fixes are applied to the latest release. Older releases may not receive
fixes; use the latest release and watch the repository for security notices.

## Reporting a vulnerability

Please report suspected vulnerabilities privately. Do not open a public issue
containing a working exploit, credentials, database contents, or other sensitive
data.

Preferred channel: **GitHub private vulnerability reporting** on this repository.

Alternative: **github@bergling.net**

Include, where possible:

- affected version or commit;
- affected platform and installation type;
- impact and attack prerequisites;
- reproduction steps or a proof of concept;
- any suggested mitigation.

No bug bounty is offered. Reports are reviewed as capacity permits; there is no
formal response SLA yet. Please allow time for a fix and coordinate disclosure
before publishing details.

## Scope

This policy covers the `cmdb-lite` desktop application, its core, build scripts,
packaging, and documentation.

The separate HIBP patch-management server has its own security policy and
threat model. Do not share cmdb-lite databases, passphrases, or SSH credentials
with HIBP.

## Security design

See [`docs/security.md`](docs/security.md) for the threat model, encryption
design, limitations, and operator hardening guidance.
