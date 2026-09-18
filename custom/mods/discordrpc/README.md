# Discord Rich Presence (mcpelauncherdiscordrpc)

Shows your Minecraft Bedrock session on your Discord profile while you play,
in the same window the launcher's process covers:

| where you are              | rich presence state        |
| -------------------------- | -------------------------- |
| game starting (boot)       | `In the launcher...`       |
| main menu / loading screen | `In the main menu`         |
| loaded into a world        | `Playing - <world name>`   |

`details` is always `Minecraft Bedrock <version>` (read live from
`versions/versions.ini`), so people see exactly which build you are on.

## Enable (one-time, ~2 minutes)

Discord requires a numeric application ID for rich presence, and the app name
/ artwork on your profile come from that app's registration. Create your own:

1. Open <https://discord.com/developers/applications> → **New Application**.
2. Copy the **Client ID** (top of the app's **General Information** page).
3. Edit `~/.local/share/mcpelauncher/discordrpc.conf`
   (created automatically by `install.sh`):

   ```
   client_id=123456789012345678
   ```

4. Restart the game. Discord desktop must be **running and logged in** (the
   mod talks to Discord through its `/tmp/discord-ipc-N` socket).

## Verify

While the game runs, `~/.local/share/mcpelauncher/discordrpc.state` shows the
current status (`connected`, `state`, `details`, `version`, `error`).
Launcher stderr lines are prefixed `[DiscordRPC]`.

`log=false` in `discordrpc.conf` silences the per-change stderr lines.

## Why it stays working

Everything here is version-independent by construction:

- **No game-internal hooks.** The mod never resolves symbols, signatures or
  vtables inside `libminecraftpe.so`, so a game update cannot break the hook
  target or crash the game. It only uses its own unix-socket client of
  Discord's public IPC protocol.
- **World detection reads /proc, not the game.** While a world is loaded,
  Bedrock keeps the world's files (`.../minecraftWorlds/<id>/`) open in the
  process, which shows up in `/proc/self/fd`. The mod watches for that and
  reads the display name from `level.dat` (NBT) or `levelname.txt`.
- **Fail-closed.** Discord absent → quiet retry with backoff, no logging spam.
  Bad config → one stderr line and the mod stays disabled. Every socket/NBT
  read is bounds-checked; any exception is caught and logged, never thrown
  into the game.
- **Version tag is live.** `versionName` is read from `versions/versions.ini`
  each launch, so the details line always matches the installed build.

## Build

Built like every other mod:

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
./custom/mods/build-mods.sh x86_64        # or arm64-v8a
```

Output: `custom/mods/out/<abi>/libmcpelauncherdiscordrpc.so`.