#!/bin/sh
# Local demo runner. Builds the image and starts one disposable replica with
# the same isolation flags as the compose file.

set -eu

cd "$(dirname "$0")/.."

docker build -f demo/Dockerfile -t cmdb-lite-demo .

docker run --rm \
  --name cmdb-lite-demo \
  -p "${CMDB_HTTP_BIND:-127.0.0.1}:${CMDB_HTTP_PORT:-8080}:8080" \
  --read-only \
  --tmpfs /tmp:rw,noexec,nosuid,size=64m,mode=1777 \
  --memory 1g \
  --pids-limit 256 \
  --security-opt no-new-privileges:true \
  --cap-drop ALL \
  -v cmdb-demo-data:/data \
  cmdb-lite-demo
