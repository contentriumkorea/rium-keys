# Inline preedit cancellation experiment

This is a disposable x64 diagnostic, excluded from the product build, installer,
and updater. It is not a working all-application shortcut solution.

It tests whether a narrowly selected inline host can receive its original bare
C/V key through its WndProc while keeping the existing Korean composition path,
then cancel one fresh preedit if the host delegates it to its default IME window.
The default IME message is **always delivered**. A failed cancellation therefore
keeps ordinary preedit visible; the probe does not hide a live composition.

## Build and isolated tests

From the repository root:

```powershell
./native-ime/experiments/inline-preedit/build.ps1 -Test
```

The existing pinned llvm-mingw toolchain and Windows SDK 10.0.22621.0 are required.
The SDK `ComCtl32.Lib` imports `GetWindowSubclass` correctly for the tested Windows
system DLL; the pinned MinGW import library's named import failed on this PC.
The build command does not download dependencies, register an IME, select a
profile, install a hook in another application, or change user configuration.

The 34 native fixture checks use own message-only windows and controlled IMM
dependencies. They exercise dispatch, focus/context changes, reentry, failed
setup/removal/cancellation, exact-once delivery and non-interference with later
messages. Their temporary-character model is **not** a real Korean IME cancel
test. The separate loader check creates no windows and installs no hooks.

The separate global-policy gate is deliberately **failing** with this design:

```powershell
./native-ime/out/inline-preedit-probe/inline-preedit-fixture.exe --release-gate
```

It models a legitimate offspot editor that delegates preedit and receives the
result later, and a text editor with a raw-key command handler. The disabled
controls preserve text without invoking commands. With the experiment enabled,
the first loses its pending text and the second executes a command during text
entry. These are controlled counterexamples, not a claim of data loss observed
in Premiere or Studio One. Passing the 34 mechanism checks does not pass this
policy gate. Do not remove these requirements to make a release appear green.

## Live evidence on 2026-10-10

In disposable projects, while Korean mode was verified in the key trace:

- Premiere Pro 2026 search C inserted only `ㅊ`; caption V inserted only `ㅍ`
  without selecting another tool.
- Premiere timeline C selected Razor. The delegated preedit cancellation
  returned TRUE and `GCS_COMPSTR` became zero. Returning to search inserted only
  `ㅊ`, without a leftover timeline character.
- Studio One 6.6.4 C toggled Click/metronome with the same cancellation result.
  Three separately armed one-shot trials in the same process succeeded.
  Its native search EDIT inserted only `ㅊ` both with and without the experiment.

The initial Studio One attempt used English mode and is excluded from those
claims. All controllers were stopped with both hooks removed and zero mapped
writers. Test search text was cleared and the added Premiere caption character
was undone. No project was saved, and no installer/profile/release was changed.
These observations do not verify a continuously armed implementation, held keys,
arbitrary shortcuts, all controls in those frameworks, or other applications.

## Explicit disposable live trial

Use only a disposable document, a known non-destructive C/V command and exclusive
keyboard/mouse time. Obtain the exact focused HWND, process and UI thread first.
The controller verifies that HWND's process/thread and current thread focus.

```text
inline-preedit-experiment.exe <pid> <focused-hwnd-decimal-or-hex> <absolute-dll-path> <seconds-1..120> --inline-only-experiment
inline-preedit-experiment.exe --stop <pid> <tid>
```

Wait for `READY` before the single physical key. `kind=6` proves a hook callback
initialized the run; `READY` alone does not. The engine never rearms in the same
run. A new controller token permits a fresh run even if Windows retained the DLL.
Ctrl, Alt, Shift and Win chords are excluded. Other keys are not restored.

Logs contain message types, C/V equality, flags, pointer identities, mode metadata
and composition **byte length only** after a cancellation request. They contain
no composition text, window titles or document contents. `open=2`,
`conversion=0xffffffff`, `length=-2147483648` mean unavailable.

`STOPPED` reports removal of both hooks and outstanding mapped-memory users.
`writers=0` is not proof that all OS hook callback frames have returned or that
the host has unloaded the DLL. If temporary subclass removal fails, an inert
callback and module reference are intentionally retained until process exit.

## Scope and unresolved limits

- SendMessage reaches the WndProc, bypassing accelerator/pretranslation loops.
- Only the first queued preedit following a fresh START within one second of
  the original key is examined. Sent-only composition paths are not covered.
- Default-window delegation is not a universal proof of non-editing. Legitimate
  offspot editors need that path; they are unsupported by this experiment.
- No delegation does not prove that an app inserted or accepted text.
- HWND, focus, HIMC and event epoch are guards, not a complete ownership proof.
  Same-window editing state changes may not be exposed.
- If raw-key delivery changes focus, cancellation is aborted and the original
  PROCESSKEY is retained. Preventing that key's composition at a new destination
  is not a guarantee of this probe.
- Later END/RESULT/IME_CHAR messages remain untouched. Passing the fixture does
  not prove stale-result rejection or support for repeated input.
- Cancellation may fail, be deferred, or occur after a visible transient popup.
  The live trial must verify both the IME state and the next real text field.

See `docs/gureum-input-routing-analysis.md` for actual app evidence and rejected
approaches. Do not promote this code into the native engine from fixture results
alone.
