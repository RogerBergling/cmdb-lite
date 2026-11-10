# Audit log

The **Audit** tab shows the append-only, hash-chained audit log stored in the
encrypted database.

Each row contains:

- timestamp (UTC);
- action (`create`, `update`, `delete`, `import`, `export`, `scan`, `backup`,
  ...);
- entity type and entity ID;
- JSON or compact detail;
- previous hash and current hash.

## Verification

**Verify chain** walks the entire log and recomputes each BLAKE2b-256 hash. It
reports the entry count and whether every `prev_hash`/`hash` link is valid.

The chain is tamper-evident against accidental edits, not against a local
attacker who knows the database key. For compliance-grade evidence, periodically
export or forward audit events to an external append-only log.

## Paging

The viewer loads 50 entries at a time, newest first. **Previous** and **Next**
move through the log. The total count is shown under the table.
