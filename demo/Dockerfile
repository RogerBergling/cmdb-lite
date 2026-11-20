# Demo-only image. This is not a production packaging path.
#
# Build from the repository root:
#   docker build -f demo/Dockerfile -t cmdb-lite-demo .

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
      -DCMDB_SERVER_DEMO=ON \
      -DCMDB_BUILD_TESTS=OFF \
      -DCMDB_STATIC_CRYPTO=ON \
      -DCMDB_BUILD_NUMBER="${CMDB_BUILD_NUMBER}" \
      -DCMDB_BUILD_COMMIT="${CMDB_BUILD_COMMIT}" \
 && cmake --build /build --parallel \
 && DESTDIR=/stage cmake --install /build

RUN cd /src/server \
 && CGO_ENABLED=0 GOTOOLCHAIN=local \
      go build -trimpath -o /build/demo-seed ./cmd/demo-seed

FROM ubuntu:${UBUNTU_VERSION} AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates \
      libexpat1 \
      libssl3t64 \
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
COPY --from=build /build/demo-seed /opt/demo/demo-seed
COPY demo/entrypoint.sh /opt/demo/entrypoint.sh
COPY demo/fake-nmap /opt/demo/fake-nmap
COPY demo/fixtures /opt/demo/fixtures

RUN chmod 0755 \
      /opt/cmdb/cmdb-server \
      /opt/cmdb/cmdb-rpc \
      /opt/demo/demo-seed \
      /opt/demo/entrypoint.sh \
      /opt/demo/fake-nmap

ENV CMDB_DB_PASSPHRASE=demo-only-public-passphrase \
    CMDB_DEMO_CREDENTIALS=/data/demo-credentials.json \
    CMDB_DEMO_RESET_HOURS=24 \
    CMDB_CONFIG_DIR=/tmp/cmdb-demo-config \
    CMDB_LISTEN=0.0.0.0:8080 \
    CMDB_NMAP=/opt/demo/fake-nmap \
    CMDB_PROXY=127.0.0.1/32,::1/128,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16

LABEL org.opencontainers.image.title="cmdb-lite demo" \
      org.opencontainers.image.description="Public demo of cmdb-lite with fake data" \
      org.opencontainers.image.source="https://github.com/RogerBergling/cmdb-lite" \
      org.opencontainers.image.licenses="MIT"

USER 10001:10001
WORKDIR /data
EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=30s --retries=3 \
  CMD wget -qO- http://127.0.0.1:8080/login >/dev/null || exit 1

ENTRYPOINT ["/usr/bin/tini", "--", "/opt/demo/entrypoint.sh"]
