#!/bin/sh
set -e

if ! getent group cmdb-agent >/dev/null 2>&1; then
  groupadd --system cmdb-agent
fi
if ! getent passwd cmdb-agent >/dev/null 2>&1; then
  useradd --system --gid cmdb-agent --home-dir /nonexistent \
    --no-create-home --shell /sbin/nologin cmdb-agent
fi

install -d -o root -g cmdb-agent -m 0750 /etc/cmdb-lite-agent

if [ -d /run/systemd/system ]; then
  systemctl daemon-reload >/dev/null 2>&1 || true
  systemctl enable cmdb-agent.service >/dev/null 2>&1 || true
  if [ -f /etc/cmdb-lite-agent/agent.json ]; then
    systemctl try-restart cmdb-agent.service >/dev/null 2>&1 || true
  fi
fi

exit 0
