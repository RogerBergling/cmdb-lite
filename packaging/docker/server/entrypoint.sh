#!/bin/sh
# Production entrypoint. Explicit arguments are passed through to cmdb-server
# so the image can be used for --create-db, --reset-auth, and --version.

set -eu

SERVER=/opt/cmdb/cmdb-server
DB_PATH="${CMDB_DB_PATH:-/data/inventory.cmdb}"
LISTEN="${CMDB_LISTEN:-0.0.0.0:8080}"
PROXY="${CMDB_PROXY:-127.0.0.1/32,::1/128}"
SESSION_IDLE="${CMDB_SESSION_IDLE:-30m}"
SESSION_ABSOLUTE="${CMDB_SESSION_ABSOLUTE:-12h}"
AGENT_INTERVAL="${CMDB_AGENT_INTERVAL:-24h}"
AGENT_RETENTION_DAYS="${CMDB_AGENT_RETENTION_DAYS:-365}"
AGENT_REPORTS_PER_HOST="${CMDB_AGENT_REPORTS_PER_HOST:-90}"

export CMDB_CONFIG_DIR="${CMDB_CONFIG_DIR:-/tmp/cmdb-config}"
export TMPDIR="${TMPDIR:-/tmp}"
mkdir -p "$CMDB_CONFIG_DIR" "$TMPDIR"

read_passphrase() {
  if [ -n "${CMDB_DB_PASSPHRASE_FILE:-}" ]; then
    if [ ! -r "$CMDB_DB_PASSPHRASE_FILE" ]; then
      echo "cmdb-server: cannot read CMDB_DB_PASSPHRASE_FILE" >&2
      exit 1
    fi
    CMDB_DB_PASSPHRASE="$(cat "$CMDB_DB_PASSPHRASE_FILE")"
    export CMDB_DB_PASSPHRASE
  fi
}

# Version/help do not need the database or its passphrase.
case "${1:-}" in
  --version|--help|-h)
    exec "$SERVER" "$@"
    ;;
esac

read_passphrase

if [ "$#" -gt 0 ]; then
  exec "$SERVER" "$@"
fi

if [ -z "${CMDB_DB_PASSPHRASE:-}" ]; then
  echo "cmdb-server: set CMDB_DB_PASSPHRASE or CMDB_DB_PASSPHRASE_FILE" >&2
  exit 1
fi

if [ ! -f "$DB_PATH" ]; then
  echo "cmdb-server: database not found: $DB_PATH" >&2
  echo "create it once with:" >&2
  echo "  docker compose run --rm cmdb-server --db $DB_PATH --create-db" >&2
  exit 1
fi

exec "$SERVER" \
  --db "$DB_PATH" \
  --listen "$LISTEN" \
  --rpc /opt/cmdb/cmdb-rpc \
  --web-dir /opt/cmdb/web \
  --proxy "$PROXY" \
  --session-idle "$SESSION_IDLE" \
  --session-absolute "$SESSION_ABSOLUTE" \
  --agent-interval "$AGENT_INTERVAL" \
  --agent-retention-days "$AGENT_RETENTION_DAYS" \
  --agent-reports-per-host "$AGENT_REPORTS_PER_HOST"
