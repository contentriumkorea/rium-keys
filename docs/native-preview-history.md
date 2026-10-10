# Historical native preview notes — through preview.9

These notes are retained for investigation history. Version and installation statements below describe past checkpoints. For current instructions see [installation.md](installation.md); for physical results see [input-owner-validation.md](input-owner-validation.md).

# CONTENTRIUM Keys native input engine — development

This is the Jamotong-based replacement candidate. The production updater still
distributes the 1.1.1 tray helper. An explicitly requested **local preview
installation** is available below; it is not a public release and has no automatic
updater. The locally installed preview.9 contains the new logical input-owner
providers and passed its installed native EDIT/button physical test on
2026-10-11. It retains the preview.8 fix for the reproduced first
syllable split after a command-to-text transition and passes 1,202 native
assertions. The installed preview.8 subsequently passed physical text/command/text
transitions in the tested Premiere search, caption and timeline routes and Studio
One search/workspace, including switching applications. After Effects and broader
compatibility remain under validation; see the [actual test record](../docs/input-owner-validation.md).

The preview.8 display name is **CONTENTRIUM Keys**. The input service/profile
GUIDs, settings locations, internal executable names and versioned installation
root are preserved so existing installations upgrade without a duplicate input
method. The upgrade updates and reads back the existing Windows profile and
installed-app names; it restores the previous names if the machine commit fails.
The local upgrade committed on 2026-10-11 at 01:19 KST after physical
Korean/shortcut/Korean input passed. Both registry views, DLL hashes, profile
description and installed-app version were independently read back.

Preview.9 adds the user's transparent **CK** notification-area logo. The PNG
source and conversion details are in [assets](../assets/README.md). The icon is
embedded at nine Windows sizes and loaded by both the native input button and
legacy executable. Input mode and enabled state remain available in the tooltip
and existing right-click menu. Preview.9 upgrades preview.8; input routing and
composition behavior are unchanged by the icon addition.
Its local installation committed on 2026-10-11 at 06:34 KST after the installed
physical Korean/shortcut/Korean test passed again. Both installed DLL hashes,
COM views, installed-app name/version and profile state were verified afterward.

On 2026-10-10 an external one-shot experiment restored C in the tested Premiere
timeline and Studio One workspace while preserving the tested search/caption
input. It is not part of the installed preview. A separate controlled policy
gate exposes text loss in a legitimate offspot editor and unintended raw-key
commands during text entry. The experiment must not be enabled globally or
packaged as a completed fix. See [experiment scope and evidence](../native-ime/experiments/inline-preedit/README.md).

`prepare-package.ps1` now runs `test-package.ps1` against both actual installable
DLL candidates before writing package files. No known-bug expectation switch is
accepted by that gate. Passing automated contracts is necessary but does not
replace physical application and installer verification. The candidate now
passes its automated package gate and installed physical fixture. Actual
application transitions remain release gates.

The latest candidate fixes Chromium-style interim selection and composition
lifetime/reentrancy errors. On 2026-10-10 the exact x64 candidate passed a real
Windows TSF own-document run with Chromium-style static flags: nine consumed
keys, exact Hangul/backspace/space results, collapsed TSF/ACP selections without
interim highlighting, live compositions and verified profile/mode restoration.
This does not test Chromium itself or solve host workspace classification.
The candidate also binds composition, pending commits and delayed resend work
to their original input owner. Exact-build DVA, CCL and AE providers perform bounded
metadata reads after background module verification. Their verified scope and
remaining unknown states are documented in [providers/README.md](../native-ime/providers/README.md).
Automated owner, inline, edit-session, pending, resend, routing and runtime
regressions pass on x64 and x86; these are not real application typing results.
Earlier findings and the official Premiere SDK/event-observer investigation are in
[the evidence report](../docs/gureum-input-routing-analysis.md).

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
- The fork has separate service/profile identities. Default fixture builds use
  `RIUM_FIXTURE_ONLY` and reject activation outside `RiumImeFixture.exe`.
  `-Installable` produces a separate local-preview output; it is never included
  in the production installer/updater by the build scripts.
- User data uses `%APPDATA%\RIUM Keys` and
  `HKCU\Software\Contentrium\RiumKeysInput`, separate from upstream Jamotong.
  The preview initializes its configuration to Korean, disables the upstream UI
  helper and extra command shortcuts, and exposes only input controls in the
  Windows input indicator. The actual initial Windows mode can still be English;
  initial mode synchronization remains a release gate (see the installed test below).
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
./native-ime/out/x64/InlineTests.exe
./native-ime/out/x64/EditSessionTests.exe
./native-ime/out/x64/RoutingTests.exe "$PWD/native-ime/out/x64/RiumKeysInput.dll"
./native-ime/out/x86/EngineTests.exe
./native-ime/out/x86/InlineTests.exe
./native-ime/out/x86/EditSessionTests.exe
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
- Verified preview.3 x64 and x86: 26 engine, 49 routing/policy and 52 inline-composition checks pass in each
  architecture. Routing tests exercise the real DLL with controlled contexts;
  they are not physical keyboard or all-application compatibility tests.
- Manual TIP activation using an ordinary application client ID returned
  `E_INVALIDARG`. This is not treated as successful Windows registration.
- On 2026-10-10, the complete **single-process native EDIT physical test passed**:
  `gksrmf` + Space produced `한글 `; the button then received original V down/up
  with the fork and Korean mode verified at both events; returning to the EDIT
  and repeating the word produced exact `한글 한글 `. Profile restoration was
  read back successfully, fixture exit was 0, and temporary registration was removed.
  This supersedes the earlier timeout/cancelled attempts.
- This is evidence for the standard Win32 EDIT/button transition only. It does
  not establish Premiere/After Effects compatibility or a latency bound.
- A two-process physical test was prepared, but Windows administrator confirmation
  was cancelled before registration or fixture launch. Cross-process behavior
  is still unverified. Production 1.1.1 was restarted and both temporary COM/TIP
  registration keys were confirmed absent after the attempt.

## Physical fixture

```powershell
./native-ime/build-fixture.ps1
./input-probe/test-registration.ps1 -ReusedEngine
# For interleaved focus changes between two independently activated processes:
./input-probe/test-registration.ps1 -TwoProcesses
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
TwoProcesses copies only the generated fixture/DLL into an isolated output
subdirectory and waits for both owned processes before removing registration.
DLL files already loaded into another host can require later cleanup after that
host exits; no application is terminated to unload them.

## Remaining release gates

An application can expose the same writable TSF context in both an editing
field and a workspace. Missing restrictions do **not** prove that a control is
editable or non-editable. This fork does not claim to solve that ambiguity.

Before a production release: verify actual Premiere/After Effects context
transitions, Korean input in modern and legacy hosts, original shortcut delivery,
initial mode synchronization, composition focus-loss lifetime, settings/registry
isolation, tray-only controls, uninstall/rollback behavior, and signed
automatic-update migration.
The preview does not bundle Hanja dictionaries or enable upstream auxiliary
commands. Automatic-update migration and complete application testing are
required before replacing the public release.

## Explicit local preview installation

```powershell
./native-ime/build.ps1 -Architecture x64 -Installable
./native-ime/build.ps1 -Architecture x86 -Installable
./native-ime/build-control.ps1
./native-ime/build-fixture.ps1
./native-ime/test-fixture-launch.ps1
./native-ime/prepare-package.ps1
./native-ime/install-local.ps1 -Preflight
./native-ime/install-local.ps1
```

The ordinary-user installer saves the current default and active input profiles,
copies the legacy utility into a recovery directory, stops its processes, and
requests one Windows administrator confirmation. The elevated worker installs
versioned x64/x86 DLLs and registers the native profile. Before committing the
migration, an ordinary-user test process with a different filename must pass
the physical Korean / V / Korean test using the registered installed DLL.
Only then is RIUM selected and the old utility removed with its own uninstaller.

The first installed preview used `C:\Program Files\RIUM Keys\2.0.0-preview.2`.
Windows loads it when selected; there is no legacy keyboard hook, UIA worker or
login executable. Microsoft IME remains available. The Windows installed-apps
entry provides removal and restoration of the captured previous input method.
Loaded DLL files may remain until the hosting applications exit; the uninstaller
does not terminate user applications. Recovery state is stored under
`%LOCALAPPDATA%\Contentrium\RIUM Keys\Recovery`.

This installer refuses an existing native registration/version directory and
input configurations it cannot snapshot faithfully. It is a first-install
preview. The current `upgrade-local.ps1` requires preview.3 and installs preview.4; it does not accept preview.2 directly. An interrupted commit is reported as unknown
instead of being labeled rolled back.

### Verified local installation on 2026-10-10

`2.0.0-preview.2` was installed and selected as the user's default/active keyboard
profile. Fresh verification confirmed enabled registration and six categories,
x64/x86 COM paths, all seven packaged file hashes, the Windows installed-apps
entry, and the `Installed` transaction state. The 1.1.1 utility's executable,
processes, uninstall entry and startup value were absent after its own uninstaller
finished. The public release remains 1.1.1.

The ordinary-user `RiumInstalledSmoke.exe` process was confirmed to load the x64
DLL from Program Files. Its physical test passed: exact `한글 `, original V
keydown/up while the RIUM profile and Korean mode were retained, then exact
`한글 한글 ` after returning to the EDIT without another language toggle. The
fixture restored its previous process-local profile and exited with code 0.
The first input initially produced Latin letters; **one Right Alt Korean toggle
was needed before the passing sequence**. This test does not prove initial mode
synchronization, cross-process transitions or Adobe compatibility.

Before this success, two UAC attempts were cancelled before registration. A later
preview.1 attempt registered successfully but Windows treated the renamed test
executable as requiring elevation because its manifest was absent. Its profile
was removed and a fresh readback confirmed the original input state. The fixture
now embeds `asInvoker`/`uiAccess=false`, and a regression test verifies that a
renamed copy launches with an unelevated token before COM or registration work.
Both x64 and x86 launch tests pass. Installation checks that staged executable
before stopping the old utility or opening UAC. Preview.1's failed-attempt files
are retained as recovery evidence; they are not registered or loaded by the new
COM paths. Preview.2 uses its own directory, without overwriting those DLLs.

The local verification report is generated under
`native-ime/out/installed-verification.json` (ignored). The physical test log SHA-256
is `8F36B9FA9E5C7DB7DBB9AC8A6E5A1C5AF7742DED7EE2201B01A6B0EF0C59E1A8`.

API references: [profile activation](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile),
[user profile enablement](https://learn.microsoft.com/en-us/windows/win32/tsf/installlayoutortip),
[default input selection](https://learn.microsoft.com/en-us/windows/win32/tsf/setdefaultlayoutortip).

### Inline composition correction (preview.3)

The blue floating syllable was the commit-only fallback for every transitory
CUAS context. Removing that condition alone caused CUAS to end composition on
each key, producing separate consonants and vowels. A transitory Korean preedit
now selects exactly its last character with `fInterimChar=TRUE` and
`TF_AE_NONE`; native TSF stores retain their collapsed caret. This follows the
[Windows Korean interim-selection contract](https://learn.microsoft.com/en-us/windows/win32/api/msctf/ns-msctf-tf_selectionstyle).
Capable contexts show the current syllable in the document as it is typed.
The unsupported-host fallback remains visible so that uncommitted text cannot
silently disappear.

Space updates the last syllable and space in the same synchronous TSF transaction.
Previously, ending CUAS composition and immediately calling `EM_REPLACESEL`
could deliver `한 글` instead of `한글 `. Finalization keeps the original
composition alive across host callbacks. Deferred focus finalization is bound
to that composition; subsequent input cannot edit another document with its
cookie or overwrite a Space that was already written. Cross-context keys and
Escape/Hangul recovery remain available if a host rejects finalization.

```powershell
./native-ime/build.ps1 -Architecture x64 -Installable
./native-ime/build.ps1 -Architecture x86 -Installable
./native-ime/out/installable/x64/InlineTests.exe
./native-ime/out/installable/x86/InlineTests.exe
./native-ime/build-control.ps1
./native-ime/build-fixture.ps1
./native-ime/test-fixture-launch.ps1
./native-ime/prepare-package.ps1
./native-ime/upgrade-local.ps1 -Preflight
./native-ime/upgrade-local.ps1
```

The preview.3 upgrade retained
preview.2, staged preview.3 in its own Program Files directory, and
updates both COM views without unregistering the shared keyboard profile.
The ordinary-user verifier checks the DLL loaded by a fresh physical fixture,
inline preedit, exact Korean text and Space, original V down/up on a button, and
Korean input after returning. The machine worker commits only after that test.
Uninstall retains the original Microsoft input fallback captured during the
first installation. Failed upgrades verify both restored COM paths, installed
metadata, and input profile state; incomplete recovery is reported explicitly.
No application is terminated to unload an old DLL. Already running applications
can require a restart to load the updated input method.

For registration-free development testing:

```powershell
./native-ime/build.ps1 -Architecture x64 -Diagnostic
./native-ime/build-inline-fixture.ps1
./native-ime/out/inline-x64/RiumImeFixture.exe
# Native EDIT, with physical keys and per-syllable document/IMM readback:
./native-ime/out/inline-x64/RiumImeFixture.exe --native
```

Focus the disposable fixture window to start. Its application-local manifest
loads its own DLL without changing machine registration. Diagnostic builds are
restricted to fixtures, isolate their logs, and cannot be built as installable
packages. The real `ITextStoreACP` fixture passed all seven checks for initial,
vowel, final, Backspace, syllable boundary, visible last syllable and Space.
The x64/x86 suites each pass 52 inline, 26 engine and 49 routing checks, including
selection failures, reentrant termination, deferred finalization, wrong-context
edit rejection and Space-write/finalize failure. These checks do not establish
compatibility with every application or remove the existing Adobe release gate.

### Verified preview.3 upgrade on 2026-10-10

The local upgrade committed successfully. Fresh readback confirmed
`2.0.0-preview.3`, the `Installed` transaction state, both versioned COM paths,
all seven installed package hashes, and the Windows installed-apps entry.
The native profile remained enabled, active and the default, with six categories.
The original uninstall fallback and preview.2 files were retained.

A fresh ordinary-user physical fixture loaded the DLL from preview.3's Program
Files directory. Physical keys showed `ㅎ → 하 → 한` inside the native EDIT,
Backspace returned to `하`, and completion plus Space produced exact `한글 `.
After clicking the button, the application received original V down/up while
the RIUM profile and Korean mode remained selected. Returning to the EDIT
produced exact `한글 한글 ` without a language toggle. Inline preedit, profile
restoration and exit code 0 all passed. The physical log SHA-256 is
`3F1770A261423FF94A878F71D633A0962752A45F0F92188973B72D60BA973E89`.

The running chat application still held the preview.2 DLL after installation;
it needs a restart to load the new version. Its composer was not directly
automated. This result verifies the installed native EDIT/button path, not
all applications or the pending Adobe scenarios. The detailed local report is
`native-ime/out/inline-upgrade-verification.json` (ignored).

### Premiere workspace diagnosis (preview.4, local only)

The running Premiere process was verified to load preview.3. After Korean input
in Project search, clicking the timeline and pressing V produced a floating
`ㅍ` composition instead of selecting the tool. The search EDIT exposed a native
caret; the timeline and graphics editor did not. A native-caret-only rule would
therefore risk disabling Korean graphics text. No such rule is enabled.

Preview.4 deliberately preserves preview.3 routing while adding an opt-in,
temporary metadata trace inside the host process. It is **not a shortcut fix**.
Normal `RoutingTests.exe` now includes the reproduced transitory workspace
expectation and reports that known failure. The explicit
`--expect-known-workspace-bug` option characterizes the diagnostic baseline; it
does not demonstrate that the bug has been repaired.

For a local diagnosis, set `FocusTraceImage` (REG_SZ, exact executable basename)
and `FocusTraceUntil` (REG_QWORD, UTC Windows FILETIME) in
`HKCU\Software\Contentrium\RiumKeysInput` before starting the target process.
The deadline must be within 30 minutes. Settings are sampled once per thread;
the loaded DLL version must be checked after the user restarts the application.
The trace writes at most 512 changed metadata rows per process, with a 100 ms
per-thread sampling interval, to `%TEMP%\RiumKeys-focus-<pid>.log`. It captures
context flags, handles, control class, and caret/IMM geometry. It never reads
keys, composition text, document text, titles, or document filenames.
Removing the registry settings prevents newly initialized threads from tracing;
already armed threads stop at the cached deadline. Logs remain local for review.

The current `upgrade-local.ps1` only stages verified preview.3 to preview.4 and
retains preview.3 for rollback. No user application is terminated. Physical
fixture verification is required before committing registration; successful
installation alone does not establish Premiere shortcut compatibility.

#### Preview.4 installation and Premiere results, 2026-10-10

Preview.4 is installed locally, with all seven manifest hashes matching. The
registered input profile is enabled, active, the user's default and has six
categories. Premiere was confirmed to load the versioned Program Files DLL.
The installed native EDIT/button physical smoke test passed after one initial
Korean toggle; its fixture restored the previous process-local profile. Evidence:
`out/focus-upgrade-verification.json` and the transaction's physical fixture log.

Actual Premiere testing **fails** the intended shortcut behavior in both paths:
Project search to timeline/C displays `ㅊ`, and on-video graphics Korean `한`
to timeline/V displays `ㅍ`. The graphics editor and timeline expose the same
writable transitory context, dynamic flags, absence of a native caret, and IMM
composition style. Their different HWNDs and sizes are not editing contracts.
No caret-only, dynamic-bit, window-name or application-name rule was added.

The [Gureum source comparison and Premiere observations](../docs/gureum-input-routing-analysis.md)
describe the measured ambiguity, the macOS/Windows contract differences, and
the limits of possible follow-up diagnostics. These findings do not establish
an application-independent classifier or a repaired shortcut path.

The temporary registry trace opt-in was restored to its original absent state.
The already armed Premiere thread retains its original deadline of
2026-10-10 14:02:50 KST; it cannot trace past that deadline. Logs remain local.
The source comparison and remaining feasibility gate are recorded in
[Gureum and Windows input routing](../docs/gureum-input-routing-analysis.md).
This installed diagnostic build is not a fixed public release.

### Experimental focus ownership changes (not installed)

The development source binds inline composition and deferred Korean commits to
their original focus HWND as well as their context. A shared CUAS context is not
enough to authorize an edit after focus moves. Edit callbacks recheck the captured
owner before inserting text, and `RequestEditSessionDataEx` propagates the inner
session result even when `ASYNCDONTCARE` executes synchronously. A queued result
is not evidence of a completed insertion.

Unresolved fallback text is retained rather than overwritten by another syllable.
Escape explicitly discards that pending text; the preview callback does not
insert it first. Returning to the original owner permits a synchronous retry.
Reentrant key callbacks cannot retry the same slot twice, and completion only
clears the captured slot generation. Preserved layout-switch keys apply the same
ownership transition. A finished composition's window is not reused as owner of
the next syllable, and temporary focus mismatch does not demote a live inline
composition to the fallback path.
An actual distinct native document can still finalize its retained composition
after focus leaves; this path checks COM identity instead of guessing from a
window class or undocumented status bits.

This is an experimental ownership repair, **not the missing workspace/text
classifier**. In a host that rejects finalization, pending fallback text can
block new Korean composition until the original target accepts it or the user
discards it with Escape. No installable package, machine registration update, or
public release has been made for these source changes. This PC remains on
preview.4, which still fails the Premiere workspace shortcut scenario.

The normal routing suite deliberately retains the failing first-V assertion.
Passing with `--expect-known-workspace-bug` characterizes that failure; it must
not be used as a release success. The added `EditSessionTests.exe` exercises the
production edit-session code, including inner errors, asynchronous scheduling,
reference lifetime, and focus changes during host callbacks.

The current fixture builds on 2026-10-10 passed 26 engine, 76 inline, 15 edit-session,
and 86 routing characterization checks per architecture (x64 and x86). Normal
routing reports **one failure out of 86** in each: `transitory workspace without
text focus passes first V`. Local results are saved in ignored
`out/ownership-x64-results.json` and `out/ownership-x86-results.json`. These are
source-level tests, not installed-build Adobe compatibility evidence.
