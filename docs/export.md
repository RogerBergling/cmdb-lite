# Exporting data

The **Export** button on the Inventory screen writes a plaintext file outside the
encrypted database.

## Formats

| Format | Contents |
|---|---|
| CI inventory — CSV | All CIs, standard fields, and a `custom.<key>` column for every attribute key in the database. |
| Relationships — CSV | `source_ci,relationship_type,target_ci,description`. |
| Full snapshot — JSON | CIs with attributes, plus relationships and timestamps. |

CSV exports are import-compatible. The CI export includes `ci_id` and
`ci_type`; re-importing with the `update` duplicate policy can update existing
records. The relationship export uses CI names, matching the relationship import
format.

## Security

Exports are plaintext snapshots. Treat them like a printed CMDB: store them in a
protected location and delete them when no longer needed. The application audits
each export with the format and path.

## Limits

The export walks the database in pages of 200 CIs/relationships. It is intended
for reporting and transfer, not for streaming very large datasets. For estates
beyond tens of thousands of CIs, add a database-side export writer with a
configured batch size.
