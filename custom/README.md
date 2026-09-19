# custom/ — everything this fork adds

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
shows the "Global Resources Reset — Resources failed to load previously"
dialog at every launch.

`install.sh` additionally bakes the skin texture into the game's *default*
skins: the vanilla `skin_packs/vanilla/steve.png` and `alex.png` inside the
extracted game assets are replaced with `skin/Nekomix/nekomix.png` (originals
kept as `*.png.bak`). Bedrock always has a default skin even when no pack is
selected, so the custom skin then shows permanently everywhere — in menus, on
your own player model, and (client-side) as the default even if a future
session picks no pack. Re-run `install.sh` after a game-version update.

## Discord Rich Presence

`mods/discordrpc/` adds rich presence to the launcher/game session: while
playing, your Discord profile shows **Playing Minecraft** with a state that
tracks what you are doing, pushed to Discord the moment it changes:

- `In the launcher...` while the game process boots,
- `In the menus` once it reaches the menu,
- `In the Overworld` / `In the Nether` / `In the End` while a singleplayer
  world is loaded, with `Playing <world name>` on the second line — the
  dimension is read straight from the world's leveldb (Bedrock only keeps a
  marker key named after the *active* dimension), so it updates as you step
  through a portal. If it cannot be read yet it falls back to `In a survival
  world: <name>` / `In a creative world: <name>` / etc. (name and game mode
  parsed from `level.dat`; state updates within ~1s),
- `On a server` / `On a Realm` while an external server or Realm session is
  live — **auto-detected**: online sessions stream chunks into
  `minecraftpe/blob_cache/` (the mod watches `/proc/self/fd` for exactly one
  of "local world files" or "blob cache log"), so nothing needs configuring.
  Set `multiplayer=server` / `multiplayer=realm` in the config only to pin the
  exact wording; without it the label is `On a server or Realm`. `server_name`
  adds a name (`On CubeCraft`); `dimension` labels the current dimension for
  online sessions (which keep no local world to read it from). The config file
  is re-read live every ~15 seconds, so edits apply without a restart.

The mod is deliberately free of game-internal hooks: it detects a loaded world
by watching which `.../minecraftWorlds/<id>/` files the process holds open
(via `/proc/self/fd`) and talks to Discord over its public unix-socket IPC
protocol. That means it needs no signature updates and cannot crash the game
on a future game update (unlike the hook-based mods above). The same
`/proc/self/fd` watch detects server/realm sessions via the blob-cache
write-ahead log, so it is just as update-proof.

While you are inside a world the presence also carries a party + join secret,
so friends get a **Join** button on your profile; requests land in
`~/.local/share/mcpelauncher/discordrpc.join` and the counter in
`discordrpc.state` ticks up. Configure `join_address` (your reachable
LAN/VPN/public address) so joiners see what to connect to. Art assets come
from your own Discord application — upload them in the Developer Portal and
reference their keys with `large_image` / `small_image` in the config.

Enable it once (2 minutes, requires your own Discord app ID — Discord shows
the app's registered name/artwork, which only your own application controls):

```bash
cat > ~/.local/share/mcpelauncher/discordrpc.conf <<'EOF'
client_id=PASTE_YOUR_DISCORD_APPLICATION_CLIENT_ID
EOF
```

Then restart the game with Discord desktop running. Status is written to
`~/.local/share/mcpelauncher/discordrpc.state`; details and setup notes live
in `mods/discordrpc/README.md`. `install.sh` seeds a default
`discordrpc.conf` (with every key commented, `client_id=` empty) so the file
is already in place.
