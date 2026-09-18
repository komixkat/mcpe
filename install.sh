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
#        libmcpelaunchersnaplook.so, libshulke.so
#   4. installs the bundled custom skin pack and enables custom skins.
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
  sha256sum -c --quiet SHA256SUMS.txt
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

echo
echo "Done."
echo "  launcher:  mcpelauncher-ui-qt"
echo "  mods:      $MODDIR"
echo
echo "On Wayland look-alikes, prefix with: EGL_PLATFORM=x11 SDL_VIDEODRIVER=x11"
