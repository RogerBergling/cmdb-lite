# Upgrading cmdb-lite

Your inventory lives in the encrypted `.cmdb` database you chose. Upgrading the
application must never replace, move, or delete that file. This document
describes the safe procedure for each platform and what to do if something goes
wrong.

## Before you upgrade

1. **Close the application** or use **Lock**. The database must not be open in
   another process.
2. **Back up the database and its sidecar together:**

   ```bash
   cp inventory.cmdb inventory.cmdb.meta /path/to/backup/
   ```

   On Windows:

   ```powershell
   Copy-Item inventory.cmdb, inventory.cmdb.meta D:\backup\
   ```

   The `.meta` file contains the Argon2id salt and KDF parameters. A database
   without its matching `.meta` file cannot be opened. Back up both files in the
   same operation. If the database uses SQLite WAL files (`-wal`, `-shm`), make
   the copy while the application is locked; a stopped application checkpoints
   the WAL.
3. **Verify the download** against the release's `SHA256SUMS`:

   ```bash
   sha256sum -c SHA256SUMS
   ```

   On Windows with PowerShell:

   ```powershell
   Get-FileHash .\cmdb-lite-windows-x64.zip -Algorithm SHA256
   ```

4. Read the release notes. If a release changes the database schema, the notes
   will say so explicitly. Schema migrations are applied when the database is
   opened.

## Database compatibility

The scanner release does not change the database schema. Existing v0.1.0
databases open directly; no migration step is required. Scan jobs are not stored
as separate tables — reviewed results become ordinary CIs and audit entries.

For future releases:

- Schema changes are forward migrations. A newer application can open an older
  database and upgrades it in place.
- Backward compatibility is not guaranteed. After a newer application has
  migrated a database, do not open the same file with an older application.
  Restore the pre-upgrade backup instead.
- The database remains encrypted throughout the upgrade. Migration does not
  create a plaintext copy.

## Debian / Ubuntu (`.deb`)

```bash
# Stop any running instance, then install the new package over the old one.
sudo apt install ./cmdb-lite_0.2.0_amd64.deb
```

`apt` replaces `/usr/bin/cmdb` and leaves your database untouched. Your database
is not in `/usr`; it is the file you selected when you created it.

Verify:

```bash
cmdb --version 2>/dev/null || true
```

The desktop application does not currently print a version to the terminal, so
check the package version instead:

```bash
dpkg -s cmdb-lite | grep '^Version:'
```

## Fedora / RHEL (`.rpm`)

```bash
sudo dnf upgrade ./cmdb-lite-0.2.0-1.x86_64.rpm
```

or, if the RPM is in a repository:

```bash
sudo dnf upgrade cmdb-lite
```

The RPM replaces the executable, desktop entry, icon, and documentation. It does
not touch your database. Verify with:

```bash
rpm -q cmdb-lite
```

## Portable Linux tarball

The portable archive contains a single `cmdb` ELF. Keep the old binary until the
new one starts successfully:

```bash
tar -xzf cmdb-lite-0.2.0-linux-x86_64.tar.gz
./cmdb-lite-0.2.0-linux-x86_64/bin/cmdb
```

The database is opened from the path you choose in the application. File paths
are absolute, so moving the executable does not move the database.

## Windows ZIP (portable)

1. Unzip the new `cmdb-lite-windows-x64.zip` to a new folder.
2. Run `cmdb-lite-windows-x64\bin\cmdb.exe`.
3. Open the same `.cmdb` file.

You can keep the old and new folders side by side during validation. The
executable does not store the database next to itself.

## Windows MSI

```powershell
msiexec /i .\cmdb-lite-0.2.0-windows-x64.msi
```

The per-machine MSI upgrades the installed executable and Start Menu shortcut.
It does not include or remove your database. Administrator elevation is
required.

To uninstall:

```powershell
msiexec /x .\cmdb-lite-0.2.0-windows-x64.msi
```

Uninstalling removes the application only. Back up your database first if you
also intend to remove the folder that contains it.

## Rolling back

If the new version fails to start or cannot open the database:

1. Do not delete the upgraded database.
2. Keep the error message and the application version.
3. Restore the pre-upgrade `.cmdb` and `.meta` pair from your backup.
4. Reinstall the previous application version from GitHub Releases.

```bash
cp /path/to/backup/inventory.cmdb /path/to/backup/inventory.cmdb.meta .
```

If the database was opened by a newer schema version, the older application may
refuse it. Restoring both files from the backup is the reliable rollback.

## Verify after upgrading

1. Open the database with the master passphrase.
2. Check the inventory count and a few recent CIs.
3. Open **Relationships** and **Dependency Map**.
4. For a scanner release, choose a small target such as a single test host and
   run a scan with **Discovery**. Confirm that nmap is available.
5. Run **Audit verify** to confirm the audit hash chain is intact.

If any step fails, stop using the upgraded database, preserve a copy, and
restore the backup before retrying.

## Upgrading multiple machines

Each machine has its own local database. Upgrading the application does not
synchronise databases between machines. Copy a database only while the
application is locked, and copy the `.meta` file with it. Do not place the same
database on a shared drive for simultaneous use; the application is
single-user and does not implement multi-writer coordination.
