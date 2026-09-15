# mcpelauncher

A Linux launcher for Minecraft: Bedrock Edition (Android builds).

This repository contains the launcher source code, build files, and the
runtime mod required to run recent game versions.

## Requirements

| Item | Requirement |
|------|-------------|
| OS | Linux (glibc, x86_64) |
| Toolchain | CMake 3.5+, Clang, Make |
| Runtime | Qt 6, curl, libpng, libevdev, systemd, libxi, libegl, libuv, zlib |

## Build

```bash
git clone --recurse-submodules -b qt6-fixed https://github.com/komixkat/mcpe.git
cd mcpe
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel
```

The launcher binaries are created in `build/mcpelauncher-client/`.

## Install and run

1. Install the launcher UI for login and version management:
   `mcpelauncher-ui` (AUR) or https://github.com/minecraft-linux/mcpelauncher-ui-manifest

2. Deploy the updates mod. The mod is required for recent game versions.

```bash
mkdir -p ~/.local/share/mcpelauncher
cp -r mods ~/.local/share/mcpelauncher/
```

3. Launch:

```bash
EGL_PLATFORM=x11 SDL_VIDEODRIVER=x11 build/mcpelauncher-client/mcpelauncher-client -dg ~/.local/share/mcpelauncher/versions/1.26.45.1
```

A fresh login is required on first use. Do not copy tokens from another
installation; stale tokens cause repeated Microsoft account lockouts.

## archlinux package

An Arch package (PKGBUILD) used to create a binary package from this source
is included in `packaging/`:

```bash
cd packaging
makepkg -f
sudo pacman -U mcpelauncher-linux-git-*.pkg.tar.zst
```

## The updates mod

`mods/libmcpelauncher-updates.so` and the files in `mods/patches/` provide
the PairIP and PlayFab runtime patches. The launcher loads the mod from
`~/.local/share/mcpelauncher/mods/`. The mod must not be removed.

## License

GPL-3.0-only. See `LICENSE` for details.