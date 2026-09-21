# Discord Rich Presence (mcpelauncherdiscordrpc)

A no-frills Discord presence for the launcher: while you play, your profile
shows **`Playing Minecraft`** with an elapsed timer, plus up to two custom
buttons:

```
Playing Minecraft        ← details (line 2)
00:12:34 elapsed         ← timestamps
[Join komixkat]          ← button 1 (configurable)
[YouTube]                ← button 2 (optional, toggleable)
```

Nothing else is detected or scanned: no server/realm names, no dimensions, no
in-game name. The presence is always the same (that is the point). The buttons
are Discord link buttons, so they have no special permissions: they just open
the URLs you configure.

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
   The mod will **auto-reconnect** if Discord starts after the game, or if
   the connection drops for any reason.

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

## Buttons (fully configurable)

All buttons are configured in `~/.local/share/mcpelauncher/discordrpc.conf`.
Changes apply live without restarting.

### Button 1 (primary join button)

Defaults to "Join komixkat" → your Minecraft profile. Override with:

```
button1_label=Your Button Name
button1_url=https://your-link.com
```

**Example - custom join button:**
```
button1_label=Join My Server
button1_url=https://discord.gg/myserver
```

### Button 2 (optional multipurpose button)

Disabled by default. Enable and configure with:

```
button2_enabled=true
button2_label=YouTube
button2_url=https://youtube.com/@yourchannel
```

**Examples:**
```
# Twitch link
button2_enabled=true
button2_label=Twitch
button2_url=https://twitch.tv/yourname

# Personal website
button2_enabled=true
button2_label=Website
button2_url=https://yourname.com

# Discord server invite
button2_enabled=true
button2_label=Discord
button2_url=https://discord.gg/yourinvite
```

## Full config example

```
client_id=1550483074905546782
log=true
large_image=mcpe-logo
large_text=Minecraft Bedrock

# Primary button (default: "Join komixkat" → Minecraft profile)
button1_label=Join My World
button1_url=https://discord.gg/myworld

# Optional second button (disabled by default)
button2_enabled=true
button2_label=YouTube
button2_url=https://youtube.com/@mychannel
```

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
  state collection; the presence cannot go wrong or stall.
- **Fail-closed.** Discord absent → quiet retry with backoff, no logging spam.
  Bad config (no numeric `client_id`) → one stderr line and the mod stays
  disabled.
- **Auto-reconnect.** If Discord starts after the game, or the connection
  drops, the mod quietly reconnects with exponential backoff (2s → 4s → 8s
  → max 30s).

## Build

Built like every other mod:

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
./custom/mods/build-mods.sh x86_64        # or arm64-v8a
```

Output: `custom/mods/out/<abi>/libmcpelauncherdiscordrpc.so`.
