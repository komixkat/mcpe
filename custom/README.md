# custom/: everything this fork adds

Everything in this directory is maintained by this fork. It is kept apart from
the vendored upstream launcher/UI trees so the fork's additions have one
obvious home and can be built, tested, and shipped as a unit.

```
custom/
├── mods/                    curated, fixed launcher mods (vendored source)
│   ├── build-mods.sh        cross-compiles every mod with the Android NDK
│   ├── discordrpc/          Discord Rich Presence (version-independent)
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

`skin/Nekomix/` is a normal Bedrock skin pack (slim-arm model,
`geometry.humanoid.customSlim`). It is installed to
`games/com.mojang/skin_packs/`. Bedrock only applies skins that are not from
the Marketplace when **Only Allow Trusted Skins** is off, so both `install.sh`
and the launcher client force `only_show_trusted_skins=0` in `options.txt`.

`install.sh` also makes sure the pack-state files
(`global_resource_packs.json`, `resource_packs.json`,
`known_resource_packs.json`) exist in `games/com.mojang/` as `[]`: when they
are missing, every session is treated as a failed resource load and the game
shows the "Global Resources Reset - Resources failed to load previously"
dialog at every launch.

`install.sh` additionally bakes the skin texture into the game's *default*
skins: the vanilla `skin_packs/vanilla/steve.png` and `alex.png` inside the
extracted game assets are replaced with `skin/Nekomix/nekomix.png` (originals
kept as `*.png.bak`). Bedrock always has a default skin even when no pack is
selected, so the custom skin then shows permanently everywhere: in menus, on
your own player model, and (client-side) as the default even if a future
session picks no pack. Re-run `install.sh` after a game-version update.

## Discord Rich Presence

`mods/discordrpc/` adds minimal rich presence to the launcher/game session:
while playing, your Discord profile shows **`Playing Minecraft`** with an
elapsed timer and a single **`Join komixkat`** button that opens the Minecraft
profile page (`https://launch.minecraft.net/profile/komixkat`).

That is all it does - deliberately. No server/realm names, no dimensions, no
in-game name: nothing is scanned or detected from the game, so the presence
cannot show wrong text, stall, or freeze your session.

The mod is free of game-internal hooks by construction: it is just a unix
socket client of Discord's public IPC protocol, with no signatures and nothing
to break on a game update.

Enable it once (2 minutes, requires your own Discord app ID - Discord shows
the app's registered name/artwork, which only your own application controls):

```bash
cat > ~/.local/share/mcpelauncher/discordrpc.conf <<'EOF'
client_id=PASTE_YOUR_DISCORD_APPLICATION_CLIENT_ID
EOF
```

Then restart the game with Discord desktop running. Art assets come from your
own Discord application - upload them in the Developer Portal and reference
the key with `large_image` in the config. Setup notes live in
`mods/discordrpc/README.md`. `install.sh` seeds a default `discordrpc.conf`
(with every key commented, `client_id=` empty) so the file is already in
place.
