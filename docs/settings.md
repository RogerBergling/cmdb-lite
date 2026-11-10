# User settings

`cmdb-lite` keeps a small per-user configuration file outside the encrypted
database. It contains no passphrases or inventory data.

## Location

| Platform | Path |
|---|---|
| Linux | `$XDG_CONFIG_HOME/cmdb-lite/config.json`, or `~/.config/cmdb-lite/config.json` |
| Windows | `%APPDATA%\cmdb-lite\config.json` |
| Test/override | `CMDB_CONFIG_DIR/cmdb-lite/config.json` |

The file is written with mode `0600` where supported.

## Contents

```json
{
  "version": 1,
  "last_database": "/home/user/inventory.cmdb",
  "recent_databases": [
    "/home/user/inventory.cmdb",
    "/home/user/test.cmdb"
  ]
}
```

- `last_database` pre-fills the unlock screen.
- `recent_databases` is a deduplicated list of up to eight paths, newest first.
- Only paths are stored. The passphrase and derived key never enter this file.
- Deleting the config file only resets convenience settings; it does not affect
  the database.

The **Theme** preference is stored separately in the encrypted database under
`app_meta.ui.theme`, with a best-effort local-storage copy.
