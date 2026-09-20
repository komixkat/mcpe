# Discord Rich Presence (mcpelauncherdiscordrpc)

A no-frills Discord presence for the launcher: while you play, your profile
shows **`Playing Minecraft`** with an elapsed timer, plus a single **Join
komixkat** button that opens your Minecraft profile page:

```
Playing Minecraft        ← details (line 2)
00:12:34 elapsed         ← timestamps
[Join komixkat]          ← button → https://launch.minecraft.net/profile/komixkat
```

Nothing else is detected or scanned: no server/realm names, no dimensions, no
in-game name. The presence is always the same — that is the point. The button
is a Discord link button, so it has no special permissions: it just opens the
profile URL.

## Enable (one-time, ~2 minutes)

Discord requires a numeric application ID for rich presence. The app **name**
that appears at the top of your presence, and any artwork, come from that
app's registration:

1. Open <https://discord.com/developers/applications> → **New Application**.
2. Copy the **Client ID** (top of the app's **General Information** page).
3. Edit `~/.local/share/mcpelauncher/discordrpc.conf`:

   ```
   client_id=123456789012345678
   ```

4. Restart the game. Discord desktop must be **running and logged in** (the
   mod talks to Discord through its `/tmp/discord-ipc-N` socket).

## Your logo (optional)

Discord only shows artwork your *own* app registered: **your application →
Rich Presence → Art Assets → Upload Image**. Give the image a key (e.g.
`mcpe-logo`) and set it in the config:

```
large_image=mcpe-logo
large_text=Minecraft
```

The config is re-read every ~15 seconds, so artwork key edits apply without
restarting the game.

## Verify

Discord shows the presence on your own profile (and your invite/friend list if
you open Discord's game activity display for the client). Launcher stderr
lines are prefixed `[DiscordRPC]`; `log=false` in the config silences the
connection chatter.

## Why it stays working

- **No game-internal hooks.** The mod never resolves symbols, signatures or
  vtables inside `libminecraftpe.so`, so a game update cannot break it or
  crash the game. It only speaks Discord's public IPC protocol over its own
  unix-socket client.
- **Nothing to misdetect.** There is no scanning, no memory reads, no game
  state collection — the presence cannot go wrong or stall.
- **Fail-closed.** Discord absent → quiet retry with backoff, no logging spam.
  Bad config (no numeric `client_id`) → one stderr line and the mod stays
  disabled.

## Build

Built like every other mod:

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
./custom/mods/build-mods.sh x86_64        # or arm64-v8a
```

Output: `custom/mods/out/<abi>/libmcpelauncherdiscordrpc.so`.