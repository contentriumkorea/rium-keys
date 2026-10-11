# CONTENTRIUM Keys native input engine

Current release: **2.0.1**, Windows x64.
[Installation](../docs/installation.md) · [Release evidence](../docs/release-2.0.1.md) · [Application evidence](../docs/input-owner-validation.md).

This native TSF input method is based on Jamotong and replaces the 1.1.1 tray helper. Windows loads it when selected, without a companion hook/UIA process or startup executable. Native updates are installed manually. The legacy updater retains its unchanged signed 1.1.1 manifest and never automatically installs the native input method.

## Behavior and scope

Hangul is composed in ordinary input fields. Original keys pass through when there is positive evidence of a command surface. Generic TSF restrictions apply across applications; exact-build DVA, CCL and AE providers inspect bounded metadata after module verification. This does not prove every application or future version is supported. See [provider boundaries](providers/README.md).

Premiere 26.5.2.5 and Studio One 6.6.4.102451 passed the physical transitions recorded for development build 8. Subsequent branding and installer changes retain that routing/composition code. After Effects shortcut/return-to-text verification remains incomplete. Automatic installation checks do not substitute for application validation.

Mode resource 101 is white **가** for Hangul layouts; 102 is white **A** for Latin/direct input. Switches notify the shell. Resource 100 is the separate CK brand icon. Windows owns placement and each private HICON returned to it.

Service/profile GUIDs and RIUM Keys internal installation/settings paths are retained. Branding upgrades snapshot both registry views before mutation, including aliased CTF keys, and restore them on commit failure. Upgrade and rollback then re-register the owned branding with ITfInputProcessorProfileMgr::RegisterProfile. Description lengths are calculated, and the machine default-enable value and user enable/capability flags are preserved.

## Build and test

Requires Windows x64, the pinned LLVM MinGW toolchain, MSVC 14.51.36231 and Windows SDK 10.0.22621.0 for the controller, and NSIS 3.13 at tools/nsis-3.13 (or pass -Nsis). End users need no development tools or .NET.

```powershell
./native-ime/bootstrap-toolchain.ps1
./scripts/build-brand-icon.ps1
./scripts/build-mode-icons.ps1
./native-ime/build.ps1 -Architecture x64 -Installable
./native-ime/build.ps1 -Architecture x86 -Installable
./native-ime/build-control.ps1 -Architecture x64
./native-ime/build-control.ps1 -Architecture x86
./native-ime/build-setup.ps1
```

The setup build runs installer contracts and native candidate suites, checks DLL versions/hashes, and outputs `native-ime/out/release/CONTENTRIUM-Keys-Setup.exe` and `SHA256SUMS.txt`.

Setup runs as the ordinary user. Only machine registration requests UAC. PowerShell execution policy is scoped to those processes; persistent policy and enterprise Group Policy are not modified.

## Installation transaction

1. Verify package hashes, version, previous installation/input state and user identity.
2. Save recovery data; stage versioned binaries in Program Files with elevation.
3. Run each architecture's controller in a fresh ordinary-user process. Verify enabled registration, six categories and COM path/model; instantiate the registered text service and check its loaded DLL path and required interfaces.
4. Verify the registered CK resource through shell icon extraction. Commit or roll back. New installs select the native profile and remove a detected legacy utility after automatic checks. Upgrades preserve selection/defaults and the original uninstall fallback.

The load check never activates a profile, attaches an input sink or opens a window. No typing test is part of installation. A failed check blocks commit and preserves recovery files. Same-version setup verifies files, registration, shell brand extraction, ownership and loadability without reinstalling. The ordinary-user refresh requests shell icon-cache invalidation. Setup offers a Windows restart with Later selected by default; it never forcibly stops Explorer or user applications. Silent success returns 3010 (restart required); preflight success returns 0.

2.0.0 upgrades directly to 2.0.1. Development build 10 can first use the archived 2.0.0 installer, then 2.0.1. Other older native builds must be removed through Installed Apps first. Exact legacy version identifiers remain in migration checks and historical evidence to recognize existing installations.

## Development diagnostics

Physical EDIT / V / EDIT fixtures remain outside the distributed installer:

```powershell
./native-ime/build-fixture.ps1
./native-ime/test-fixture-launch.ps1
./native-ime/test-installed-load.ps1
./native-ime/test-installed-branding.ps1
```

The last two commands are read-only tests against an already installed input method. They verify both COM architectures and shell brand extraction, reject foreign paths and unelevated brand mutation, and check input selection preservation. Physical fixtures require deliberate developer operation; Setup never starts them.

The default DLL build without -Installable is fixture-only and cannot activate outside RiumImeFixture.exe. Builds never register or activate an input method. Installer rollback tests use disposable HKCU keys. Code signing, unattended updates, broader clean-machine coverage and remaining application scenarios are not included in this release.

## Upstream and history

Jamotong is pinned to aa78c1a328bab5d193f5f9932c5f824d5fb222d5 (0.73.1). Its [MIT license](third_party/jamotong/LICENSE) and [copyright notices](third_party/jamotong/COPYRIGHT.md) are preserved and packaged. Upstream dictionaries, auxiliary helper and settings window are not distributed.

Configuration uses `%APPDATA%\RIUM Keys` and `HKCU\Software\Contentrium\RiumKeysInput`. Recovery uses `%LOCALAPPDATA%\Contentrium\RIUM Keys\Recovery`. Input/document contents are not uploaded; the native input method has no network updater.

[Historical records](../docs/native-history.md) are not current installation instructions. [Gureum comparison](../docs/gureum-input-routing-analysis.md) and [cross-language research](../docs/cross-language-shortcut-research.md) describe the architecture and its limits.
