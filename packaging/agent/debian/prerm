#!/bin/sh
set -e

if [ -d /run/systemd/system ]; then
  systemctl disable --now cmdb-agent.service >/dev/null 2>&1 || true
fi

exit 0
