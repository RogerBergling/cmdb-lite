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
   python3 packaging/embed_web.py --check
   git diff --check
   ```

3. Test migrations against at least the previous schema version.
4. Commit and push `main`.
5. Wait for the normal `main` CI run to pass.

## Tag and publish

```bash
git tag -a v0.5.0 -m "cmdb-lite v0.5.0"
git push origin v0.5.0
```

The tag triggers `.github/workflows/ci.yml` for Linux, Fedora, Flatpak, and
Windows. When all four jobs pass, the `release` job:

1. downloads the four platform artifacts;
2. copies the DEB, RPM, portable Linux tarball, Flatpak, Windows ZIP, and MSI
   into one asset directory;
3. generates `SHA256SUMS`;
4. creates a GitHub Release with generated notes.

No manual artifact assembly or `gh release create` step is required.

## Verify the release

```bash
gh release view v0.5.0
gh release download v0.5.0 --dir /tmp/cmdb-release
cd /tmp/cmdb-release
sha256sum -c SHA256SUMS
```

## If a job fails

The release job does not run unless all platform jobs pass. Fix the commit,
delete the tag locally and remotely, then re-tag:

```bash
git tag -d v0.5.0
git push origin :refs/tags/v0.5.0
# fix, commit, push main, wait for CI
git tag -a v0.5.0 -m "cmdb-lite v0.5.0"
git push origin v0.5.0
```

Do not move a published release tag. Publish a new patch version instead.
