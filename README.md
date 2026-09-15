# mcpelauncher (komixkat fork — fixed build)

A fixed fork of the unofficial Linux launcher for the Android version of
**Minecraft: Bedrock Edition** (based on the `qt6` branch of the
mcpelauncher monorepo).

Unlike upstream, this build ships with the launch-crashing PairIP/emutls
hacks **disabled** and relies on the `mcpelauncher-updates` mod to handle
PairIP/PlayFab decryption at runtime. This makes recent Bedrock versions
(1.26.x) boot instead of crashing in an infinite JNI_OnLoad recursion.

---

## Install (Arch Linux / CachyOS / any pacman distro)

### Option A — Prebuilt package (recommended)

1. Download/build `mcpelauncher-linux-git-1.7.6.r2.gb4805a2-2-x86_64.pkg.tar.zst`.
   If you have the `mcpelauncher-linux-git` AUR dir checked out locally, run:
   ```bash
   makepkg -f --noconfirm        # ~10 min, builds from mcpe.tar.gz snapshot
   sudo pacman -U mcpelauncher-linux-git-1.7.6.r2.gb4805a2-2-x86_64.pkg.tar.zst
   ```
2. Install the launcher UI (login / version management):
   ```bash
   yay -S mcpelauncher-ui        # or from AUR cache if already fetched
   ```

### Option B — Build from source in this repo

```bash
git clone --recurse-submodules -b qt6 https://github.com/komixkat/mcpe.git
cd mcpe
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
# binaries land in build/mcpelauncher-client/mcpelauncher-client etc.
```

> **Note:** AUR PKGBUILD `makepkg` must build from the bundled `mcpe.tar.gz`
> snapshot, NOT from upstream submodules — the submodule origins in the
> monorepo's `.gitmodules` are relative/local paths that only resolve inside
> the original workspace.

---

## First run — the mod (REQUIRED for 1.26.x)

The launcher needs the **updates mod** deployed at exactly these paths
(it is NOT part of the package):

```
~/.local/share/mcpelauncher/mods/libmcpelauncher-updates.so
~/.local/share/mcpelauncher/mods/patches/libPlayFabMultiplayer.so
~/.local/share/mcpelauncher/mods/patches/v1.26.0.2/x86_64/libmaesdk.so
```

Deployed `libmcpelauncher-updates.so` must be the extended build (45.6 MB,
zip-dated 2026-07-11) that carries the extra `apply_rt0_style_patches` /
`g_hardware_candidate_patches` / wide version ranges — the GitHub master
version does **not** cover 1.26.45.1.

## Login & account

1. Launch `mcpelauncher-ui`.
2. Log in fresh (Xbox / Microsoft). **Do NOT** carry over tokens from an old
   install or from flatpak — stale + device-bound tokens cause repeated
   auth failures and Microsoft account lockouts (30 min–24 h).
3. If you were locked out, wait it out, then log in again on a clean
   `~/.local/share/mcpelauncher` (erase it for a fully fresh login).

## Running

```bash
EGL_PLATFORM=x11 SDL_VIDEODRIVER=x11 mcpelauncher-client -dg ~/.local/share/mcpelauncher/versions/<version>
# e.g.
EGL_PLATFORM=x11 SDL_VIDEODRIVER=x11 mcpelauncher-client -dg ~/.local/share/mcpelauncher/versions/1.26.45.1
```

`EGL_PLATFORM=x11` is required on Wayland sessions.

---

## What's fixed in this fork

| Issue | Status |
|------|--------|
| Wayland/EGL boot failure | Fixed |
| `pthread_sigmask` / `div` / `ldiv` missing-symbol crashes | Fixed |
| PairIP/emutls infinite JNI recursion on 1.26.x | Fixed by deferring to the mod |
| Game boots on 1.26.45.1 | Confirmed |
| UI texture loading ("unknown image type") | **Known upstream cosmetic bug** — 3D world renders, UI textures blank; affects all launcher versions incl. flatpak |
| Websocket/XBL signaling hang | Investigation pending a valid login |
| 1.26.0.2 / 1.26.60.23 | Untested with mod |

## Troubleshooting

- **Crashes on launch after version download:** the mod/patches are missing
  or wrong paths — re-deploy the mod files above.
- **Account temporarily locked:** wait 30 min–24 h, do not spam login,
  then log in fresh on a wiped `~/.local/share/mcpelauncher`.
- **Crashes inside Minecraft libs:** use `lldb`, not `gdb` (the custom NDK
  loader only notifies lldb).
- **Textures/UI blank but 3D world renders:** cosmetic upstream bug, see table.

## Related

- Upstream UI: https://github.com/minecraft-linux/mcpelauncher-ui-manifest
- Docs/wiki: https://mcpelauncher.readthedocs.io