# Import format

`cmdb-lite` accepts UTF-8 CSV and tab-separated text files. The import screen
always validates the complete file and shows a preview before anything is
written. A committed import runs in one database transaction: either every row
is applied or the import is rolled back.

## Encoding and delimiters

| Item | Rule |
|---|---|
| Character encoding | UTF-8. A UTF-8 BOM is accepted and ignored. |
| CSV delimiter | Comma (`,`). |
| Text/TSV delimiter | Tab. Files ending in `.tsv` or `.txt` are treated as tab-separated. |
| Header row | Required. The first non-empty row is the header. |
| Quoting | RFC 4180: wrap a field in double quotes to include the delimiter, a newline, or a quote. Double a quote inside a quoted field. |
| Line endings | LF or CRLF. |
| Empty rows | Ignored. |
| Maximum size | 20 MiB, 10,000 data rows, 128 columns, 1 MiB per raw field. |
| Dates | ISO 8601 UTC, for example `2026-11-09T10:00:00Z`. |
| Empty cells | Stored as empty values. Do not use the literal string `NULL`. |
| Header matching | Case-insensitive. Spaces and hyphens in headers are converted to underscores, so `CI Name` becomes `ci_name`. |

Malformed quoting, a NUL byte, invalid UTF-8, duplicate headers, extra columns,
or an unterminated quoted field stops the import with a line-numbered error.

## CI inventory files

### Required columns

```text
ci_name,ci_type
```

### Optional standard columns

```text
ci_id
description
environment
status
business_service
technical_owner
support_team
criticality
hosting_location
last_updated
external_id
custom.<key>
```

### Example `cis.csv`

```csv
ci_name,ci_type,description,environment,status,business_service,technical_owner,support_team,criticality,hosting_location,external_id,custom.ip_address,custom.serial_number,custom.os_version
app01,Linux Server,Primary payments application server,Production,Active,Payments,Alice Example,Linux Platform Team,High,dc1-rack42,SRV-001,10.20.0.11,SN-8891,Ubuntu 26.04
db01,Database,Payments primary database,Production,Active,Payments,Alice Example,Database Team,Critical,dc1-rack43,DB-001,10.20.0.21,SN-2210,PostgreSQL 18
payments-api,Web Application,Payments REST API,Production,Active,Payments,Bob Example,Payments Team,High,cloud-eu-west-1,APP-001,,,v4.12.0
```

### Field rules

- `ci_name` is required and limited to 255 UTF-8 bytes. Control characters are
  not allowed.
- `ci_type` is required. It must match an existing CI type case-insensitively,
  unless **Create missing CI types** is enabled. Imported types are created with
  a description of `Imported CI type`.
- `ci_id` is optional. If present, it must be a UUID. It is used to update an
  existing record or to create a record with a caller-supplied ID.
- `external_id` is optional, limited to 255 bytes, and unique when non-empty.
  It is the recommended stable key for repeated imports from another system.
- `last_updated` is optional and validated as ISO 8601 UTC. The application's
  own `updated_at` timestamp remains authoritative.
- `description` allows newlines and tabs.
- Environment, status, business service, owner, support team, criticality, and
  hosting/location are free text. Suggested values are shown in the UI; the
  importer does not enforce a taxonomy.
- The `dependencies` field is **derived** from relationships and is not imported
  as a free-text column. Use a relationship file instead.

### Custom fields

A custom column is any header starting with `custom.`:

```csv
ci_name,ci_type,custom.ip_address,custom.serial_number
app01,Linux Server,10.20.0.11,SN-8891
```

- The key after `custom.` must match `[a-z][a-z0-9_]{0,63}`.
- Custom keys are matched case-insensitively; headers are lowercased.
- Values are limited to 4,095 UTF-8 bytes.
- A custom attribute may be imported even when the CI type does not declare it.
  This keeps imports resilient to schema drift. The UI shows undeclared
  attributes in the **Additional attributes** section.
- If a CI type declares a field as **required**, an empty or missing `custom.*`
  value is a validation error.

## Relationship files

### Required columns

```text
source_ci,relationship_type,target_ci
```

Optional columns:

- `description` (maximum 1,023 bytes).
- `impact` — `availability`, `degraded`, `management`, `monitoring`, or
  `informational`. When omitted or empty, the relationship type's default is
  used.

### Example `relationships.csv`

```csv
source_ci,relationship_type,target_ci,description,impact
payments-api,runs_on,app01,Payments API runs on app01,availability
payments-api,uses,db01,Payments API uses the primary database,availability
payments-api,depends_on,payments-service,Payments API depends on the business service,availability
vcenter01,manages,app01,Inventory and power operations,management
app01,depends_on,net01,App server depends on the network segment,availability
```

### Resolving source and target

`source_ci` and `target_ci` accept, in order:

1. An exact CI ID (UUID).
2. An exact external ID.
3. An exact CI name, but only when exactly one CI has that name.

If a name is ambiguous, the row is rejected. Use a CI ID or external ID for
unambiguous imports.

### Relationship types

Relationship types are matched case-insensitively:

- `runs_on` / `hosts`
- `uses` / `used_by`
- `hosted_in` / `hosts`
- `supports` / `supported_by`
- `depends_on` / `depended_on_by`
- `connects_to` / `connects_to`
- `member_of` / `contains`
- `manages` / `managed_by`
- `monitors` / `monitored_by`
- `provided_by` / `provides`

The inverse names are shown in the UI; the stored edge is always directed from
source to target. A CI cannot relate to itself. Duplicate
`(source, relationship_type, target)` triples are handled by the selected
duplicate policy. With the `update` policy, the relationship's description and
impact are updated; the source, target, and type identify the existing row.

## Duplicate policy

The import screen offers three policies:

| Policy | Behaviour |
|---|---|
| `error` | Reject the whole import if an existing record is matched. Safest default. |
| `skip` | Leave existing records unchanged; create only new records. |
| `update` | Replace the existing record's standard fields and custom attributes with the imported row. |

Matching order for CIs:

1. `ci_id`, when supplied.
2. `external_id`, when supplied.
3. Otherwise the row is treated as a new record. **Names are never used to
   silently merge CIs.**

If both `ci_id` and `external_id` are supplied and resolve to different existing
records, the row is rejected.

Relationship matching uses the resolved source ID, target ID, and relationship
type ID.

## Validation report

The preview and commit responses contain:

- `rows`: number of data rows processed.
- `created`, `updated`, `skipped`: planned or applied actions.
- `error_count`: total validation errors.
- `errors`: up to 100 errors, each with `line`, `column`, and `message`.
- `errors_truncated`: true when more than 100 errors exist.
- `type_create`: number of missing CI types that were (or would be) created.
- `committed`: true only for a successful commit.

A commit is refused when any validation error exists. The UI keeps the **Commit
import** button disabled until a preview succeeds.

## CSV injection

Cells beginning with `=`, `+`, `-`, or `@` can be interpreted as formulas by
spreadsheet software. `cmdb-lite` treats all imported cells as literal data and
never executes them. CSV exports prefix such cells with a single quote (`'`) so
spreadsheet applications do not evaluate them. The prefix is part of the
exported text; use the JSON export for a byte-exact round trip.

## Network discovery results

Network discovery is a separate workflow in the **Discovery** tab; it is not a
CSV/TSV import format. nmap XML is parsed into staged hosts and the operator
selects which hosts to import. See [`scanning.md`](scanning.md).

Each discovered host is reviewed and created as a new CI with the selected CI
type. There is no automatic merge into existing CIs. Scan data is evidence, not
an automatic source of truth.
