# mcpelauncher

A Linux launcher for Minecraft: Bedrock Edition (Android builds), with the
PairIP / PlayFab patches needed to run recent game versions.

Everything lives in this one repository: the launcher source, the UI source,
the runtime mod, the Arch build files, and the auto-build workflow.

## Install (Arch Linux)

```bash
curl -sL https://raw.githubusercontent.com/komixkat/mcpe/qt6/install.sh | bash
```

This downloads the latest built `mcpelauncher-linux` and `mcpelauncher-ui`
packages from the GitHub Releases of this repository and installs them with
pacman. No AUR, no compilation on your machine.

Binaries are rebuilt and published automatically on a weekly schedule (and
on every push to the `qt6` branch) by the `build` workflow in
`.github/workflows/`.

## Build from source

Everything is in-tree, including all UI components (vendored from
`minecraft-linux/mcpelauncher-ui-manifest` into `ui/`).

```bash
git clone --recurse-submodules -b qt6 https://github.com/komixkat/mcpe.git
cd mcpe

# launcher core
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-core --parallel

# UI
cmake -S ui -B build-ui -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-ui --parallel
```

## Arch packages

- `packaging/core/PKGBUILD` builds `mcpelauncher-linux` (the launcher core from
  this repo, with the fixes and the updates mod).
- `packaging/ui/PKGBUILD` builds `mcpelauncher-ui` (the Qt login/version UI,
  built from the vendored `ui/` directory).

```bash
cd packaging/core && makepkg -f
cd ../ui && makepkg -f
sudo pacman -U mcpelauncher-linux-git-*.pkg.tar.zst mcpelauncher-ui-*.pkg.tar.zst
```

## Run

Launch `mcpelauncher-ui-qt`, log in, and pick a version. The UI downloads and
launches the game through `mcpelauncher-client`.

The launcher core still runs on an X11 EGL stack, so on a Wayland session it
needs these environment variables. The desktop entry installed by the UI
already sets them, so launching from the application menu is automatic. Only
terminal launches need the prefix:

```bash
EGL_PLATFORM=x11 SDL_VIDEODRIVER=x11 mcpelauncher-ui-qt
```

A fresh login is required on first use. Do not copy tokens from another
installation, stale tokens cause repeated Microsoft account lockouts.

## Updating from upstream

The launcher source in this repo tracks `minecraft-linux/mcpelauncher-manifest`
on the `qt6` branch. The `sync-upstream` workflow (`.github/workflows/`)
checks weekly for upstream changes and records them in
`packaging/upstream.state`; if upstream moved it opens a pull request so the
vendored sources and fix patches (`packaging/core/fixes/`) can be refreshed
and rebuilt.

## Repository layout

- `mcpelauncher-client/`, `mcpelauncher-core/` and the rest: the launcher
  source with the login and 1.26.x fixes applied.
- `ui/`: the Qt launcher UI source (vendored, single-tree).
- `mods/`: `libmcpelauncher-updates.so` and the PairIP / PlayFab patches that
  the launcher loads at runtime.
- `packaging/`: Arch `PKGBUILD`s, the uploaded fix patches, and the recorded
  upstream state.
- `install.sh`: the one-command installer.
- `.github/workflows/`: `build.yml` (compiles and publishes packages) and
  `sync-upstream.yml` (tracks upstream).

## License

GPL-3.0-only. See `LICENSE` for details.