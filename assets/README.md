# CONTENTRIUM Keys icons

`contentrium-keys.png` is the transparent white **CK** brand source, generated with the built-in image tool on 2026-10-11. It replaces the mint source retained in Git history.

Final edit prompt: keep the CK arrangement and rounded geometry; use only two solid white letter silhouettes, a transparent canvas and clean edges, without extra text, a background tile, shadow or gradient. The selected result is encoded without recoloring or modifying its alpha by `scripts/build-brand-icon.ps1`.

`input-mode-korean.png` and `input-mode-english.png` are deterministic UI text glyphs, **가** and **A**, rendered by `scripts/build-mode-icons.ps1` in Malgun Gothic and Segoe UI. They are white on transparent backgrounds, with no CK suffix. This script uses fonts supplied by Windows, not a redistributed font file.

Each ICO includes 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel PNG frames. Native resources: **100 = CK**, **101 = 가**, **102 = A**. The profile registration points at 100. The language-bar button uses 101 or 102 from the active layout; switching notifies the shell. Windows owns and destroys each private HICON returned to it.

The Windows input indicator hosts a fixed brand icon and a separate changing mode icon. See [Microsoft IME requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements). The existing internal name `src/rium-keys.ico` is retained for resource and installer compatibility.
