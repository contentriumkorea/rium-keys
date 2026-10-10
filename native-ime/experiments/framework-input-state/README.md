# Framework input-state research

Diagnostics only; see [cross-language findings](../../../docs/cross-language-shortcut-research.md).
These tools are absent from product packages, startup and automatic updates.

## CCL exact-binary observation

`ccl-readonly-probe.py` uses process query/read rights only. It does not load code
into the host, call a host function, attach a hook, send keys, change focus or IME
modes, or read text. Each invocation exits after one sample. Use 64-bit Python:

```powershell
python .\native-ime\experiments\framework-input-state\ccl-readonly-probe.py `
  --pid <observed-target-pid> --label <observed-state> `
  --output .\native-ime\out\ccl-input-state.jsonl
```

The caller supplies an observed PID. The output directory must exist. Labels are
operator observations, not programmatic proof. Exit 1 and `state: unknown` preserve
missing/incompatible/failed observations. Success is not a routing verdict.

Only CCL GUI 4.0.3 with SHA-256
`A7ADF05DC29C9FD045C4803922D1C25825882BB17024790976FD3D77D0392A4B` is verified.
Static inspection and read-only loaded pointer inspection established:

| Value | RVA |
| --- | --- |
| Exported GetDesktop | `0x33B890` |
| Returned static interface | `0x52EC30` |
| Interface vtable | `0x42B028` |
| Installed isInMode function, slot 14 | `0x33C590` |
| Signed counter tested by mode 8 | `0x55B414` |

That branch returns true for counter > 0. The public current CCL source calls
the corresponding state `kTextInputMode` and uses `EditBox::isAnyEditing()`.
No CCL source or DLL is redistributed. The current header ABI is not used to
call this older DLL. Never reuse these addresses for another binary by name alone.

Before reading the counter the probe checks the full file hash, loaded getter
bytes, vtable and method address. State can change between reads. This counter is
framework-wide, not bound to current foreground/text ownership; neither zero nor
positive is a universal routing decision. No policy reads this output.

Local Studio One QA observed name field 1 -> cancelled dialog 0 and browser search
1 -> workspace 0. The report excludes an initial failed menu click. No text was
entered and no project saved. The other-framework check returned unknown.
An incompatible-digest negative control also verified rejection before any
process-memory data read. Python compilation passed; these checks do not exercise
shortcut dispatch or composition.

The raw six-sample log `native-ime/out/ccl-input-state-live1.jsonl` has SHA-256
`22630CF30118A876D42677717480BFDB4956AB682929E100EC0F82525794A021`.

Do not put external module enumeration/hash checks in a key callback. A product
adapter still needs verified ABI, GUI-thread ownership, current text-target
lifetime, and real first-key/composition/third-party-plugin tests.

## Windows InputScope observation

Mozc queries `GUID_PROP_INPUTSCOPE` on the current TSF selection. The independent
`input-scope-*` diagnostic tests that metadata path; it does not copy Mozc code.
It reports HRESULTs, VARIANT type and numeric InputScope enums only. It does not
request phrases, regular expressions, XML, document text or keyboard events.

Build and exercise an owned TSF document with the repository's research toolchain
(prepared by `native-ime/bootstrap-toolchain.ps1` if it is not present):

```powershell
.\native-ime\experiments\framework-input-state\build-input-scope.ps1 -Test
```

The core uses one `TF_ES_SYNC | TF_ES_READ` edit session. It verifies process,
thread, document and top-context identity before and after provider calls. The
external observer additionally requires the exact native focus HWND to remain
owned by that thread. A heap COM session owns its data; any unexpected retained
session is disarmed and pins its module, and the run is reported as abnormal.

`known=1` means only that a scope enum was read successfully. Even `IS_DEFAULT`
is metadata, not evidence of text ownership. Missing, malformed, failed, expired
or changed-owner observations remain unknown. No keyboard-routing policy uses
the output.

The hidden fixture owns a real Windows TSF document and checks numeric/default,
missing/empty and failing properties, reference cleanup, and absence of text reads
or writes. Its successful metadata cases deliberately omit native HWND focus
validation: they are not evidence that an external application's focus was tested.

The external controller accepts one explicit PID, a HWND observed in that process,
an absolute path to the built DLL, and an optional 1–10 second deadline. It attaches
a temporary `WH_GETMESSAGE` hook to that GUI thread for one private message;
ordinary input messages are left unchanged. It reads only an already active
thread manager and balances the temporary TSF client registration. It does not
install or select an input service.

After selecting and verifying the intended application and state:

```powershell
$scopeDll = (Resolve-Path '.\native-ime\out\input-scope-probe\input-scope-probe.dll').Path
& '.\native-ime\out\input-scope-probe\input-scope-probe.exe' <PID> <HWND> $scopeDll 5
```

The implementation requires `ITfThreadMgrEx::GetActiveFlags` success and
`TF_TMF_ACTIVATED` before acquiring a temporary client. It preserves restrictions
and uses `NOACTIVATETIP | NOACTIVATEKEYBOARDLAYOUT`. Nested activation may return
`S_FALSE`; successful activation is still balanced exactly once. Final flags
must equal their initial value. The private message is consumed before any COM
call can reenter a message loop and reuse its storage.

The controller deadline bounds its wait, not the duration of a target provider's
COM call. Timeout reports and cleanup fields must be retained; a stuck callback
cannot safely be cancelled inside the target process. Do not run this probe as
a background service or in a key callback.

Exit 0 means the attempt completed and cleaned up; `known=0` may still be the
correct metadata result. Exits 2/3 reject arguments/target before hooking; exit 6
reports incomplete execution or cleanup. Do not interpret exit 0 as text intent.

Verification: 45 owned TSF checks and four own-thread hook cleanup cases passed
(empty focus, mismatched focus, wrong token and timeout). DLL load/export/unload
passed. A separate hidden native-focus host returned `SKIP77`, not a successful
external transport observation. The build does not connect to external apps.

The root subsequently performed four one-shot reads in its already-open Premiere
QA project: timeline, search, caption editing and return to timeline. All four
executed one actual provider read, preserved active flags, and unhooked cleanly.
All returned `S_OK / VT_EMPTY`, so all remain unknown. The screenshot-confirmed
states had different focus HWNDs; no text was entered and the project was not
saved. See the cross-language report for the observed states and limitations.

Recorded probe DLL SHA-256:
`A81069FFF50036FB8664E8A8BDBBBCC8EE5A8DFE0EE0EDEBDF55EB943B2CC940`.
Local logs are in `native-ime/out/input-scope-probe/`; the raw logs and compiled
diagnostics are intentionally excluded from the Git source and product payload.
