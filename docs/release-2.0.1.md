# CONTENTRIUM Keys 2.0.1 verification

Date: 2026-10-11. Environment: Windows 11 x64, ordinary user session.

## Change

An upgraded input profile could keep showing the previous Korean text icon in the Windows input switcher even when the registry already pointed to the CK resource. Updating registry values alone did not reliably refresh the displayed brand.

- The installer now re-registers the owned profile through `ITfInputProcessorProfileMgr::RegisterProfile`, with the installed DLL's CK resource 100 and the complete CONTENTRIUM Keys name. It preserves the machine default-enable value, user enable state, capability flags and input selection.
- Both architectures verify the registered name, DLL path and resource ID, then extract the large and small CK icons using the Windows shell API. The new check is used by fresh installation, upgrade and same-version setup.
- Branding is staged with COM registration. The ordinary-user branding, loading and input-state checks run before upgrade commit; rejection follows the existing rollback path, including restoration of the original brand through the profile API.
- The ordinary-user process requests an asynchronous shell icon-cache refresh. Setup offers a Windows restart with **Later selected by default**. Silent setup never restarts Windows and returns **3010** on success; read-only preflight returns **0**.
- Resource 100 remains the white transparent **CK** product icon. The separate **가 / A** input-mode icons and all routing/composition code are unchanged.

## Verification

- The candidate DLLs passed **25 native suites, 1,506 assertions, zero failures**.
- **34 installer contract checks** passed under PowerShell 7 and Windows PowerShell 5.1, including failed-gate package preservation and aliased registry-key branding rollback. These rollback contracts use disposable HKCU keys; they are not a new live machine failure-injection test.
- The new installed-branding integration test passed in both shells: x64/x86 icon extraction, rejection of a foreign DLL path, rejection of unelevated re-registration, and unchanged profile metadata/selection. The test also refuses an elevated session before launching any controller.
- Independent code review identified and resolved the post-commit verification ordering and elevated-test safety issues before the final installation. The final review found no blocking issue.

## Actual installer execution

- The exact distributed Setup EXE upgraded this PC from **2.0.0 to 2.0.1 at 09:49 KST**. Windows PowerShell 5.1 performed the installation. The installer returned **3010**, indicating successful installation with restart deferred.
- The machine log confirms successful profile-API brand registration and preserved profile state. No application, Explorer process or computer restart was forced.
- All **13 installed payload files** match the package hashes. Both COM architectures load the 2.0.1 service from the correct registered path and expose the required interfaces. Both controllers extract the registered CK icon successfully.
- The installed-app entry and user recovery pointer agree on 2.0.1. Registered/enabled status, six categories, active profile, Korean default, system default and active TIP all match the pre-upgrade snapshot. The original Microsoft input-method fallback is preserved.
- Running the same Setup EXE again with `/S` returned **3010** and `ALREADY INSTALLED`, after loading and branding checks. `/S /PREFLIGHT` returned **0**. Neither repeated operation requested machine registration or created another installation.
- The legacy `update.json` is byte-for-byte identical to the 2.0.0 release asset. Its RSA signature verifies against the repository's public key and its payload still targets legacy 1.1.1.

## Distributed file hashes

| File | SHA-256 |
| --- | --- |
| CONTENTRIUM-Keys-Setup.exe | `ace6c11d66925f6c519b632388e5f373343d945ebf328d562c03e5461bfef8ae` |
| x64/RiumKeysInput.dll | `e4831d703a8bcc9a5d1a4fd06ca1dad1a0b0ce63b1fe3ae9cd3cc8788cc00262` |
| x86/RiumKeysInput.dll | `5370ae887039c23dc618a454a8ac627a8f9ed41d35edf483796eb313cdd3b233` |
| update.json (legacy compatibility) | `dfb65c69eca116306a9f325618b3e3fe1fdc1168e09d23bc3ec91bffae11d8f0` |

## Scope

Before packaging, the user confirmed CK appeared on this PC after the same profile-API repair and a shell restart. That establishes the observed repair sequence, not that the API call alone refreshed every running shell. The final installer adds cache notification and a restart choice instead of stopping Explorer automatically.

This release was installed on one existing Windows 11 installation. A separate clean PC and post-reboot visual check were not available in this run. Existing application evidence remains in [input-owner-validation.md](input-owner-validation.md); this branding patch does not add new application compatibility claims. Code signing and automatic updates for the native input method remain outside the release scope.
