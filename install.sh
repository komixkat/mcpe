#!/usr/bin/env bash
# Install the fixed mcpelauncher (core + UI) and the curated, fixed mods from
# the komixkat/mcpe GitHub Releases.
#
#   curl -sL https://raw.githubusercontent.com/komixkat/mcpe/qt6/install.sh | bash
#
# What this does, in order:
#   1. installs / refreshes the launcher packages with pacman,
#   2. deploys the runtime "updates" mod (game-version compatibility patches),
#   3. deploys our fixed mods for the host architecture:
#        libfullbright.so, libmcpelauncherzoom.so,
#        libmcpelaunchersnaplook.so, libshulke.so,
#        libmcpelauncherdiscordrpc.so (Discord Rich Presence)
#   4. installs the bundled custom skin pack and enables custom skins,
#   5. seeds a default discordrpc.conf so Discord presence can be enabled.
#
# The mods live in user data, so launcher/game updates never touch them, and
# every run re-asserts them from this repository. Re-run this script at any
# time to repair or update the install.
set -euo pipefail

REPO="komixkat/mcpe"
BRANCH="qt6"
RAW="https://raw.githubusercontent.com/$REPO/$BRANCH"

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
DATA_DIR="$DATA_HOME/mcpelauncher"
MODDIR="$DATA_DIR/mods"
SKINROOT="$DATA_DIR/games/com.mojang/skin_packs"
OPTIONS="$DATA_DIR/games/com.mojang/minecraftpe/options.txt"

for tool in jq curl tar; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "ERROR: '$tool' is required. Install it first (e.g. sudo pacman -S $tool)." >&2
    exit 1
  fi
done

case "$(uname -m)" in
  x86_64|amd64)  ABI="x86_64" ;;
  aarch64|arm64) ABI="arm64-v8a" ;;
  *) echo "ERROR: unsupported architecture '$(uname -m)'." >&2; exit 1 ;;
esac

echo "Resolving latest release of $REPO ..."
PAGE=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest") || {
  echo "ERROR: could not reach the GitHub API." >&2; exit 1; }

TAG=$(printf '%s' "$PAGE" | jq -r '.tag_name')
URLS=$(printf '%s' "$PAGE" | jq -r '.assets[].browser_download_url')
if [ -z "$URLS" ] || [ "$URLS" = "null" ]; then
  echo "ERROR: release $TAG has no assets. Run the build workflow first." >&2
  exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

echo "Downloading release assets from $TAG ..."
for url in $URLS; do
  curl -fsSLO "$url"
done

if [ -f SHA256SUMS.txt ]; then
  echo "Verifying checksums ..."
  # The checksum file may record absolute build paths (older releases) or
  # relative paths. Normalize every entry to its basename so the check works
  # against this flat download directory.
  sed -E 's|^([0-9a-f]{64}) [ *].*/|\1  |' SHA256SUMS.txt > SHA256SUMS.local.txt
  if ! sha256sum -c --quiet SHA256SUMS.local.txt; then
    echo "ERROR: checksum verification failed." >&2
    exit 1
  fi
else
  echo "WARNING: release has no SHA256SUMS.txt, skipping checksum verification." >&2
fi

PKGS=( *.pkg.tar.zst )

# Packages are renamed with a -mcpe suffix so AUR helpers stop offering
# "updates". Pre-remove any old-named packages still installed, otherwise
# pacman hits an unresolvable conflict (it prompts [y/N] to remove them and
# --noconfirm answers no, which aborts the whole transaction).
REMOVE=()
if pacman -Qq | grep -qx 'mcpelauncher-linux-git'; then REMOVE+=(mcpelauncher-linux-git); fi
if pacman -Qq | grep -qx 'mcpelauncher-ui'; then REMOVE+=(mcpelauncher-ui); fi
if [ "${#REMOVE[@]}" -gt 0 ]; then
  echo "Removing old-named packages before installing: ${REMOVE[*]}"
  sudo pacman -R --noconfirm "${REMOVE[@]}"
fi

echo "Installing launcher packages: ${PKGS[*]}"
# --noconfirm keeps every child off stdin. When this script is piped
# (curl ... | bash) a child that reads stdin would consume the remaining
# script bytes off the same pipe and silently truncate the install.
sudo pacman -U --needed --noconfirm "${PKGS[@]}"

# ---------------------------------------------------------------------------
# Launcher UI metadata: the launcher's "Installed Mods" tab lists
# mods/<name>/<ver>/<arch>/mod.json. Without these files the UI shows bare
# folder names (like "patches") with no description or link. These files are
# display-only: the game client loads *.so files and ignores everything else.
# ---------------------------------------------------------------------------
write_mod_meta() { # name version arch json
  local name="$1" version="$2" arch="$3" json="$4"
  local dir="$MODDIR/$name/$version/$arch"
  install -d "$dir"
  printf '%s\n' "$json" > "$dir/mod.json"
  echo "  OK: launcher metadata $dir/mod.json"
}

# ---------------------------------------------------------------------------
# Runtime "updates" mod: the PairIP / PlayFab compatibility layer.
# ---------------------------------------------------------------------------
echo "Deploying the runtime updates mod ..."
install -d "$MODDIR/patches/v1.26.0.2/x86_64"
curl -fsSL -o "$MODDIR/libmcpelauncher-updates.so" "$RAW/mods/libmcpelauncher-updates.so"
curl -fsSL -o "$MODDIR/patches/libPlayFabMultiplayer.so" "$RAW/mods/patches/libPlayFabMultiplayer.so"
curl -fsSL -o "$MODDIR/patches/v1.26.0.2/x86_64/libmaesdk.so" "$RAW/mods/patches/v1.26.0.2/x86_64/libmaesdk.so"
chmod +x "$MODDIR/libmcpelauncher-updates.so"

if [ ! -s "$MODDIR/libmcpelauncher-updates.so" ] \
   || [ ! -s "$MODDIR/patches/libPlayFabMultiplayer.so" ] \
   || [ ! -s "$MODDIR/patches/v1.26.0.2/x86_64/libmaesdk.so" ]; then
  echo "ERROR: updates mod missing from $MODDIR" >&2
  exit 1
fi
echo "  OK: updates mod -> $MODDIR"
write_mod_meta "patches" "v1.26.0.2" "x86_64" '{"metadata":{"name":"MCPE Runtime Patches","description":"PlayFab/PairIP compatibility layer that lets recent Minecraft versions (incl. 1.26.x) run on the Linux launcher. Required for online play; leave it installed.","url":"https://github.com/komixkat/mcpe","image":""}}'

# ---------------------------------------------------------------------------
# Our fixed mods, built for this architecture from this repository.
# ---------------------------------------------------------------------------
BUNDLE="mcpelauncher-mods-$ABI.tar.gz"
if [ -f "$BUNDLE" ]; then
  echo "Deploying fixed mods for $ABI ..."
  install -d "$MODDIR"
  BACKUP="$DATA_DIR/mod-backups/$(date +%Y%m%d-%H%M%S)"
  mkdir -p "$BACKUP"
  backed_up=0
  while IFS= read -r entry; do
    if [ -f "$MODDIR/$entry" ]; then
      cp -f "$MODDIR/$entry" "$BACKUP/"
      backed_up=1
    fi
  done < <(tar -tzf "$BUNDLE")
  tar -xzf "$BUNDLE" -C "$MODDIR"
  [ "$backed_up" = 1 ] && echo "  (previous mods backed up to $BACKUP)"
  echo "  OK: mods -> $MODDIR"
  find "$MODDIR" -maxdepth 1 -type f -name 'lib*.so' -printf '      %f\n' | sort
  write_mod_meta "fullbright" "$TAG" "$ABI" '{"metadata":{"name":"Fullbright","description":"Raises the viewer brightness so dark areas of the world are clearly visible.","url":"https://github.com/komixkat/mcpe","image":""}}'
  write_mod_meta "snaplook" "$TAG" "$ABI" '{"metadata":{"name":"Snaplook","description":"Hold a key to snap into an over-the-shoulder view behind your character.","url":"https://github.com/komixkat/mcpe","image":""}}'
  write_mod_meta "zoom" "$TAG" "$ABI" '{"metadata":{"name":"Zoom","description":"Zoom in while playing (hold a key; sensitivity adjustable in the mod config).","url":"https://github.com/komixkat/mcpe","image":""}}'
  write_mod_meta "shulkerpreview" "$TAG" "$ABI" '{"metadata":{"name":"Shulker Preview","description":"Preview the contents of shulker boxes without opening them.","url":"https://github.com/komixkat/mcpe","image":""}}'
  write_mod_meta "discordrpc" "$TAG" "$ABI" '{"metadata":{"name":"Discord Rich Presence","description":"Shows \"Playing Minecraft\" on your Discord profile with an elapsed timer and up to two configurable link buttons (Join, YouTube, Twitch, etc.). Set client_id in discordrpc.conf to enable. Auto-reconnects if Discord starts after the game.","url":"https://github.com/komixkat/mcpe/blob/qt6/custom/mods/discordrpc/README.md","image":""}}'
else
  echo "WARNING: $TAG has no $BUNDLE; skipping fixed mods." >&2
fi

# ---------------------------------------------------------------------------
# Custom skin pack + enable custom (untrusted) skins.
# ---------------------------------------------------------------------------
if [ -f mcpelauncher-skinpack.tar.gz ]; then
  echo "Installing custom skin pack ..."
  install -d "$SKINROOT"
  tar -xzf mcpelauncher-skinpack.tar.gz -C "$SKINROOT"
  echo "  OK: skin pack -> $SKINROOT"
fi

if [ -f "$OPTIONS" ]; then
  if grep -q '^only_show_trusted_skins:' "$OPTIONS"; then
    sed -i 's/^only_show_trusted_skins:.*/only_show_trusted_skins:0/' "$OPTIONS"
  else
    printf 'only_show_trusted_skins:0\n' >> "$OPTIONS"
  fi
  echo "  OK: custom skins enabled (only_show_trusted_skins=0)"
else
  echo "  Note: options.txt does not exist yet; the launcher forces custom"
  echo "        skins on at first launch, so no action is needed."
fi

# ---------------------------------------------------------------------------
# Default-skin override: the game bakes "alex" and "steve" as the default
# skins. Replace both with the custom skin texture so the player's own model
# shows it permanently, regardless of which default skin is active. Originals
# are kept as *.png.bak. The versions dir persists across boots, so this only
# needs to be re-asserted after a game-version update (re-run install.sh).
# ---------------------------------------------------------------------------
if [ -f "$SKINROOT/Nekomix/nekomix.png" ]; then
  VANILLA_STEVE="$(find "$DATA_DIR/versions" -type f \
    -path '*/skin_packs/vanilla/steve.png' 2>/dev/null | sort | tail -1)"
  if [ -n "$VANILLA_STEVE" ]; then
    VANILLA_DIR="$(dirname "$VANILLA_STEVE")"
    cp -f "$VANILLA_DIR/steve.png" "$VANILLA_DIR/steve.png.bak"
    cp -f "$VANILLA_DIR/alex.png" "$VANILLA_DIR/alex.png.bak"
    cp -f "$SKINROOT/Nekomix/nekomix.png" "$VANILLA_DIR/steve.png"
    cp -f "$SKINROOT/Nekomix/nekomix.png" "$VANILLA_DIR/alex.png"
    echo "  OK: default skins (alex/steve) overridden with the custom skin"
  else
    echo "  Note: game assets not found yet; default-skin override will be"
    echo "        applied on the next install.sh run after the game exists."
  fi
fi

# ---------------------------------------------------------------------------
# Discord Rich Presence: seed a default config. The mod (shipped in the mods
# bundle above) is disabled until the user pastes a Discord application Client
# ID, so never overwrite a config that already exists.
# ---------------------------------------------------------------------------
if [ ! -f "$DATA_DIR/discordrpc.conf" ]; then
  cat > "$DATA_DIR/discordrpc.conf" <<'EOF'
# Discord Rich Presence for the launcher/game (custom/mods/discordrpc).
# The presence is always "Playing Minecraft" (with an elapsed timer) plus
# up to two configurable buttons.
# Config is re-read every ~15 seconds, so edits apply without restarting.
#
# 1. Create a Discord application: https://discord.com/developers/applications
# 2. Copy its Client ID into client_id below.
# 3. Restart the game with Discord running.
#    The mod auto-reconnects if Discord starts after the game or if the
#    connection drops (quiet retry with exponential backoff).
client_id=

# Optional artwork: upload images in the Developer Portal (Rich Presence ->
# Art Assets), then set the keys here. Empty uses Discord's default icon.
large_image=
large_text=

# Quiet the "[DiscordRPC]" connection lines on the launcher console.
log=true

# Button 1 (primary join button) — override the default "Join komixkat":
# button1_label=Your Button Name
# button1_url=https://your-link.com

# Button 2 (optional multipurpose button) — disabled by default:
# button2_enabled=true
# button2_label=YouTube
# button2_url=https://youtube.com/@yourchannel
#
# Examples:
#   button2_enabled=true
#   button2_label=Twitch
#   button2_url=https://twitch.tv/yourname
#
#   button2_enabled=true
#   button2_label=Website
#   button2_url=https://yourname.com
#
#   button2_enabled=true
#   button2_label=Discord
#   button2_url=https://discord.gg/yourinvite
EOF
  echo "  OK: created discordrpc.conf (set client_id to enable presence)"
fi

# ---------------------------------------------------------------------------
# Ensure Bedrock's pack-state files exist. If these are missing, the game
# treats them as a failed resource-load every session and shows the
# "Global Resources Reset - Resources failed to load previously" dialog at
# every launch. They must be valid JSON arrays; only create when absent so a
# user's real pack selection is never overwritten.
# ---------------------------------------------------------------------------
PACKDIR="$DATA_DIR/games/com.mojang"
for PKG in global_resource_packs.json resource_packs.json known_resource_packs.json; do
  if [ ! -f "$PACKDIR/$PKG" ]; then
    printf '[]\n' > "$PACKDIR/$PKG"
    echo "  OK: created $PKG (empty pack list)"
  fi
done
unset PACKDIR PKG

echo
echo "Done."
echo "  launcher:  mcpelauncher-ui-qt"
echo "  mods:      $MODDIR"
echo
echo "On Wayland look-alikes, prefix with: EGL_PLATFORM=x11 SDL_VIDEODRIVER=x11"
