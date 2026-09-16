#!/usr/bin/env bash
# Install the fixed mcpelauncher (core + UI) from komixkat/mcpe GitHub Releases.
# Usage: curl -sL https://raw.githubusercontent.com/komixkat/mcpe/qt6/install.sh | bash

set -euo pipefail

REPO="komixkat/mcpe"

if ! command -v jq >/dev/null 2>&1; then
  echo "jq is required. Install it first: sudo pacman -S jq"
  exit 1
fi

echo "Resolving latest release on GitHub..."
API="https://api.github.com/repos/$REPO/releases/latest"
PAGE=$(curl -fsSL "$API") || { echo "Could not reach GitHub API"; exit 1; }

TAG=$(printf '%s' "$PAGE" | jq -r '.tag_name')
URLS=$(printf '%s' "$PAGE" | jq -r '.assets[].browser_download_url')

if [ -z "$URLS" ] || [ "$URLS" = "null" ]; then
  echo "No packages found in release $TAG. Run the build workflow first."
  exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

echo "Downloading packages from $TAG ..."
for url in $URLS; do
  curl -fsSLO "$url"
done

PGKS=(*.pkg.tar.zst)

# Packages renamed to a -mcpe suffix so AUR helpers stop offering "updates".
# Pre-remove any old-named packages still installed, otherwise pacman hits an
# unresolvable conflict (it prompts [y/N] to remove them, and --noconfirm
# answers no, which aborts the transaction).
REMOVE=()
if pacman -Qq | grep -qx 'mcpelauncher-linux-git'; then REMOVE+=(mcpelauncher-linux-git); fi
if pacman -Qq | grep -qx 'mcpelauncher-ui'; then REMOVE+=(mcpelauncher-ui); fi
if [[ ${#REMOVE[@]} -gt 0 ]]; then
  echo "Removing old-named packages before installing: ${REMOVE[*]}"
  sudo pacman -R --noconfirm "${REMOVE[@]}"
fi

echo "Installing: ${PGKS[*]}"
# Run pacman with --noconfirm so it never reads from stdin.
# When this script is piped (curl ... | bash), any child that reads stdin
# would consume the remaining script bytes from the same pipe and silently
# truncate the install after this step (the mod deploy below). Directing
# sudo's prompt also breaks sudo on real terminals, so --noconfirm is the
# clean fix: no child touches stdin at all, and sudo reads the password
# from the controlling tty as normal.
sudo pacman -U --needed --noconfirm "${PGKS[@]}"

echo "Deploying the updates mod..."
MODDIR="$HOME/.local/share/mcpelauncher/mods"
install -d "$MODDIR/patches/v1.26.0.2/x86_64"
base="https://raw.githubusercontent.com/$REPO/qt6/mods"
curl -fsSL -o "$MODDIR/libmcpelauncher-updates.so" "$base/libmcpelauncher-updates.so"
curl -fsSL -o "$MODDIR/patches/libPlayFabMultiplayer.so" "$base/patches/libPlayFabMultiplayer.so"
curl -fsSL -o "$MODDIR/patches/v1.26.0.2/x86_64/libmaesdk.so" "$base/patches/v1.26.0.2/x86_64/libmaesdk.so"
chmod +x "$MODDIR/libmcpelauncher-updates.so"

echo "Verifying mod deployment..."
if [ -s "$MODDIR/libmcpelauncher-updates.so" ] && [ -s "$MODDIR/patches/libPlayFabMultiplayer.so" ] && [ -s "$MODDIR/patches/v1.26.0.2/x86_64/libmaesdk.so" ]; then
  echo "  OK: updates mod -> $MODDIR"
  find "$MODDIR" -type f -printf "      %p (%s bytes)\n"
else
  echo "  FAILED: mod files missing from $MODDIR" >&2
  exit 1
fi

echo
echo "Done. Launch the launcher from the application menu (mcpelauncher-ui-qt) or run: mcpelauncher-ui-qt"
echo "On Wayland look-alikes, prefix with: EGL_PLATFORM=x11 SDL_VIDEODRIVER=x11 mcpelauncher-ui-qt"