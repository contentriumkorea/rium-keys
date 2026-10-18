# CONTENTRIUM Keys icons

`contentrium-keys.png` is the transparent white **CK** brand source, generated with the built-in image tool on 2026-10-11. It replaces the mint source retained in Git history.

Final edit prompt: keep the CK arrangement and rounded geometry; use only two solid white letter silhouettes, a transparent canvas and clean edges, without extra text, a background tile, shadow or gradient. The selected result is encoded without recoloring or modifying its alpha by `scripts/build-brand-icon.ps1`.

`input-mode-korean.png` and `input-mode-english.png` are deterministic UI text glyphs, **가** and **A**, rendered by `scripts/build-mode-icons.ps1` in Malgun Gothic and Segoe UI. They are white on transparent backgrounds, with no CK suffix. This script uses fonts supplied by Windows, not a redistributed font file.

Each ICO includes 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel PNG frames. Native resources: **100 = CK**, **101 = 가**, **102 = A**. The profile registration points at 100. The language-bar button uses 101 or 102 from the active layout; switching notifies the shell. Windows owns and destroys each private HICON returned to it.

The Windows input indicator hosts a fixed brand icon and a separate changing mode icon. See [Microsoft IME requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements). The existing internal name `src/rium-keys.ico` is retained for resource and installer compatibility.

## GitHub presentation — 2026-10-18

`contentrium-keys-cover.png` is a project-specific 1600×900 cover. The keycaps are a feature concept, not a screenshot. All lettering is rendered directly with Windows Malgun Gothic; no font files are distributed and no generated-image text is used.

`contentrium-keys-indicators.png` displays the three shipped PNG icons on an illustrative background. Original icon files, transparency and executable resources are unchanged. The README identifies this layout as an illustration, not a Windows screenshot.

`download-windows.svg` and `contentrium-home.svg` are static, accessible link buttons with text, no scripts or embedded external content. They link separately to the installer and the company website. These presentation assets are not part of the 2.0.2 installer payload.
