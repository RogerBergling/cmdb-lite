# Data model

The database is a single SQLCipher file. All tables live in the same encrypted
file. Schema version 1 is created by `src/core/schema.c`.

## `app_meta`

Key/value application metadata stored inside the encrypted database.

| Key | Meaning |
|---|---|
| `schema_version` | Integer schema version; currently `1`. |
| `created_at` | Database creation timestamp (UTC). |
| `app_version` | Application version that created/updated the schema. |

The KDF salt and parameters are stored in the sidecar `.meta` file, not in this
table. They are not secret, but the database cannot be opened without them.

## `ci_types`

User-defined categories.

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT | UUID primary key. |
| `name` | TEXT | Unique, case-insensitive. |
| `description` | TEXT | Free text. |
| `icon` | TEXT | Optional icon key for the UI. |
| `color` | TEXT | Optional display colour. |
| `created_at`, `updated_at` | TEXT | ISO 8601 UTC. |

The application seeds editable examples: Linux Server, Windows Server, Virtual
Machine, Router, Switch, Firewall, Web Application, Database, Business Service,
Cloud Service, Network, and Other.

## `ci_type_fields`

Custom field definitions belonging to a CI type.

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT | UUID primary key. |
| `type_id` | TEXT | FK to `ci_types.id`, cascade delete. |
| `field_key` | TEXT | `[a-z][a-z0-9_]{0,63}`; unique per type, case-insensitive. |
| `label` | TEXT | Display label. |
| `field_type` | TEXT | `text`, `number`, `date`, `boolean`, `url`, or `select`. |
| `required` | INTEGER | `0` or `1`. |
| `options_json` | TEXT | JSON array of select options. |
| `sort_order` | INTEGER | UI ordering. |

## `cis`

Configuration items.

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT | UUID primary key. |
| `name` | TEXT | Required, maximum 255 bytes. Not unique; name resolution requires uniqueness. |
| `type_id` | TEXT | FK to `ci_types.id`, restrict delete while in use. |
| `description` | TEXT | Maximum 4,095 bytes. |
| `environment` | TEXT | Free text. |
| `status` | TEXT | Free text. |
| `business_service` | TEXT | Free text. |
| `technical_owner` | TEXT | Free text. |
| `support_team` | TEXT | Free text. |
| `criticality` | TEXT | Free text. |
| `hosting_location` | TEXT | Free text. |
| `external_id` | TEXT | Optional; unique when non-empty. Used for idempotent imports. |
| `created_at`, `updated_at` | TEXT | ISO 8601 UTC. |

Indexes: case-insensitive name, type, and a partial unique index on
`external_id`.

`Dependencies` is not a column. It is derived from `relationships`.

## `ci_attributes`

Custom attribute values.

| Column | Type | Notes |
|---|---|---|
| `ci_id` | TEXT | FK to `cis.id`, cascade delete. |
| `field_key` | TEXT | Case-insensitive key. |
| `value` | TEXT | Maximum 4,095 bytes. |

Primary key: `(ci_id, field_key)`. Attribute keys do not have to be declared in
`ci_type_fields`; this allows imports to preserve extra data and lets the UI
surface it under **Additional attributes**.

## `relationship_types`

Typed edges.

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT | UUID primary key. |
| `name` | TEXT | Unique, case-insensitive. |
| `inverse_name` | TEXT | Display name for the inverse direction. |
| `description` | TEXT | Human-readable meaning. |
| `color` | TEXT | Optional display colour. |
| `created_at`, `updated_at` | TEXT | ISO 8601 UTC. |

Seeded types: `runs_on`, `uses`, `hosted_in`, `supports`, `depends_on`,
`connects_to`, `member_of`.

## `relationships`

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT | UUID primary key. |
| `source_id` | TEXT | FK to `cis.id`, cascade delete. |
| `target_id` | TEXT | FK to `cis.id`, cascade delete. |
| `type_id` | TEXT | FK to `relationship_types.id`, restrict delete. |
| `description` | TEXT | Maximum 1,023 bytes. |
| `created_at`, `updated_at` | TEXT | ISO 8601 UTC. |

Constraints:

- `UNIQUE(source_id, target_id, type_id)`.
- `CHECK(source_id <> target_id)`.

Indexes on source and target for graph traversal.

## `diagrams` (reserved)

The embedded draw.io editor will store diagrams here:

| Column | Type | Notes |
|---|---|---|
| `id` | TEXT | UUID primary key. |
| `name` | TEXT | Diagram name. |
| `drawio_xml` | TEXT | `.drawio` XML. |
| `created_at`, `updated_at` | TEXT | ISO 8601 UTC. |

The vertical slice exports `.drawio` files but does not yet persist diagrams.

## `audit_log`

| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER | Auto-increment primary key. |
| `ts` | TEXT | ISO 8601 UTC. |
| `actor` | TEXT | `local` in the single-user version. |
| `action` | TEXT | `create`, `update`, `delete`, `import`, `export`, ... |
| `entity_type` | TEXT | `ci`, `ci_type`, `ci_type_field`, `relationship`, ... |
| `entity_id` | TEXT | Entity UUID or empty. |
| `detail` | TEXT | JSON or compact detail. |
| `prev_hash` | TEXT | Previous row hash, or 64 zeroes. |
| `hash` | TEXT | BLAKE2b-256 of the chained row content. |

The chain is verified by `cmdb_audit_verify`. See
[`security.md`](security.md).

## Entity relationships

```text
ci_types 1 ─── n ci_type_fields
ci_types 1 ─── n cis
cis     1 ─── n ci_attributes
cis     n ─── n cis            (through relationships)
cis     1 ─── n diagrams       (future)
```

## Future multi-user path

The schema is deliberately server-friendly:

- UUID primary keys rather than auto-increment IDs for domain objects.
- An audit log with actor and entity fields.
- A clear API boundary (`src/core/api.c`) between the UI and persistence.
- SQLite can be replaced by a server database behind the same repository/API
  layer, or the existing database can be served by a small local server with
  authentication and RBAC.

Multi-user access to a shared encrypted SQLite file is **not** supported. SQLite's
locking is not a substitute for a server, and sharing a passphrase across users
destroys individual accountability.
