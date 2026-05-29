# Sightline — Font Assets

Place the following files in this directory:

| File | Source | Licence |
|------|--------|---------|
| `fa-solid-900.ttf` | [Font Awesome 6 Free](https://github.com/FortAwesome/Font-Awesome/releases) — download `fontawesome-free-6.x.x-desktop.zip`, take `otfs/Font Awesome 6 Free-Solid-900.otf` and rename/convert to `fa-solid-900.ttf`, **or** grab the `.ttf` directly from the release `webfonts/` folder. | [OFL-1.1](https://scripts.sil.org/OFL) — free for commercial use |

Sightline will load this file at runtime and merge it into the UI font atlas.
If the file is not found, the app falls back to text labels gracefully.
