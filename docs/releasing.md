# Releasing

Releases are built and published by GitHub Actions. A version tag is the only
manual trigger required.

## Before tagging

1. Update `project(... VERSION ...)` in `CMakeLists.txt`, the README artifact
   names, and the upgrade examples.
2. Run locally:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMDB_WERROR=ON
   cmake --build build
   ctest --test-dir build --output-on-failure
   (cd server && gofmt -l . && go vet ./... && go test ./...)
   # Optional vulnerability scan:
   (cd server && govulncheck ./...)
   python3 packaging/embed_web.py --check
   git diff --check
   ```

3. Test migrations against at least the previous schema version. v0.7.0 does
   not change the schema, but verify that a v0.6.0 database opens and that its
   inventory, diagrams, scans, and audit chain remain intact.
4. Verify both Linux packages:
   - the desktop+server package contains `cmdb`, `cmdb-server`, `cmdb-rpc`, and
     `share/cmdb-lite/web`;
   - the server-only package contains `cmdb-server`, `cmdb-rpc`, web assets, the
     systemd unit, and the passphrase example, and does **not** contain the
     desktop `cmdb` binary or GTK/WebKit dependencies.
   Verify server setup, mandatory TOTP enrollment, recovery-code generation, and
   forced password change from a fresh database. Check the package contents explicitly:

   ```bash
   dpkg-deb -c dist/*.deb | grep -E 'bin/(cmdb|cmdb-server|cmdb-rpc)|cmdb-lite/web'
   rpm -qlp dist/*.rpm | grep -E 'bin/(cmdb|cmdb-server|cmdb-rpc)|cmdb-lite/web'
   tar -tzf dist/*.tar.gz | grep -E 'bin/(cmdb|cmdb-server|cmdb-rpc)|cmdb-lite/web'
   ```
5. Run the clean-Windows-VM checklist and a Linux GUI/Flatpak smoke test on the
   target distributions.
6. Signing and SBOMs are not required for the current rollout. Windows artifacts
   are unsigned and may trigger SmartScreen; record that expectation.
7. Commit and push `main`.
8. Wait for the normal `main` CI run to pass.

## Tag and publish

```bash
git tag -a v0.7.0 -m "cmdb-lite v0.7.0"
git push origin v0.7.0
```

The tag triggers `.github/workflows/ci.yml` for Linux, Fedora, Flatpak, and
Windows. Fedora CI passes `-DCMDB_PACKAGE_VARIANT=fedora`, so its tarballs are
distinct from the Ubuntu-built tarballs. When all four jobs pass, the `release`
job:

1. downloads the four platform artifacts plus the two server-only Linux
   artifacts;
2. copies the desktop DEB/RPM/tarballs, server-only DEB/RPM/tarballs, Flatpak,
   Windows ZIP, and MSI into one asset directory;
3. generates `SHA256SUMS`;
4. creates a GitHub Release with generated notes.

No manual artifact assembly or `gh release create` step is required.

## Verify the release

```bash
gh release view v0.7.0
gh release download v0.7.0 --dir /tmp/cmdb-release
cd /tmp/cmdb-release
sha256sum -c SHA256SUMS
```

Windows artifacts are unsigned by the current rollout decision. Use
`SHA256SUMS` and expect a possible SmartScreen warning.

`SHA256SUMS` is generated from the release assets, but it is not itself signed.
If the GitHub release or workflow is compromised, the checksums provide no
independent trust anchor. Signing, SBOMs, and build provenance are not required
for this rollout.

## If a job fails

The release job does not run unless all platform jobs pass. Fix the commit,
delete the tag locally and remotely, then re-tag:

```bash
git tag -d v0.7.0
git push origin :refs/tags/v0.7.0
# fix, commit, push main, wait for CI
git tag -a v0.7.0 -m "cmdb-lite v0.7.0"
git push origin v0.7.0
```

Do not move a published release tag. Publish a new patch version instead.
