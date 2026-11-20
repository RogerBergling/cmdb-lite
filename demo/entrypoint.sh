#!/bin/sh
# Demo entrypoint. Everything is disposable: the SQLCipher database is
# recreated from fixtures on request, and the plaintext demo credentials are
# written next to it for the login banner.

set -eu

DATA_DIR="${CMDB_DATA_DIR:-/data}"
DB_PATH="${CMDB_DB_PATH:-${DATA_DIR}/demo.cmdb}"
CREDENTIALS="${CMDB_DEMO_CREDENTIALS:-${DATA_DIR}/demo-credentials.json}"
PASSPHRASE="${CMDB_DB_PASSPHRASE:-demo-only-public-passphrase}"
RESET_ON_START="${CMDB_DEMO_RESET_ON_START:-1}"
RESET_HOURS="${CMDB_DEMO_RESET_HOURS:-24}"
LISTEN="${CMDB_LISTEN:-0.0.0.0:8080}"
PROXY="${CMDB_PROXY:-127.0.0.1/32,::1/128,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16}"
SESSION_IDLE="${CMDB_SESSION_IDLE:-30m}"
SESSION_ABSOLUTE="${CMDB_SESSION_ABSOLUTE:-12h}"

export CMDB_DB_PASSPHRASE="$PASSPHRASE"
export CMDB_DEMO_CREDENTIALS="$CREDENTIALS"
export CMDB_CONFIG_DIR="${CMDB_CONFIG_DIR:-/tmp/cmdb-demo-config}"
export CMDB_NMAP="${CMDB_NMAP:-/opt/demo/fake-nmap}"
export TMPDIR="${TMPDIR:-/tmp}"

mkdir -p "$DATA_DIR" "$CMDB_CONFIG_DIR" "$TMPDIR"

if [ ! -w "$DATA_DIR" ]; then
  echo "demo: $DATA_DIR is not writable; mount it with uid 10001 or chown it to 10001:10001" >&2
  exit 1
fi
if [ ! -w "$CMDB_CONFIG_DIR" ]; then
  echo "demo: $CMDB_CONFIG_DIR is not writable" >&2
  exit 1
fi

reset_demo() {
  rm -f \
    "$DB_PATH" \
    "${DB_PATH}.meta" \
    "${DB_PATH}-wal" \
    "${DB_PATH}-shm" \
    "${DB_PATH}-journal" \
    "$CREDENTIALS"
}

seed_demo() {
  if [ -f "$DB_PATH" ]; then
    if [ -f "$CREDENTIALS" ]; then
      return 0
    fi
    echo "demo: database exists without credentials; recreating" >&2
    reset_demo
  fi
  echo "demo: creating fixtures" >&2
  /opt/demo/demo-seed \
    -rpc /opt/cmdb/cmdb-rpc \
    -db "$DB_PATH" \
    -passphrase "$PASSPHRASE" \
    -fixtures /opt/demo/fixtures \
    -credentials "$CREDENTIALS"
}

run_server() {
  exec /opt/cmdb/cmdb-server \
    --db "$DB_PATH" \
    --listen "$LISTEN" \
    --rpc /opt/cmdb/cmdb-rpc \
    --web-dir /opt/cmdb/web \
    --proxy "$PROXY" \
    --session-idle "$SESSION_IDLE" \
    --session-absolute "$SESSION_ABSOLUTE"
}

case "$RESET_HOURS" in
  ''|*[!0-9]*)
    echo "demo: CMDB_DEMO_RESET_HOURS must be a non-negative integer" >&2
    exit 1
    ;;
esac

if [ "$RESET_HOURS" -eq 0 ]; then
  if [ "$RESET_ON_START" = "1" ]; then
    reset_demo
  fi
  seed_demo
  run_server
fi

RESET_SECONDS=$((RESET_HOURS * 3600))
child=""
first_run=1
trap 'if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || true; fi; exit 0' TERM INT

while :; do
  if [ "$first_run" = "1" ] && [ "$RESET_ON_START" != "1" ]; then
    first_run=0
  else
    reset_demo
  fi
  seed_demo
  timeout "$RESET_SECONDS" /opt/cmdb/cmdb-server \
    --db "$DB_PATH" \
    --listen "$LISTEN" \
    --rpc /opt/cmdb/cmdb-rpc \
    --web-dir /opt/cmdb/web \
    --proxy "$PROXY" \
    --session-idle "$SESSION_IDLE" \
    --session-absolute "$SESSION_ABSOLUTE" &
  child=$!
  wait "$child" || true
  child=""
  sleep 2
done
