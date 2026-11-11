# Flatpak (self-contained Linux build)

The Flatpak packages the application together with SQLCipher, libsodium, and
nmap. GTK3, WebKitGTK, expat, and the base libraries come from the GNOME runtime,
which Flatpak installs alongside the application. It is the recommended Linux
artifact when you do not want to install distribution development packages.

## Install a released bundle

```bash
flatpak install --user ./cmdb-lite.flatpak
flatpak run io.github.rogerbergling.CmdbLite
```

The application can read and write your chosen database in the home directory.
The sandbox has network access for authorised scans and graphics access for
WebKit.

## Build locally

Install `flatpak` and `flatpak-builder`, add Flathub, and install the GNOME 48
runtime and SDK:

```bash
flatpak remote-add --if-not-exists flathub \
  https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub \
  org.gnome.Platform//48 org.gnome.Sdk//48
```

From the repository root:

```bash
flatpak-builder --user --force-clean --repo=repo build-flatpak \
  packaging/flatpak/io.github.rogerbergling.CmdbLite.yml
flatpak build-bundle repo cmdb-lite.flatpak \
  io.github.rogerbergling.CmdbLite
```

The manifest downloads pinned SQLCipher 4.6.1, libsodium 1.0.20, libpcap
1.10.5, nmap 7.95, and the webview source. The application links against the
bundled libraries. The Flatpak installs the application license and
third-party notices under `share/licenses/cmdb-lite`; nmap is distributed under
the Nmap Public Source License, so review its terms before redistribution.
Flathub distribution is not planned for the current rollout, so AppStream
`metainfo.xml` is not included.

The sandbox grants broad home-directory and network access because the user may
choose any database path and the scanner needs nmap. Flatpak's file portal is
not used for arbitrary paths. Treat the sandbox as a trusted local application,
not as a hardened multi-tenant boundary.

## Sandbox and nmap

The manifest grants:

- `--share=network` for authorised scans;
- `--filesystem=home` and `--filesystem=xdg-documents` for the database;
- Wayland/X11, IPC, and DRI for the native webview.

nmap is installed inside the sandbox and is found on `PATH`. Service/version
detection (`-sV`) works. OS detection (`-O`) requires root and is not available
inside the normal Flatpak sandbox, so the application will reject it with a
clear error. Do not grant broad privileges to the sandbox just for `-O`; use the
distribution package on a trusted test host if you need active OS detection.

## Update and remove

```bash
# Reinstall a newer bundle over the old one
flatpak install --user ./cmdb-lite.flatpak

# Remove the application (the database is not touched)
flatpak uninstall io.github.rogerbergling.CmdbLite
```

The database and `.meta` sidecar remain in the directory you chose. Back them up
before a major upgrade. See [`upgrading.md`](upgrading.md).
