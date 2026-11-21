# Production server-only image. The public demo image is separate.
#
# Build from the repository root:
#   docker build -f packaging/docker/server/Dockerfile \
#     -t cmdb-lite-server .

ARG UBUNTU_VERSION=24.04

FROM ubuntu:${UBUNTU_VERSION} AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG CMDB_BUILD_NUMBER=dev
ARG CMDB_BUILD_COMMIT=unknown

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      cmake \
      g++ \
      gcc \
      git \
      golang-go \
      libexpat1-dev \
      libsqlcipher-dev \
      libsodium-dev \
      libssl-dev \
      ninja-build \
      pkg-config \
      zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN cmake -S /src -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMDB_BUILD_UI=OFF \
      -DCMDB_BUILD_SERVER=ON \
      -DCMDB_BUILD_TESTS=OFF \
      -DCMDB_STATIC_CRYPTO=ON \
      -DCMDB_BUILD_NUMBER="${CMDB_BUILD_NUMBER}" \
      -DCMDB_BUILD_COMMIT="${CMDB_BUILD_COMMIT}" \
 && cmake --build /build --parallel \
 && DESTDIR=/stage cmake --install /build

FROM ubuntu:${UBUNTU_VERSION} AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      libexpat1 \
      libssl3t64 \
      nmap \
      passwd \
      tini \
      wget \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --user-group --home-dir /data \
      --shell /usr/sbin/nologin cmdb \
 && install -d -o 10001 -g 10001 -m 0700 /data

COPY --from=build /stage/usr/bin/cmdb-server /opt/cmdb/cmdb-server
COPY --from=build /stage/usr/bin/cmdb-rpc /opt/cmdb/cmdb-rpc
COPY --from=build /stage/usr/share/cmdb-lite/web /opt/cmdb/web
COPY packaging/docker/server/entrypoint.sh /opt/cmdb/entrypoint.sh

RUN chmod 0755 \
      /opt/cmdb/cmdb-server \
      /opt/cmdb/cmdb-rpc \
      /opt/cmdb/entrypoint.sh

ENV CMDB_DB_PATH=/data/inventory.cmdb \
    CMDB_LISTEN=0.0.0.0:8080 \
    CMDB_PROXY=127.0.0.1/32,::1/128 \
    CMDB_CONFIG_DIR=/tmp/cmdb-config \
    CMDB_SESSION_IDLE=30m \
    CMDB_SESSION_ABSOLUTE=12h \
    CMDB_AGENT_INTERVAL=24h \
    CMDB_AGENT_RETENTION_DAYS=365 \
    CMDB_AGENT_REPORTS_PER_HOST=90

LABEL org.opencontainers.image.title="cmdb-lite server" \
      org.opencontainers.image.description="Single-node cmdb-lite server with external TLS proxy" \
      org.opencontainers.image.source="https://github.com/RogerBergling/cmdb-lite" \
      org.opencontainers.image.licenses="MIT"

USER 10001:10001
WORKDIR /data
EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
  CMD port="${CMDB_LISTEN##*:}"; [ -n "$port" ] || port=8080; \
  wget -qO- "http://127.0.0.1:$port/healthz" >/dev/null || exit 1

STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/bin/tini", "--", "/opt/cmdb/entrypoint.sh"]
