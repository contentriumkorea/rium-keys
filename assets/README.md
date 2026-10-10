# CONTENTRIUM Keys icon

`contentrium-keys.png` is the first-party transparent RGBA source for the
notification-area and input-method brand icon. It was generated with the built-in
image generation tool on 2026-10-11 for this project.

Design prompt: a minimal mint-teal, rounded geometric uppercase CK monogram,
with an open C, separate K, strong strokes, transparent padding, no background
tile, shadow, wordmark or keyboard illustration. Intended for small Windows
notification-area sizes.

Run `scripts/build-brand-icon.ps1` to encode the PNG as `src/rium-keys.ico` with
16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel alpha-preserving PNG frames. The
internal ICO filename is retained for compatibility. Native resource 100 and the
legacy executable use this same icon. The native language-bar button loads its
own resource handle at the current small-icon size; Windows owns and destroys
that handle. Input mode remains in the tooltip, and the existing right-click
controls remain available.
