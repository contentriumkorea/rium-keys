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
./input-probe/out/x64/RiumContextProbe.exe --cleanup-registration
```

`--self-test` checks real COM compartment reads (missing vs explicit zero/nonzero),
null-owner errors, factory lifetime and non-consuming callbacks. A manual
ActivateEx result is printed as a diagnostic, not claimed as OS activation.

`--registration-test` requires keyboard-category registration rights. It creates
a temporary per-user COM entry and keyboard category, registers a **process-local**
profile, activates it only inside the controller and checks both active keyboard
profile identity and an actual service callback. A success HRESULT alone is not
considered success. It restores the process-local profile and checks registration
cleanup. Registration refuses to overwrite an existing class entry.

After abrupt controller termination, run `--cleanup-registration` with the same
architecture and registration rights. It refuses an active diagnostic mapping or
a COM DLL path different from the DLL beside the controller; it removes only the
diagnostic GUID's category and COM entry and verifies both are absent. Process-local
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
