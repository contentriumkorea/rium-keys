# RIUM Keys: Windows input-layer redesign

User brief: Korean remains selected; editable controls compose Korean immediately;
non-text controls receive physical shortcuts immediately across applications.
Keep the installer, tray-only controls, startup and signed automatic updater.
No new application-name, panel-title or tool-name rules.

## Decision and feasibility gate

The current out-of-process UI Automation poll plus synthetic WM_KEY messages is
not a universal input method. It cannot distinguish virtual controls whose native
HWND is shared, and stale observations cause the first key after transitions to
bypass correction. Broadening its allowlist would corrupt text input.

The candidate replacement is a native TSF Korean text service. It would own Korean
composition and decline shortcut keys through ITfKeyEventSink, without switching
Microsoft IME state, polling UIA or synthesizing messages. A keyboard driver cannot
infer whether a key is text. Adding a background TSF observer beside Microsoft IME
also does not make Microsoft IME decline keys.

Before implementing a replacement IME, measure whether native TSF signals identify
text/non-text transitions in supported and legacy IMM-compatible hosts. A context
can exist without corresponding to a visible editable field. Read-only, empty and
disabled flags alone must not be called a universal classifier.

## Diagnostic deliverable

Build an isolated, process-local, non-consuming TSF diagnostic TIP. It records
only context identity, status, compartment availability/values, native window ID,
thread/process ID and callback timing. Existing external applications are outside
the first fixture's scope. No virtual-key values, strings, document
contents, window titles or passwords are recorded. It never inserts text, toggles
conversion, sends keys or becomes the default input profile. It is not a Korean
IME and must not be shipped through automatic update.

Tests: real TSF context with missing, zero, nonzero, invalid and failed compartment
reads; registration cleanup; activation/deactivation; callback pass-through.
Compare identical observable tuples in text and non-text states. One conflicting
pair falsifies automatic classification based on those signals.

## Replacement implementation after the gate passes

1. Deterministic two-beolsik composition engine with compound vowels/finals,
   splitting, backspace, selection replacement and commit/cancel tests.
2. TSF edit sessions and composition ownership. Context loss commits to the original
   context only; unavailable context passes input without replay. Modifier shortcuts
   are not consumed. No context from another app may be reused.
3. 32/64-bit registration, user language-profile selection via Windows APIs, rollback
   on partial registration and uninstall. Preserve existing input methods.
4. Tray controls and updater migration. No hook/UIA worker in the new input path.
   Do not hot-replace a DLL loaded in user applications; use versioned paths and
   apply activation at a safe restart boundary.
5. Real Korean input and first-key tests across Win32, WPF, Chromium/Electron,
   Premiere and After Effects. Password, rename/search, tab/Alt-Tab, held keys,
   composition cancellation, app crash and uninstall recovery are release gates.

## Release truth

No claim of all-app coverage follows from compilation or fixture tests. Apps that
expose indistinguishable states need app cooperation or an explicit user mode;
neither is silently substituted for the requested automatic design. Production
1.1.1 remains unchanged while this feasibility gate is open.

References:
- https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements
- https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-compartments
- https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeyeventsink-ontestkeydown
- https://learn.microsoft.com/en-us/windows/desktop/api/Imm/nf-imm-immdisabletextframeservice
