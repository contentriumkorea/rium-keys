# CONTENTRIUM Keys 2.0.0-preview.10 verification

Date: **2026-10-11**, Windows 11 x64. This is a manually installed preview, not a production auto-update or universal compatibility approval.

## Changes

- The Windows input-mode button displays white **가 / A** only, with no CK suffix. Korean custom layouts use 가; Latin mappings and disabled/direct input use A. Existing mode-change events notify the shell immediately.
- The independent input-method brand is a transparent white CK. The upgrade corrects the profile icon path that older upgrades left at preview.2, causing the old K to persist. Both aliased registry views are snapshotted before mutation; rollback restores the previous icon and name.
- A single `CONTENTRIUM-Keys-Setup.exe` bundles the verified native installer and Korean physical-test instructions. Supported entry paths are new installation / legacy 1.1.1 migration and native preview.9 → preview.10. Same-version setup checks live registration and installed file integrity before reporting success.
- The native typing/routing implementation is unchanged from preview.9 (which retained preview.8's functional fixes).

## Automated verification

All **25 candidate suites / 1,506 assertions** passed on the final x64/x86 candidates. This includes 152 language-bar/resource checks per architecture: actual DLL resources at nine sizes, transparent white glyphs, distinct Korean/Latin state, immediate notification, private icon ownership, disabled input, preserved Dvorak configuration, custom Hangul and detached service behavior.

All **30 installer-script checks** passed, including exact manifest entries, missing/duplicate/tampered payloads, path containment, gate failure before staging, real value types, aliased registry handles, rollback and preservation of a concurrent foreign icon owner. Registry tests only used a disposable HKCU key.

The renamed ordinary-user physical fixture launch passed. The actual NSIS EXE `/S /PREFLIGHT` exited 0 before installation. Its first trial had exposed an inbox module lookup failure inherited through the launcher; explicitly importing the current shell's built-in Utility module fixed it, and the rebuilt EXE passed.

Review also caught and corrected process execution-policy assumptions, missing live registration checks on a same-version run, non-Hangul layouts incorrectly selecting 가, and misleading interactive preflight completion text. No persistent execution policy is changed. Independent review found no remaining P0/P1/P2 code issue in the final scoped changes.

## Actual installation

The packaged EXE was run in the ordinary user session. After the user approved Windows elevation, preview.9 → preview.10 committed at **07:10:26 KST**. The user completed the displayed physical test:

1. Native EDIT contains exact `한글 ` after composition and Space.
2. Button receives original V keydown and keyup while the fork's Korean profile is still selected (eligible=1, down=1, up=1).
3. Returning to the EDIT yields exact `한글 한글 `; inline preedit observed, Korean profile retained, previous process-local profile restored.

Fixture and installation passed. Independent post-install readback confirmed:

- Windows installed-app name/version: CONTENTRIUM Keys / 2.0.0-preview.10.
- All **13 installed manifest payloads** match their hashes.
- Both COM views reference the correct versioned x64/x86 DLLs.
- Both profile views use CONTENTRIUM Keys, preview.10's x64 DLL as `IconFile`, and `IconIndex=-100`.
- Registered, enabled, active and default native profile; six required categories.
- Original Microsoft IME fallback remains in the saved recovery state.
- The final EXE's same-version `/S /PREFLIGHT` exits 0 and reports a verified no-op, without a second registration.

Local transaction: `20261011-070928-06801c0f-8f31-484e-aefe-d6598af9bee6`. Logs remain local under the documented recovery/setup directories; raw machine/account metadata is not published.

## Artifact identity

| File | SHA-256 |
| --- | --- |
| CONTENTRIUM-Keys-Setup.exe | `7a28128b870abdcfadd354dcaaccaad1d41c3b63d789e1166cc463a097a35890` |
| x64/RiumKeysInput.dll | `9c1ce589a9345e04d8dfcc7a726f533182fdda79984623babd48067803c9aaf8` |
| x86/RiumKeysInput.dll | `0821c95c4a8e63e2cd5be9f67d72f85a342b8f2c25834eda5a21ac173dcd959f` |

## Limits

This validates the packaged upgrade on this PC and the isolated physical EDIT/button transition, plus the automated icon behavior. The Windows shell may retain its previous brand image until sign-out; the entire taskbar's rendered state was not captured by the window-scoped test tools. Running apps can retain an earlier DLL until restarted.

The Premiere and Studio One physical results in [input-owner-validation.md](input-owner-validation.md) are from preview.8 and are not presented as new preview.10 app tests. After Effects shortcut/return-to-text, other builds, fast typing throughout all applications, clean-machine installation/uninstallation coverage, code signing and native unattended updates remain incomplete. The old stable update feed is unchanged.
