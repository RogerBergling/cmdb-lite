#!/usr/bin/env bash
set -euo pipefail

# cmdb-lite Linux setup: install build dependencies, build, and run tests.
# Run from the repository root with:  ./setup.sh

cd "$(dirname "$0")"

if [ "$(id -u)" -eq 0 ]; then
  echo "Run this script as your normal user, not with sudo." >&2
  echo "The script will ask for sudo only for the apt-get step." >&2
  exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This setup script is for Debian/Ubuntu systems (apt-get)." >&2
  echo "Install the equivalent packages for your distribution and then run:" >&2
  echo "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug" >&2
  echo "  cmake --build build" >&2
  echo "  ctest --test-dir build --output-on-failure" >&2
  exit 1
fi

echo "== Installing build dependencies (sudo will ask for your password) =="
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  fakeroot dpkg-dev \
  libsqlcipher-dev libsodium-dev libexpat1-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev \
  clang lld valgrind cppcheck

echo
echo "== Configuring cmdb-lite =="
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMDB_WERROR=ON

echo
echo "== Building cmdb-lite =="
cmake --build build

echo
echo "== Running tests =="
ctest --test-dir build --output-on-failure

echo
echo "== Building Debian/Ubuntu packages =="
rm -rf build/dist
cpack --config build/CPackConfig.cmake -G DEB -B build/dist
cpack --config build/CPackConfig.cmake -G TGZ -B build/dist
ls -lh build/dist

echo
echo "== Done =="
echo "Start the application with:"
echo "  ./build/cmdb"
echo "Packages:"
echo "  build/dist/*.deb"
echo "  build/dist/*.tar.gz"
