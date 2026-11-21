# Contributing and local commit workflow

This repository is validated locally before changes are committed. The same
commands are used by CI where the platform supports them.

## Before committing

Run the smallest set that covers the area you changed, then the full set before
pushing a feature:

```bash
# Generated UI must match src/web
python3 packaging/embed_web.py --check

# Whitespace and conflict-marker check
git diff --check

# C core, desktop, server, and C tests
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

# Agent tests and Linux package
cmake -S . -B build-agent -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMDB_BUILD_UI=OFF \
  -DCMDB_BUILD_SERVER=OFF \
  -DCMDB_BUILD_AGENT=ON \
  -DCMDB_BUILD_TESTS=ON
cmake --build build-agent
ctest --test-dir build-agent --output-on-failure
cpack --config build-agent/CPackConfig.cmake -G DEB -B build-agent/dist

# Go server and agent
(cd server && gofmt -l . && go vet ./... && go test ./...)
(cd agent && gofmt -l . && go vet ./... && go test ./...)
(cd agent && GOOS=windows GOARCH=amd64 CGO_ENABLED=0 \
  go build -trimpath -o /tmp/cmdb-agent.exe .)

# Optional hardening checks
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMDB_ENABLE_ASAN=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
  --suppress=missingIncludeSystem src include
```

If you changed UI source, regenerate the embedded bundle before the final check:

```bash
python3 packaging/embed_web.py
python3 packaging/embed_web.py --check
```

Do not commit database files, passphrases, enrollment or agent tokens, build
trees, package output, or local configuration. Check the staged diff before
committing:

```bash
git status --short
git diff --stat
git diff --cached --check
git diff --cached
```

## Commit

Use a short imperative subject and a body that explains the user-visible change,
security implications, schema migrations, and validation performed.

```bash
git add <files>
git commit -m "Add application inventory agent and history" -m "Describe the change and validation here."
```

Keep logical changes together. A schema change must include its migration,
tests, and data-model documentation in the same commit. A packaging change must
include the CI and release-asset updates. Do not amend a pushed commit on `main`
without coordinating with the other users of the repository.

## Push

The development branch is `main` and the remote is `origin`:

```bash
git push origin main
```

Pushing to `main` runs the standard CI jobs: Linux, Fedora, Flatpak, and
Windows. A `v*` tag additionally runs the release job and publishes the
artifacts and `SHA256SUMS`:

```bash
git tag -a v0.7.2 -m "cmdb-lite v0.7.2"
git push origin v0.7.2
```

Before a release, complete the platform checks in
[`releasing.md`](releasing.md). In particular, Windows agent MSI
install/enroll/upgrade/uninstall and Fedora RPM behavior are validated on those
platforms; the Linux development host cannot substitute for them.

## Agent-specific release checks

- Build the agent DEB/RPM/TGZ and confirm `/usr/bin/cmdb-agent` and
  `cmdb-agent.service` are present.
- Install the agent on the target Ubuntu, Kali, and Fedora images, enroll it,
  verify the first report, and check the **Applications** tab.
- Install the Windows MSI on a clean Windows 11 and Windows Server 2019/2022 VM.
  Verify the service, enrollment, upgrade, uninstall, configuration removal, and
  Programs and Features entry.
- Run the end-to-end flow against a fresh server: first-run admin setup, TOTP,
  recovery codes, enrollment-token creation, agent enrollment, report, current
  software, change events, and history.
- Verify retention with `--agent-retention-days` and
  `--agent-reports-per-host`.
- Verify that backups and restores include the schema v5 agent tables and that
  existing pre-upgrade backups still open.

## Security review checklist

- No plaintext token is written to logs, audit details, or documentation.
- A used enrollment token is rejected; a revoked agent token returns HTTP 401.
- Agent URLs outside loopback require HTTPS; redirects are not followed.
- Agent configuration permissions are `0600`/`0640` and root/service-group
  owned on Linux, or ACL-restricted on Windows.
- Agent request bodies and application fields are bounded; duplicate application
  keys are rejected.
- The agent executes no shell and accepts no remote commands.
- Reports with collector errors are marked partial and do not remove current
  applications.
