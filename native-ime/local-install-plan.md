# RIUM Keys local IME installation

The user explicitly requested replacing the installed 1.1.1 tray bridge with
the native input method on this PC. This authorizes a local preview installation;
it does not turn the unresolved all-application compatibility gate into a pass.

## Design

Keep the fixture-only build as the default. Add an explicit installable build
in a separate output directory, using the same Korean engine and routing.
Isolate configuration and registry state from upstream Jamotong. Limit controls
to the Windows input-indicator menu; no upstream settings window or extra
Ctrl/Alt shortcuts. Keep Microsoft IME available. Windows loads the registered
input method on demand, including after login, without the old hook/UIA worker.

Use versioned Program Files x64/x86 DLL paths. Machine registration runs once
through Windows UAC; user enable/default selection runs as the original user.
Save the original default and legacy installer/settings before mutation. Stop
the legacy bridge, register and verify both architectures, select the native
profile, then uninstall the legacy bridge. Roll back failures to the captured
default and restart the old bridge if it is still installed. Do not overwrite
loaded DLLs, kill user applications, remove other input methods, or publish this
preview through the production updater. The preview has no automatic updater.

## Work and verification

- [x] Add failing real-DLL tests for helper/shortcut isolation, then implement
  policy, config/registry namespace isolation and tray-only menu.
- [x] Build fixture and installable x64/x86 outputs; run engine/routing tests.
- [x] Add native profile control and bounded transactional install/uninstall
  scripts. Verify read-only preflight, both registry views and profile identity.
- [ ] Review before elevation. Install locally, verify default/active profile,
  both DLL hashes, old-process/startup/uninstall removal and new uninstall entry.
- [ ] Exercise the installed DLL through an isolated native control fixture.
  Do not claim Adobe or all-app compatibility from this result.

Failure focus: wrong user after elevation; second-architecture failure; locked
DLLs; partial legacy uninstall; user profile selection failure. All must fail
visibly with recovery information rather than reporting completion.

2026-10-10 outcome: code review found no remaining concrete installation blocker.
Both guarded and installable x64/x86 builds passed 26 engine and 42 routing/policy
checks per build. Two UAC attempts were cancelled before machine registration.
Rollback readback confirmed no native registration or version directory, the
original Microsoft default/active profile, and the legacy 1.1.1 utility running.
The actual installation/installed-DLL physical verification items stay open.

References: Microsoft ITfInputProcessorProfileMgr::ActivateProfile,
InstallLayoutOrTip and SetDefaultLayoutOrTip documentation.
