#!/bin/sh
set -e

if [ "$1" = "0" ] && [ -d /run/systemd/system ]; then
  systemctl stop cmdb-server.service >/dev/null 2>&1 || true
  systemctl disable cmdb-server.service >/dev/null 2>&1 || true
fi

exit 0
