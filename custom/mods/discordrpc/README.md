# Discord Rich Presence (mcpelauncherdiscordrpc)

Shows your Minecraft Bedrock session on your Discord profile while you play,
in the same window the launcher's process covers:

| where you are                                   | rich presence state           |
| ----------------------------------------------- | ----------------------------- |
| game starting (first ~10 seconds)               | `In the launcher...`          |
| main menu / loading screen                      | `In the menus`                |
| loaded into a survival world                    | `In a survival world: <name>` |
| loaded into a creative world                    | `In a creative world: <name>` |
| loaded into an adventure world                  | `In an adventure world: <name>` |
| spectating                                       | `Spectating: <name>`          |
| on an external server (auto-detected)            | `On a server`                 |
| on a Realm (auto-detected)                       | `On a Realm`                  |
| on a server or Realm (detected, mode unknown)    | `On a server or Realm`        |

`details` is just **`Playing Minecraft`** (no version number unless you set
`show_version=true`). The game mode comes from the world's `level.dat`
`GameType` tag; the world name from `levelname.txt` (or `LevelName`).

When the current dimension can be read from the world database, the state
becomes **`In the Overworld`** / **`In the Nether`** / **`In the End`** and the
second line shows `Playing <world name>`. Otherwise you see the mode+name rows
above.

If you are in a world but Discord still shows "In the menus", the world
detection only sees *singleplayer* worlds whose files the process holds open —
join the world in-game for a moment and the state updates within a second.

## Servers and Realms

Server/realm sessions are **auto-detected**: the game streams chunks into
`minecraftpe/blob_cache/` (a leveldb) for the whole online session and keeps
its write-ahead log open, whereas singleplayer keeps the world's own files
open instead. The mod watches `/proc/self/fd` for exactly one of the two — no
config needed.

Which label is shown depends on the `multiplayer` and `server_name` settings
in `discordrpc.conf`:

```ini
# optional, exact wording (overrides auto-detection):
server_name=CubeCraft   # shows "On CubeCraft" while on that server
multiplayer=realm       # shows "On a Realm"            (default for realms)
multiplayer=server      # shows "On a server"           (default for servers)
```

### It reads the actual server name from the game itself

The launcher's libc shim records every hostname the game resolves into
`~/.local/share/mcpelauncher/resolved_hosts.log`. The mod uses the host the
game is *connected* to as a memory anchor, then — because the mod runs inside
the game process — reads the display name the game keeps next to it in its own
memory. This works on **featured servers, custom IP servers and Realms alike**,
with no alias or config (e.g. joining SoulSteel reads `SoulSteel`, and joining
a Realm reads the Realm's actual title):

- `On <Server Name>` — name read from the game's memory (anchored on the
  host it is connected to), with the featured-server catalog
  (`ContentCache/ThirdPartyServer`) as fallback;
- `On <Realm Title>` — same read anchored on the `pocket.realms.*` host, with
  `On a Realm` as fallback;
- `On a server or Realm` — when the memory read finds nothing trustworthy.
  The mod never *guesses* a name it cannot observe.

The candidates it considered each refresh are in `discordrpc.debug`
(`server_scan=` with score+name, `server_anchor=` the anchor host, and raw
`scan_cands=`), so the pick rule can be calibrated per game version.

`server_name=` / `multiplayer=` above always win over auto-detection.

### Naming your own servers (`server_alias`)

Auto-detection reads a real name from memory in almost all cases now. The
alias table remains as an explicit override when you want a *different*
label than the game's own string, or when the memory read can't find one
(repeatable, checked with priority over every other source, live-reloaded):

```ini
# server_alias=<What you want shown>|<host the game resolves>
server_alias=My Private Server|play.myserver.net
server_alias=SoulSteel|soulsteel.cubecraft.net
```

What the game actually resolves is visible in
`~/.local/share/mcpelauncher/resolved_hosts.log` (also mirrored as
`resolved=` in `discordrpc.debug`) — use the exact hostname from there.
A wildcard host works too: `server_alias=My Network|*.myserver.net`.

`discordrpc.conf` is **re-read live** (~every 15 seconds), so changing
`server_name`, `multiplayer`, `dimension` or the artwork keys applies without
restarting the game.

## Dimensions

Singleplayer worlds detect their current dimension automatically — Bedrock
keeps a marker key named after the active dimension (`Overworld`, `Nether` or
`TheEnd`) in the world's leveldb, and only the active one is present. The mod
reads it from the world's newest log file, so the state updates when you step
through a portal.

Server and Realm sessions stream the world from the network and keep **no
local world**, so the dimension usually can't be read there. As a best-effort,
the mod also scans the live session's memory for the active dimension marker
and logs what it finds as `server_dim=` in `discordrpc.debug` (so it can be
calibrated per version); many builds expose it, some don't. When you want a
fixed label while online regardless, set it explicitly (also live-reloadable):

```ini
dimension=Nether    # shows "In the Nether" while online
```

The `discordrpc.state` file always reports what the mod believes
(`dimension=` — the auto-detected one for worlds, `(auto)` when none, or the
override while online).

Note: menus and singleplayer never touch `blob_cache/`, so the detection
cannot misfire there. While you play a server or realm, the mod also writes
`~/.local/share/mcpelauncher/discordrpc.debug` every 5s (open files + live
sockets + blob-cache state + the `resolved=` hostnames it saw and the
`server_auto=`/`catalog=` detection output) so the label can stay correct
across game updates.

## Enable (one-time, ~2 minutes)

Discord requires a numeric application ID for rich presence, and the app name
/ artwork on your profile come from that app's registration. Create your own:

1. Open <https://discord.com/developers/applications> → **New Application**.
   The application **name** is what appears at the top of your rich presence.
2. Copy the **Client ID** (top of the app's **General Information** page).
3. Edit `~/.local/share/mcpelauncher/discordrpc.conf`
   (created automatically by `install.sh`):

   ```
   client_id=123456789012345678
   ```

4. Restart the game. Discord desktop must be **running and logged in** (the
   mod talks to Discord through its `/tmp/discord-ipc-N` socket).

## Your logo / artwork

Discord only shows artwork that your *own* app registered, so upload it in the
Developer Portal: **your application → Rich Presence → Art Assets → Upload
Image**. Give each image a memorable key (e.g. `mcpe-logo`), then put the keys
in the config; the artwork replaces the generic placeholder on the second line:

```
large_image=mcpe-logo
large_text=Minecraft
small_image=mcpe-small
small_text=Server
```

`small_*` is optional and draws on top of the large image.

## Join Game button

While you are **inside a world or Realm**, rich presence includes a party and
a join secret, so friends who can see your profile get a **Join** button.
**Press `F8` in-game to toggle join on/off** at any time (the choice is saved
to `join_enabled` in `discordrpc.conf` and survives restarts):

- When a friend clicks it, a join request lands in
  `~/.local/share/mcpelauncher/discordrpc.join` with their username/id, and a
  `[DiscordRPC] join request:` line is logged. The party counter
  (`1/10` by default) ticks up as requests arrive.
- Friends who run this same launcher + mod with the same application ID also
  receive the join secret from Discord. If you set `join_address` in the
  config, that address is the secret — so they see exactly what to connect to
  in their `discordrpc.join`. For a REALM/LAN world, `join_address` should be
  your reachable address: a VPN LAN IP (e.g. Radmin/Hamachi, `1.2.3.4:19132`),
  or a public domain/IP with UDP port `19132` forwarded.
- Without `join_address`, the mod still tells you *who* wants in, and the
  joiner's copy shows `ask the host for their LAN/VPN address`.

This is unofficial: the mod opens the Discord link for you with an address you
configure, it does not inject into the game's networking, so it cannot break
on game updates and cannot silently join you to strangers.

## Your in-game name (IGN)

Your Minecraft account name is read from the game's memory (anchored on the
XUID in `catalog_info.json`) and shown on the second line of the presence, e.g.
`Playing Minecraft · RespectfulGamer` — no manual entry. Disable it with
`show_ign=false` in `discordrpc.conf` (false is rare: the game must be logged
in and the name must be readable; it is logged as `ign=` in `discordrpc.debug`).

## Verify

While the game runs, `~/.local/share/mcpelauncher/discordrpc.state` shows the
current status (`connected`, `state`, `details`, `dimension`, `world`,
`multiplayer`, `party`, `version`, `error`).
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
  reads the display name and game mode from `level.dat` (NBT) or
  `levelname.txt`.
- **State changes are pushed instantly.** Every change to the state, details,
  party or join secret is sent to Discord immediately — no waiting on a
  refresh timer — so switching menu → world → menu updates your profile
  within about a second.
- **Fail-closed.** Discord absent → quiet retry with backoff, no logging spam.
  Bad config → one stderr line and the mod stays disabled. Every socket/NBT
  read is bounds-checked; any exception is caught and logged, never thrown
  into the game.

## Build

Built like every other mod:

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
./custom/mods/build-mods.sh x86_64        # or arm64-v8a
```

Output: `custom/mods/out/<abi>/libmcpelauncherdiscordrpc.so`.