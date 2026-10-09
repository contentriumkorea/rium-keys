# RIUM Keys native input engine — development

This is the Jamotong-based replacement candidate. It is **not installed as the
system input method and is not included in the production updater**. Production
1.1.1 remains the existing tray helper.

## Reuse and local changes

- Upstream: https://github.com/rubidus-api/jamotong_ime
- Pinned commit: `aa78c1a328bab5d193f5f9932c5f824d5fb222d5`
- Source version at this commit: **0.73.1** (`src/version.h`).
- Code license: MIT. The original LICENSE and COPYRIGHT.md are preserved in
  `third_party/jamotong`. The notices cover the included Spleen icon glyphs too.
  No upstream Hanja dictionaries, installer, cleanup executable, or UI helper
  is built or distributed by this development build.
- The Korean automaton, keyboard maps, inline composition, and legacy CUAS
  commit path are reused. The hand-written `input-core` engine remains historical
  fixture code; it is not the selected product engine.
- Local fixes read the event's **current** context restrictions in both preview
  and actual keydown. Null, disconnected, read-only, disabled, empty, failed,
  and malformed restriction states pass the original key. Absent optional
  compartments remain compatible with existing text controls.
- The fork has separate service/profile identities, and `RIUM_FIXTURE_ONLY`
  rejects activation outside `RiumImeFixture.exe`. Removing this restriction
  requires the release gates below, not a build switch in the shipping installer.
- `win32-compat.h` supplies three official WinUser accessibility constants absent
  from the pinned compiler headers. No original Korean composition algorithm
  changes were needed for these tests.

Upstream identifiers are retained in internal C symbol/file names to keep the
fork diff reviewable. User-facing product integration is unfinished. Do not run
the retained upstream Makefile's installer/manager/cleanup targets: use the
RIUM build scripts below.

## Reproduce

```powershell
./native-ime/bootstrap-toolchain.ps1
./native-ime/build.ps1 -Architecture x64
./native-ime/build.ps1 -Architecture x86
./native-ime/out/x64/EngineTests.exe
./native-ime/out/x64/RoutingTests.exe "$PWD/native-ime/out/x64/RiumKeysInput.dll"
./native-ime/out/x86/EngineTests.exe
./native-ime/out/x86/RoutingTests.exe "$PWD/native-ime/out/x86/RiumKeysInput.dll"
```

The portable compiler is pinned to llvm-mingw `20261006`, SHA-256
`317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a`.
The bootstrap checks the downloaded archive against the GitHub release digest
before extraction. The build does not register an input method.

Test results on this PC:

- Original key sink: six failing routing cases (null, changed disabled value,
  empty, read-only, disconnected, stale cache). The added failure-injection
  suite also reproduced four compartment error/type failures before the fix.
- Current x64 and x86: 26 reused-engine checks + 36 routing/activation-guard checks pass in each
  architecture. Routing tests exercise the real DLL with controlled contexts;
  they are not physical keyboard or all-application compatibility tests.
- Manual TIP activation using an ordinary application client ID returned
  `E_INVALIDARG`. This is not treated as successful Windows registration.
- A temporary, process-local native EDIT trial physically composed `gksrmf`
  into `한글`. That trial expired before the shortcut transition was finished;
  it is **not** a passing end-to-end test.
- The subsequent trial, with stricter shortcut/profile checks, did not run:
  Windows elevation was cancelled. Temporary machine registration is absent;
  the original RIUM Keys 1.1.1 tray process was restarted.

## Physical fixture

```powershell
./native-ime/build-fixture.ps1
./input-probe/test-registration.ps1 -ReusedEngine
```

The second command needs a Windows administrator confirmation for temporary
COM/TSF registration. It never selects the profile as the system default.
The ordinary user fixture uses a native EDIT's own IMM/TSF bridge and an isolated
APPDATA directory. Enter `gksrmf` then Space, click the button and press V, then
return to the input and enter `gksrmf` then Space. A pass requires:

1. Exact fixture text before and after the transition.
2. Original V keydown and keyup received by the button's application WndProc.
3. Fork input profile and Korean mode still selected.
4. Previous process-local profile restored and read back correctly.
5. Temporary registration removed by the parent controller.

The controller bounds the test and cleans registration on success/failure.
DLL files already loaded into another host can require later cleanup after that
host exits; no application is terminated to unload them.

## Remaining release gates

An application can expose the same writable TSF context in both an editing
field and a workspace. Missing restrictions do **not** prove that a control is
editable or non-editable. This fork does not claim to solve that ambiguity.

Before a production release: verify actual Premiere/After Effects context
transitions, Korean input in modern and legacy hosts, original shortcut delivery,
composition focus-loss lifetime, settings/registry isolation, tray-only controls,
versioned DLL installation, rollback, and signed automatic-update migration.
The upstream configuration windows, preserved shortcuts, registry paths, and
helper process are not yet adapted for RIUM's product behavior.
