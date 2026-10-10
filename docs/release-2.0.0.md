# CONTENTRIUM Keys 2.0.0 verification

Date: 2026-10-11. Environment: Windows 11 x64, ordinary user session.

## Release changes

- Product menu, installer, DLL metadata and current installation instructions use CONTENTRIUM Keys 2.0.0 with no development-stage label. Mode icons remain white 가 / A and the separate white CK brand.
- Setup performs automatic installation checks instead of requiring the user to type Korean, press V, then type Korean again. The physical fixture remains a development tool and is not distributed in the installer.
- Both x64 and x86 controllers verify the enabled profile and six categories, the registered COM DLL path/threading model, creation of the actual COM service, required input/composition interfaces, and the loaded module path. Each runs in a fresh ordinary-user process with a bounded timeout. The check does not activate a profile or open a window.
- Failed automatic checks block the installation commit. Upgrade rollback and original fallback-profile preservation remain in place. Same-version setup also checks COM loading.
- Normal GitHub distribution includes the unchanged, signed legacy 1.1.1 update manifest so the previous utility can still check its feed. This does not automatically migrate legacy users to the native input method.

## Automated evidence

- x64/x86 native candidate suites: **25 suites, 1,506 assertions, zero failures**.
- Installer contracts: **34 checks passed**, including package integrity, failed-gate package preservation, aliased-key rollback, stable/legacy profile ownership and invalid-path rejection.
- Controller integration against the previously installed build: both architectures load the registered service; mismatched DLL paths fail; current/default input selection remains unchanged.
- The actual 2.0.0 Setup EXE `/S /PREFLIGHT` passed against the existing installation under Windows PowerShell 5.1. No elevation or installation occurs in this mode.
- Legacy update.json SHA-256 and RSA signature match the existing 1.1.1 release. Its payload still targets the 1.1.1 legacy installer.

## Actual installation

- The final Setup EXE upgraded this PC from development build 10 to **2.0.0** on **2026-10-11 at 07:33 KST**, exit 0. After administrator approval, x64/x86 checks completed automatically; no physical typing fixture was launched.
- All **13 installed payload files** match the package hashes. Both profile registry views reference the new CK resource and CONTENTRIUM Keys description. The original Microsoft input-method fallback remains intact.
- The installed controllers pass positive COM-loading and negative wrong-path checks under Windows PowerShell 5.1. The same helper also passed under PowerShell 7. Current/default input profile selection is unchanged by these checks.
- Running the exact EXE again with `/S` returned exit 0 with `ALREADY INSTALLED`, after automatic loading checks. No second installation or input-profile selection change occurred.
- An earlier local candidate exposed Windows PowerShell 5.1 returning a null ExitCode for a short-lived redirected Start-Process child. That attempt correctly blocked commit and restored the previous COM/application registration. The helper now uses Process.Start with a retained handle and asynchronous output reads; the regression test exercises that exact helper. Failed staging files were archived after ownership and hash verification, without deleting them.

## Distributed file hashes

| File | SHA-256 |
| --- | --- |
| CONTENTRIUM-Keys-Setup.exe | `cbfff6eef90cdd4cb89cb6a0648ea208024c557c82590670116b026cb38dded3` |
| x64/RiumKeysInput.dll | `ec2423e9aa6844308a9754b273de7e317c4d6a9f49f62f1717c7768424c7119b` |
| x86/RiumKeysInput.dll | `82538eec1b62c507f21652245af3b269eecdc0ce83bc4e64d66c11d5e82d2ffa` |
| update.json (legacy compatibility) | `dfb65c69eca116306a9f325618b3e3fe1fdc1168e09d23bc3ec91bffae11d8f0` |

## Scope

The routing/composition implementation is unchanged from the previous build. Existing Premiere and Studio One physical evidence is linked from [input-owner-validation.md](input-owner-validation.md); it is not a fresh all-application test of this installer change. After Effects shortcut/return-to-text validation, broader clean-machine coverage, code signing and native automatic updates remain outside the confirmed scope.

Legacy identifiers remain in migration code and historical evidence so installed development versions can be recognized. They are not current product branding.
