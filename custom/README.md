# custom/ — everything this fork adds

Everything in this directory is maintained by this fork. It is kept apart from
the vendored upstream launcher/UI trees so the fork's additions have one
obvious home and can be built, tested, and shipped as a unit.

```
custom/
├── mods/                    curated, fixed launcher mods (vendored source)
│   ├── build-mods.sh        cross-compiles every mod with the Android NDK
│   ├── fullbright/          fullbright mod
│   ├── snaplook/            third-person-back toggle + first-person-while-zooming
│   ├── zoom/                scroll zoom + FOV-proportional look dampening
│   └── shulkerpreview/      shulker box / bundle preview on hover
├── skin/
│   └── Nekomix/             bundled custom skin pack
└── patches/                 diffs vs. the upstream mod revisions + provenance
```

Everything here is built by `.github/workflows/build.yml` on every push to
`qt6` and attached to the rolling `latest` release:

- `mcpelauncher-mods-x86_64.tar.gz`
- `mcpelauncher-mods-arm64-v8a.tar.gz`
- `mcpelauncher-skinpack.tar.gz`

`install.sh` (repository root) downloads the bundle for the host architecture
and deploys it into `~/.local/share/mcpelauncher/mods/`, installs the skin
pack, and enables custom skins.

## Building the mods

The Android NDK is required (the mods run inside the game's Android process).
Any NDK r26/r27 works.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
./custom/mods/build-mods.sh              # x86_64 + arm64-v8a
./custom/mods/build-mods.sh x86_64       # one ABI
```

Output lands in `custom/mods/out/<abi>/`.

## Why the mods stay working

- **Updates can't clobber them.** The mods live in user data, not in the
  packages, so `pacman -U` never touches them. `install.sh` re-asserts the
  current fixed builds on every run and backs up what it replaces under
  `~/.local/share/mcpelauncher/mod-backups/`.
- **A removed upstream can't break installs.** The sources are vendored here,
  so a deleted GitHub project or dead download URL cannot affect a build.
- **A future game version can't crash the launcher.** Every mod validates each
  game-window export, each RTTI chain, and each byte signature before hooking.
  On a mismatch it logs `... disabled` and leaves the game untouched instead of
  dereferencing a bad pointer.
- **Zoom sensitivity always matches the zoom level.** The zoom mod computes the
  angular FOV ratio from the live camera each frame, from the default 16.8°
  all the way down to the 0.9° maximum. The exact ratio feels too slow to most
  players, so it is scaled up by `sensitivityMultiplier` (default `2`) and
  kept above `sensitivityFloor` (default `0.15`). Tune both in
  `games/com.mojang.minecraftpe/zoom.conf` (i.e. `~/.local/share/mcpelauncher/zoom.conf`):

  ```
  zoomKey=67
  animated=true
  sensitivityMultiplier=2
  sensitivityFloor=0.15
  ```

  `multiplier=1` + `floor=0` restores the exact screen-space ratio.

## If a game update does invalidate a signature

The mods log which step failed. Rebuild and reinstall:

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
./custom/mods/build-mods.sh x86_64
sudo install -Dm644 custom/mods/out/x86_64/lib*.so ~/.local/share/mcpelauncher/mods/
```

Then update any changed signature in the relevant `src/` file and push; CI
publishes new bundles automatically.

## Skin pack

`skin/Nekomix/` is a normal Bedrock skin pack. It is installed to
`games/com.mojang/skin_packs/`. Bedrock only applies skins that are not from
the Marketplace when **Only Allow Trusted Skins** is off, so both `install.sh`
and the launcher client force `only_show_trusted_skins=0` in `options.txt`.
