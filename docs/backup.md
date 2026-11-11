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

Server-mode accounts, TOTP secrets, and recovery-code hashes live in the same
encrypted database under the `app_meta` key `auth.state`. A database backup
therefore includes authentication state, and restore tests must include login,
TOTP, and recovery-code validation. Sessions are memory-only and are not part of
a backup; restarting the server logs everyone out.

Opening a server-managed database in local desktop mode bypasses application
logins. The database passphrase is the only gate in that mode.

## Manual backup while the application is closed

If the application is locked or closed, copying the files is also safe:

```bash
cp inventory.cmdb inventory.cmdb.meta /path/to/backup/
```

Do not copy a live database while it is open if you are not using the in-app
backup. Use **Backup** instead.

## Server mode

`cmdb-server` does not expose a backup API in this release, and the server-mode
UI hides the desktop **Backup** action. To take a consistent backup:

1. Stop the server process.
2. Copy the `.cmdb` and matching `.meta` sidecar together.
3. Restart the server and confirm that accounts and inventory are present.

Stopping the server also ends all sessions. Do not copy the file while the
server is running. Keep the state directory `0700` and run the service with
`UMask=0077` so newly created WAL/SHM files are not group/world-readable. A
future release should add an authenticated server-side backup action with
destination and retention controls.

## Restore

### In-app restore to a new path

1. On the unlock screen, choose **Restore a backup to a new database…**.
2. Select the backup `.cmdb` file. Its `.meta` sidecar must be next to it.
3. Choose a **new** destination path that does not exist.
4. Confirm the restore.
5. Enter the passphrase that was active when the backup was created.

The in-app restore copies the database and creates the matching `.meta` sidecar.
It never overwrites an existing database. To replace a database, restore to a
new path first, verify it, and then switch to it.

The **Audit** tab can verify the restored chain.

### Manual restore

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

## Backup policy for the production rollout

Desktop-local mode provides **manual in-app backup** and **automatic
pre-migration backup**. Server mode has no online backup API, so a consistent
backup requires stopping the service and copying the database pair. Neither
mode provides configured automatic or scheduled backups yet.

The planned go-live requirement is configurable backup policy:

- manual backup remains available;
- automatic backup on a configurable interval;
- optional backup on application lock/exit;
- configurable destination directory;
- retention count or age;
- last-run status and failure visibility;
- restore verification instructions.

Until that is implemented, use the manual **Backup** action and an external
scheduler or file-copy procedure. Do not describe the current release as having
scheduled backups.

## What is not included

- Passphrase recovery. A lost passphrase cannot be recovered.
- Passphrase rotation. It is a future milestone; changing the passphrase
  requires a migration tool.
- Configurable automatic/scheduled backups. The current backup is manual, plus
  the automatic pre-migration backup.
- A plaintext export of the database. Exports such as `.drawio` are intentionally
  plaintext files chosen by the user.

## Verification

- A backup is not a substitute for testing a restore.
- Keep at least one backup off the machine.
- Backups remain encrypted; protect the passphrase separately.
- Store backups on media that is not world-writable.
