#!/usr/bin/env bash
set -euo pipefail

# cmdb-lite Fedora setup: install dependencies, build, test, and create
# RPM/TGZ packages.
#
# Run from the repository root with:
#   ./setup-fedora.sh

cd "$(dirname "$0")"

if [ "$(id -u)" -eq 0 ]; then
  echo "Run this script as your normal user, not with sudo." >&2
  echo "The script will ask for sudo only for the dnf step." >&2
  exit 1
fi

if ! command -v dnf >/dev/null 2>&1; then
  echo "This setup script requires Fedora/RHEL with dnf." >&2
  exit 1
fi

echo "== Installing Fedora build dependencies (sudo may ask for your password) =="
sudo dnf install -y \
  gcc gcc-c++ cmake ninja-build pkgconf-pkg-config git python3 \
  rpm-build \
  sqlcipher-devel libsodium-devel libsodium-static \
  openssl-devel zlib-devel \
  gtk3-devel webkit2gtk4.1-devel

echo
echo "== Configuring cmdb-lite =="
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMDB_WERROR=ON

echo
echo "== Building cmdb-lite =="
cmake --build build

echo
echo "== Running tests =="
ctest --test-dir build --output-on-failure

echo
echo "== Building Fedora packages =="
rm -rf build/dist
cpack --config build/CPackConfig.cmake -G RPM -B build/dist
cpack --config build/CPackConfig.cmake -G TGZ -B build/dist
ls -lh build/dist

echo
echo "== Done =="
echo "Run:     ./build/cmdb"
echo "RPM:     $(find build/dist -name '*.rpm' -print -quit)"
echo "Tarball: $(find build/dist -name '*.tar.gz' -print -quit)"
