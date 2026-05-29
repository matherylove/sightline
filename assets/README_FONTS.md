# Required Font Files

Sightline uses **Font Awesome 6 Free (Solid)** for UI icons.

## How to obtain

1. Download Font Awesome 6 Free from https://fontawesome.com/download  
   (choose "Free for Desktop" / "Font Awesome Free x.x.x Desktop")
2. Inside the zip, find `otfs/Font Awesome 6 Free-Solid-900.otf`  
   **or** `webfonts/fa-solid-900.ttf`
3. Copy **`fa-solid-900.ttf`** next to `Sightline.exe`  
   (or inside an `assets/` subfolder next to the executable).

If the file is missing, Sightline falls back to text-only labels automatically.

## Main UI font

Sightline loads the first available system font from this list:

- `C:/Windows/Fonts/verdana.ttf`
- `C:/Windows/Fonts/tahoma.ttf`
- `C:/Windows/Fonts/arial.ttf`

No action needed for the main font — it is always available on Windows.
