# Backups and restore

`cmdb-lite` stores everything in one encrypted `.cmdb` database plus a
`<database>.meta` sidecar. A usable backup is always the **pair**. The `.meta`
file contains the Argon2id salt and KDF parameters; without it the database
cannot be opened.

## In-app backup

1. Unlock the database.
2. Press **Backup** in the top bar.
3. Choose a new path ending in `.cmdb`.
4. The application writes the database and `<backup>.meta` sidecar.

The backup uses SQLite's online backup API, so it is consistent while the
database is open and includes committed WAL content. The copy remains encrypted
with the same master passphrase. The application refuses to overwrite an
existing `.cmdb` or `.meta` file.

The derived SQLCipher key is kept in process memory while the database is open
so backup does not ask for the passphrase again. It is zeroized on close and is
never written to disk.

## Manual backup while the application is closed

If the application is locked or closed, copying the files is also safe:

```bash
cp inventory.cmdb inventory.cmdb.meta /path/to/backup/
```

Do not copy a live database while it is open if you are not using the in-app
backup. Use **Backup** instead.

## Restore

1. Close `cmdb-lite`.
2. Keep the current database and `.meta` somewhere safe.
3. Copy the backup pair back to the path you want to use:

   ```bash
   cp /path/to/backup/inventory.cmdb .
   cp /path/to/backup/inventory.cmdb.meta .
   ```

4. Start `cmdb-lite`, choose **Open database**, and enter the passphrase.
5. Run **Audit verify** and inspect a few CIs.

If the backup was created with a different passphrase, the current passphrase
will not open it. Use the passphrase that was active when the backup was taken.

## What is not included

- Passphrase recovery. A lost passphrase cannot be recovered.
- Passphrase rotation. It is a future milestone; changing the passphrase
  requires a migration tool.
- Automatic scheduled backups. The backup is manual.
- A plaintext export of the database. Exports such as `.drawio` are intentionally
  plaintext files chosen by the user.

## Verification

- A backup is not a substitute for testing a restore.
- Keep at least one backup off the machine.
- Backups remain encrypted; protect the passphrase separately.
- Store backups on media that is not world-writable.
