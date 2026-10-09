# RIUM Keys context probe — development only

This is a non-consuming diagnostic TSF service, **not a Korean input method** and
not an upgrade to the shipping application. It must not enter the automatic updater.
See `../docs/system-input-design.md` for the architecture and release gates.

Build on the current Windows development machine:

```powershell
./input-probe/build.ps1
./input-probe/build.ps1 -Architecture x86
./input-probe/out/x64/RiumContextProbe.exe --self-test
./input-probe/out/x64/RiumContextProbe.exe --registration-test
./input-probe/out/x64/RiumContextFixture.exe --registration-test
./input-probe/out/x64/RiumContextProbe.exe --cleanup-registration
```

`--self-test` checks real COM compartment reads (missing vs explicit zero/nonzero),
null-owner errors, factory lifetime and non-consuming callbacks. A manual
ActivateEx result is printed as a diagnostic, not claimed as OS activation.

`--registration-test` requires keyboard-category registration rights. It creates
a temporary machine COM entry, diagnostic TIP identity and keyboard category, registers a **process-local**
profile, activates it only inside the controller and checks both active keyboard
profile identity and an actual service callback. A success HRESULT alone is not
considered success. It restores the process-local profile and checks registration
cleanup. Registration refuses to overwrite an existing class entry.

After abrupt controller termination, run `--cleanup-registration` with the same
architecture and registration rights. It refuses an active diagnostic mapping or
a COM DLL path different from the DLL beside the controller; it removes only the
diagnostic GUID's TIP identity, category and COM entry. Process-local
profiles end with their host process. An activation callback is not proof of real
keyboard delivery; key-event callbacks and editable/non-editable contexts still
need their own integration fixture.

There is deliberately no session-wide activation command. This controller cannot
inspect an existing external application. No UI Automation scan, key injection,
IME conversion-mode write, default-keyboard change or text logging is implemented.
The metadata slot includes context identity, compartment type/result/value, native
window ID and callback event type; it does not include key values or text.

The build currently uses the installed VS18 MSVC14.51 and Windows SDK10.0.22621.
Outputs are isolated under `out/x64` and `out/x86`; no installer changes are made.

## Current integration runner

Run `./input-probe/test-registration.ps1` from a **non-administrator** PowerShell.
It requests UAC for registration only, then starts the disposable fixture as the
ordinary user. Named events coordinate a bounded registration lifetime; the
elevated process removes the temporary registration even if the fixture fails.
Do not run overlapping instances. The older direct `--registration-test` runner
is not the current integration entry point.

Registration uses `RegisterProfile` and `InstallLayoutOrTip`; it is temporary
machine/user registration, while **activation is process-local**. Those are
different scopes. It never makes the diagnostic the default keyboard.

Before readiness polling, the fresh standard-user fixture resolved the service, keyboard
category and language-profile description, but `EnumLanguageProfiles` ends
normally with two other profiles and omits the diagnostic. `GetProfile` returns
E_FAIL and the service activation callback is never entered. Cache invalidation,
COM description and Windows-app compatibility registration have not resolved
this failure on their own.

The registration readiness test now polls `GetProfile` with message processing
for up to three seconds. On 2026-10-10, an untraced run became ready after 250 ms,
selected the diagnostic profile, entered `ActivateEx`, and registered the key
sink with S_OK. Another run became ready after 265 ms. The earlier immediate
selection was racing profile-list propagation. This is a registration/fixture
fix, not evidence that the original production application's focus bug is fixed.

The latest fixture separately requires a key callback through the system's
`ITfKeystrokeMgr::TestKeyDown`. This is API dispatch testing, not physical-key
delivery. A prior interactive attempt timed out during focus changes and did
not demonstrate a key callback. Real input and Adobe coverage remain open gates.

Machine COM/TIP keys are removed on normal teardown. Windows leaves a disabled
HKCU entry for the exact diagnostic profile; this is not an installed service.
The runner must distinguish that metadata from live machine registration.

## Verified on 2026-10-09

- x64 and x86 DLL/controller build and self-test passed after cleanup changes.
- Global TIP registration returned E_FAIL; the per-user COM entry was removed.
- Process-local RegisterProfile/ActivateProfile returned S_OK, but without a
  registered keyboard category the active keyboard profile did not match and no
  activation callback was observed. This is a **failed integration test**.
- Keyboard-category registration returned E_FAIL. Opening the system CTF TIP
  registration root for write separately raised SecurityException in this host.
- Real Korean composition, Adobe contexts and first-key shortcut behavior have
  **not** been validated. A replacement IME must not ship on this evidence.

## Elevated diagnostic on 2026-10-10

- User authorized launching the administrator confirmation. Elevated TIP and
  keyboard-category registration succeeded.
- Elevated COM activation returned CLASSNOTREG with per-user registration; moving
  this diagnostic's temporary COM entry to the machine scope yielded S_OK.
- A visible, foreground Win32 EDIT fixture was verified by the controller. Even
  after pumping messages, process-local profile activation did not match the
  requested keyboard profile and no service callback arrived. The integration
  check remains failed; no real Korean composition or shortcut coverage is claimed.
- Normal cleanup removed this diagnostic's machine COM and CTF TIP keys, verified
  separately. The shipping application was not modified or updated.
- `--registration-test` writes `registration-result.log` beside the controller so
  results survive the UAC launch without a console window.
- Both console and GUI-subsystem fixtures report thread-manager active flags
  `0x80000001` (including NOACTIVATETIP) and fail the activation check. Merely
  changing the executable subsystem or foregrounding the edit window did not fix
  the diagnostic. This is a fixture/runtime issue to resolve before app testing.
