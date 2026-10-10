# CONTENTRIUM Keys native input engine

Current distributable: **2.0.0-preview.10**, manual Windows x64 preview.
[User installation instructions](../docs/installation.md) · [release evidence](../docs/preview-10-release.md) · [application evidence](../docs/input-owner-validation.md).

This is a native TSF input method based on Jamotong, replacing the 1.1.1 tray helper. It is loaded by Windows when selected. There is no companion hook/UIA process or startup executable. The production 1.1.1 automatic updater is unchanged and does not install this preview. Unattended native updates and universal application compatibility remain incomplete.

## Current behavior and scope

The engine composes Hangul in ordinary input fields and passes original keys when it has positive evidence of a command surface. Generic TSF restrictions apply across applications. Exact-build DVA, CCL and AE providers additionally examine bounded metadata in the hosting process after module verification; they do not prove that every application or future version is supported. See [provider boundaries](providers/README.md).

Premiere 26.5.2.5 and Studio One 6.6.4.102451 passed the physical transitions recorded for preview.8. Preview.9 retained that input-routing code. Preview.10 changes presentation and packaging, and uses the same routing/composition implementation. After Effects shortcut/return-to-text verification remains incomplete. Automated fixtures do not substitute for physical application validation.

The input mode button uses resource 101 (white **가**) for actual Hangul layouts and resource 102 (white **A**) for Latin/direct input. Mode switches notify the shell immediately. Resource 100 is the independent CK input-method brand icon. Windows owns the placement of those two surfaces; they are not duplicate utility instances. Every returned HICON is private and owned by the shell.

Existing service/profile GUIDs and the `RIUM Keys` installation/settings paths are retained. Preview.10 fixes the stale profile `IconFile` left pointing to preview.2. The upgrade snapshots both registry views before mutation, including aliased CTF keys, and restores both names and icons on machine commit failure.

## Build and test

Prerequisites: Windows x64; pinned LLVM MinGW toolchain; MSVC 14.51.36231 + Windows SDK 10.0.22621.0 for controller/fixture; NSIS 3.13 at `tools/nsis-3.13` (or pass `-Nsis`). End users need none of those development tools or a separate .NET runtime.

```powershell
./native-ime/bootstrap-toolchain.ps1
./scripts/build-brand-icon.ps1
./scripts/build-mode-icons.ps1
./native-ime/build.ps1 -Architecture x64 -Installable
./native-ime/build.ps1 -Architecture x86 -Installable
./native-ime/build-control.ps1
./native-ime/build-fixture.ps1
./native-ime/test-fixture-launch.ps1
./native-ime/build-setup.ps1
```

`build-setup.ps1` runs installer contracts and all native candidate suites, stages only the manifest payload, verifies DLL versions/hashes and writes:

- `native-ime/out/release/CONTENTRIUM-Keys-Setup.exe`
- `native-ime/out/release/SHA256SUMS.txt`

The installer has `asInvoker` behavior, extracts its own payload, and selects a new install or the supported preview.9 upgrade. Both require an ordinary-user physical native EDIT / V / EDIT test before commit. UAC is requested only for machine registration. PowerShell execution policy is scoped to those installer processes; persistent policy and enterprise Group Policy are not modified.

The default build without `-Installable` remains fixture-only. It cannot activate outside `RiumImeFixture.exe`. Build scripts do not register or activate an input method. Icon tests open the actual candidate as resource data and exercise the production language-bar implementation against isolated state. Installer rollback tests use only disposable HKCU values, not live input registration.

## Installation transaction

1. Validate package hashes, version, previous installation/default input state and ordinary user identity.
2. Capture recovery data; stage versioned DLLs in Program Files with elevation.
3. Load the newly installed DLL in a fresh, non-elevated test process.
4. Verify physical Korean / original V down-up / Korean with the selected Korean profile retained.
5. Commit or roll back. New installs select the native profile and remove a detected legacy utility after successful verification. Upgrades preserve the current/default profile and original uninstall fallback.

Only preview.9 → preview.10 is an in-place native upgrade. Older native previews must be removed through their current installed-app entry first. A failed transaction keeps files for recovery and refuses to overwrite a possibly loaded DLL. Same-version setup verifies files, both COM views, branding, user ownership and live enabled registration before reporting a no-op success.

Source-only release scripts do not sign or publish automatically. Code signing, unattended native updates, a broader clean-machine install/uninstall matrix and remaining application scenarios are still production-release work. This preview must not replace the old stable updater feed.

## Upstream and history

Jamotong source is pinned to `aa78c1a328bab5d193f5f9932c5f824d5fb222d5` (0.73.1). Original [MIT license](third_party/jamotong/LICENSE) and [copyright notices](third_party/jamotong/COPYRIGHT.md) are preserved and packaged. Internal upstream C names remain for a reviewable fork. Upstream dictionaries, auxiliary helper, settings window and upstream installer/cleanup targets are not distributed.

Configuration uses `%APPDATA%\RIUM Keys` and `HKCU\Software\Contentrium\RiumKeysInput`; recovery uses `%LOCALAPPDATA%\Contentrium\RIUM Keys\Recovery`. Input and document contents are not uploaded. No network updater runs in the native preview.

Older investigation notes are retained as [historical records](../docs/native-preview-history.md). Their failed/earlier-version observations are not current installation instructions. The [Gureum comparison](../docs/gureum-input-routing-analysis.md) and [cross-language research](../docs/cross-language-shortcut-research.md) explain the original architecture decision and its limitations.
