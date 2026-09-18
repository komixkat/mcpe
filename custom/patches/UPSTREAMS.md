# Mod provenance

Our mod sources are vendored in `custom/mods/` and are built from this repo so
an upstream removal or a dead download URL can never break an install.

| Vendored directory | Upstream project | Local changes |
| --- | --- | --- |
| `mods/fullbright` | https://github.com/CrackedMatter/mcpelauncher-fullbright | none (as-is) |
| `mods/snaplook` | https://github.com/GameParrot/mcpelauncher-snaplook | third-person **back** toggle + first-person-while-zooming |
| `mods/zoom` | https://github.com/GameParrot/mcpelauncher-zoom | FOV-proportional look dampening, `mcpelauncher_zoom_active` export |
| `mods/shulkerpreview` | https://github.com/grimaoss/ShulkerBoxPreview | wildcarded `call` displacement so the signature survives game updates |

The matching `.patch` files in this directory are the local diffs against the
upstream revisions the sources were taken from. They are kept for provenance and
for easy rebasing; the build does not apply them (the sources are already
patched).
