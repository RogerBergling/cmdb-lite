#!/bin/sh
# DEB-only installer and enroller for the cmdb-lite application inventory agent.
#
# Usage:
#   sudo ./install-agent.sh \
#     --server https://cmdb.example.com \
#     --token-file /root/cmdb-agent-token
#
#   sudo ./install-agent.sh \
#     --server https://cmdb.example.com \
#     --token '<one-time-token>' \
#     --ca-file /etc/pki/ca-trust/source/anchors/cmdb-ca.pem
#
#   sudo ./install-agent.sh --deb ./cmdb-lite-agent_0.7.2_amd64.deb ...
#
# Environment:
#   CMDB_AGENT_REPO     GitHub repository (default: RogerBergling/cmdb-lite)
#   CMDB_AGENT_VERSION  Release tag, or "latest" (default: latest)
#
# Debian packages cannot receive arbitrary flags through apt/dpkg. This script
# installs the DEB, then invokes `cmdb-agent enroll` with the supplied values.

set -eu

REPO="${CMDB_AGENT_REPO:-RogerBergling/cmdb-lite}"
VERSION="${CMDB_AGENT_VERSION:-latest}"
DEB=""
SERVER=""
TOKEN=""
TOKEN_FILE=""
CA_FILE=""
NO_START=0

usage() {
  cat <<'EOF'
Install and enroll the cmdb-lite agent from a Debian package.

Options:
  --server URL       cmdb-server HTTPS base URL (required)
  --token TOKEN      one-time enrollment token
  --token-file PATH  read the one-time token from a file (recommended)
  --ca-file PATH     additional CA bundle for a private TLS certificate
  --version TAG      release tag, or "latest" (default: latest)
  --deb PATH         install a local DEB instead of downloading one
  --no-start         write the configuration without starting the service
  -h, --help         show this help

Environment:
  CMDB_AGENT_REPO     GitHub repository (default: RogerBergling/cmdb-lite)
  CMDB_AGENT_VERSION  Release tag, or "latest" (default: latest)

Only amd64 DEB packages are supported.
EOF
}

need() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: required command not found: $1" >&2
    exit 1
  fi
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --server)
      [ "$#" -ge 2 ] || { echo "error: --server needs a value" >&2; exit 2; }
      SERVER="$2"
      shift 2
      ;;
    --token)
      [ "$#" -ge 2 ] || { echo "error: --token needs a value" >&2; exit 2; }
      TOKEN="$2"
      shift 2
      ;;
    --token-file)
      [ "$#" -ge 2 ] || { echo "error: --token-file needs a value" >&2; exit 2; }
      TOKEN_FILE="$2"
      shift 2
      ;;
    --ca-file)
      [ "$#" -ge 2 ] || { echo "error: --ca-file needs a value" >&2; exit 2; }
      CA_FILE="$2"
      shift 2
      ;;
    --version)
      [ "$#" -ge 2 ] || { echo "error: --version needs a value" >&2; exit 2; }
      VERSION="$2"
      shift 2
      ;;
    --deb)
      [ "$#" -ge 2 ] || { echo "error: --deb needs a value" >&2; exit 2; }
      DEB="$2"
      shift 2
      ;;
    --no-start)
      NO_START=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ "$(id -u)" -ne 0 ]; then
  echo "error: run this installer as root" >&2
  exit 1
fi

if [ -z "$SERVER" ]; then
  echo "error: --server is required" >&2
  usage >&2
  exit 2
fi

if [ -n "$TOKEN" ] && [ -n "$TOKEN_FILE" ]; then
  echo "error: use either --token or --token-file, not both" >&2
  exit 2
fi
if [ -z "$TOKEN" ] && [ -z "$TOKEN_FILE" ]; then
  echo "error: --token or --token-file is required" >&2
  usage >&2
  exit 2
fi
if [ -n "$TOKEN_FILE" ]; then
  if [ ! -r "$TOKEN_FILE" ]; then
    echo "error: cannot read token file: $TOKEN_FILE" >&2
    exit 1
  fi
  TOKEN="$(tr -d '\r\n' < "$TOKEN_FILE")"
fi
if [ -z "$TOKEN" ]; then
  echo "error: enrollment token is empty" >&2
  exit 1
fi
if [ -n "$CA_FILE" ]; then
  case "$CA_FILE" in
    /*) ;;
    *) CA_FILE="$(pwd)/${CA_FILE}" ;;
  esac
  if [ ! -r "$CA_FILE" ]; then
    echo "error: cannot read CA file: $CA_FILE" >&2
    exit 1
  fi
fi

case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "error: only amd64 DEB packages are published" >&2; exit 1 ;;
esac

TMP=""
cleanup() {
  if [ -n "$TMP" ] && [ -d "$TMP" ]; then
    rm -rf "$TMP"
  fi
}
trap cleanup EXIT INT TERM

if [ -z "$DEB" ]; then
  need curl
  need sha256sum

  if [ "$VERSION" = "latest" ]; then
    need sed
    if ! RELEASE_JSON="$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest")"; then
      echo "error: could not resolve the latest release for ${REPO}" >&2
      echo "Use --version v0.7.2 or --deb PATH instead." >&2
      exit 1
    fi
    TAG="$(printf '%s\n' "$RELEASE_JSON" \
      | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
      | head -n 1)"
    if [ -z "$TAG" ]; then
      echo "error: could not determine the latest release tag" >&2
      exit 1
    fi
  else
    case "$VERSION" in
      v*) TAG="$VERSION" ;;
      *) TAG="v$VERSION" ;;
    esac
  fi

  AGENT_VERSION="${TAG#v}"
  ASSET="cmdb-lite-agent_${AGENT_VERSION}_amd64.deb"
  BASE="https://github.com/${REPO}/releases/download/${TAG}"
  TMP="$(mktemp -d)"
  echo "Downloading ${ASSET} from ${TAG}..."
  if ! curl -fsSL "${BASE}/${ASSET}" -o "${TMP}/${ASSET}"; then
    echo "error: release asset not found: ${BASE}/${ASSET}" >&2
    exit 1
  fi
  if ! curl -fsSL "${BASE}/SHA256SUMS" -o "${TMP}/SHA256SUMS"; then
    echo "error: SHA256SUMS not found for ${TAG}" >&2
    exit 1
  fi
  if ! grep -E "[[:space:]]${ASSET}$" "${TMP}/SHA256SUMS" >/dev/null 2>&1; then
    echo "error: ${ASSET} is missing from SHA256SUMS" >&2
    exit 1
  fi
  (
    cd "$TMP"
    grep -E "[[:space:]]${ASSET}$" SHA256SUMS | sha256sum -c -
  )
  DEB="${TMP}/${ASSET}"
else
  case "$DEB" in
    /*) ;;
    *) DEB="$(pwd)/${DEB}" ;;
  esac
  if [ ! -f "$DEB" ]; then
    echo "error: DEB not found: $DEB" >&2
    exit 1
  fi
fi

echo "Installing ${DEB}..."
export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null 2>&1; then
  apt-get install -y "$DEB"
else
  need dpkg
  dpkg -i "$DEB"
fi

AGENT=/usr/bin/cmdb-agent
if [ ! -x "$AGENT" ]; then
  AGENT="$(command -v cmdb-agent || true)"
fi
if [ -z "$AGENT" ] || [ ! -x "$AGENT" ]; then
  echo "error: cmdb-agent was not installed" >&2
  exit 1
fi

echo "Enrolling agent..."
set -- enroll --server "$SERVER" --token "$TOKEN"
if [ -n "$CA_FILE" ]; then
  set -- "$@" --ca-file "$CA_FILE"
fi
if [ "$NO_START" = "1" ]; then
  set -- "$@" --no-start
fi
"$AGENT" "$@"

echo
echo "Agent installed and enrolled."
"$AGENT" status
