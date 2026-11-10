# Flatpak (self-contained Linux build)

The Flatpak bundles the application, GTK3, WebKitGTK, SQLCipher, libsodium,
expat, and nmap with the GNOME runtime. It is the recommended Linux artifact
when you do not want to install distribution development packages.

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
bundled libraries.

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
