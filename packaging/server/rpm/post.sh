#!/bin/sh
set -e

if ! getent group cmdb >/dev/null 2>&1; then
  groupadd --system cmdb
fi
if ! getent passwd cmdb >/dev/null 2>&1; then
  useradd --system --gid cmdb --home-dir /var/lib/cmdb-lite \
    --no-create-home --shell /sbin/nologin cmdb
fi

install -d -o cmdb -g cmdb -m 0700 /var/lib/cmdb-lite
install -d -o root -g root -m 0700 /etc/cmdb-lite

if [ -d /run/systemd/system ]; then
  systemctl daemon-reload >/dev/null 2>&1 || true
  systemctl try-restart cmdb-server.service >/dev/null 2>&1 || true
fi

exit 0
