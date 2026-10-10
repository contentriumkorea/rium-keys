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
- [x] Review before elevation. Install locally, verify default/active profile,
  both DLL hashes, old-process/startup/uninstall removal and new uninstall entry.
- [x] Exercise the installed DLL through an isolated native control fixture.
  Do not claim Adobe or all-app compatibility from this result.

Failure focus: wrong user after elevation; second-architecture failure; locked
DLLs; partial legacy uninstall; user profile selection failure. All must fail
visibly with recovery information rather than reporting completion.

2026-10-10 outcome: `2.0.0-preview.2` installation and removal of the legacy
utility completed. Fresh verification confirmed the default/active native
profile, six categories, enabled state, both COM architectures, all package
hashes, installed-apps entry and `Installed` transaction status. The installed
x64 DLL passed the physical EDIT / V / EDIT test. One initial Right Alt toggle
was necessary to enter Korean; the subsequent transition retained Korean without
another toggle. Initial mode synchronization and all-app/Adobe compatibility
remain unverified release gates.

Before installation, the original renamed fixture reproduced Windows error 740.
Its missing embedded manifest was corrected with `asInvoker`/`uiAccess=false`;
real launch/unelevated-token regression tests pass for x64/x86. Staged launch is
now checked before elevation. Rollback restores the original profiles after an
enable attempt too and records the readback. The preview.1 failed-attempt files
are retained; preview.2 uses a separate version directory. Fresh preview.2 x64/x86
builds each passed 26 engine and 42 routing/policy checks, and the changes passed
source review and PowerShell syntax checks.

References: Microsoft ITfInputProcessorProfileMgr::ActivateProfile,
InstallLayoutOrTip and SetDefaultLayoutOrTip documentation.
