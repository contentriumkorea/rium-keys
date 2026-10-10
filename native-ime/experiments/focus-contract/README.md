# Focus contract snapshot (diagnostic only)

This executable reads GUI-thread and MSAA metadata for one supplied process/window.
It does not install hooks, register an IME, inject keys, invoke accessibility actions,
set focus/selection, or read accessible names, values, document text or window titles.
The default mode reads MSAA; the separate `--uia` mode reads UIA patterns.

Build and test only hidden controls owned by the diagnostic:

```powershell
& .\native-ime\experiments\focus-contract\build.ps1 -Test
```

One real-app snapshot (operator supplies the current PID and main HWND):

```powershell
& .\native-ime\out\focus-contract-probe\focus-contract-snapshot.exe <PID> <MAIN-HWND> 3000
& .\native-ime\out\focus-contract-probe\focus-contract-snapshot.exe --uia <PID> <MAIN-HWND> 3000
```

HWND accepts decimal or `0x` hexadecimal. The optional whole-process timeout is
250–10000 ms, default 3000 ms. A watchdog terminates only the diagnostic with exit
124 if any provider/COM call blocks. This does not promise cancellation of a
provider call already executing in the target process.

Output is JSON Lines: target/foreground metadata, before/after `GetGUIThreadInfo`,
the global caret object (queried with a NULL HWND only while the target owns the
foreground and GUI focus), and at most eight MSAA nodes per `OBJID_CLIENT` focus
chain from the main and focused windows. Each node reports only numeric role,
state, location and HRESULT/VARIANT types. A focused simple child is read through
its parent's child ID. Resolved foreign-process objects are not traversed.
Objects whose owning HWND cannot be resolved are explicitly marked unknown.

UIA mode runs in an MTA and inspects, in order, the GUI focus HWND, the current
UIA focused element (only when the target was foreground), then the main HWND if
different. Every element must report the supplied PID before further properties
are read. It queries only numeric control type, keyboard-focus and pattern
availability, actual TextPattern2 `GetCaretRange` active status, the caret range's
`UIA_IsReadOnlyAttributeId`, and ValuePattern `CurrentIsReadOnly`. It never requests
text, document/selection ranges, the Value property, names or titles. It does not
enumerate the tree, subscribe to events or invoke any accessibility action.

Boolean property reads use `GetCurrentPropertyValueEx(..., TRUE, ...)` to avoid
UIA default substitution. The standard reserved unsupported and mixed objects are
compared by canonical IUnknown identity. `unsupported`, `mixed`, `query_failed`,
`empty` and unexpected types each produce an explicit status and JSON `null`.
Only a successful supported BOOL is printed as true/false. Pattern acquisition
reports HRESULT and object presence independently; S_OK with no pattern object
does not mean support. IAccessibleEx/IA2 are not queried in this diagnostic.

Every potentially blocking COM operation is preceded by a `call` record. On a
timeout, partial output identifies the last operation reached. Non-numeric role
or state variants are not interpreted or read as strings. A failed/unsupported
property is not `false`, and no reported focus/caret is not proof of non-editing.

`consistent_endpoints=true` only means the sampled foreground, focus, caret HWND
and target identity match before/after. It cannot exclude a round trip or a
same-HWND logical focus change during the query. Inactive-window metadata and a
global caret object are not evidence that the app currently accepts text. This
diagnostic deliberately produces no shortcut-routing or composition-cancel verdict.

Exit codes: 0 completed snapshot/self-test, 2 bad arguments, 3 invalid target or
unavailable GUI thread, 4 setup failure, 5 self-test failure, 6 changed endpoints,
124 watchdog timeout. A completed snapshot may contain failed MSAA queries.

`--self-test` creates hidden EDIT, read-only EDIT and STATIC windows, checks their
role/state, exercises focus-chain queries, rejects an invalid target, then destroys
those windows. It never shows or focuses them and never targets another app.

`--self-test-uia` creates its hidden controls on a dedicated pumping UI thread so
the MTA client can inspect its own providers without a UI-thread deadlock. It tests
the actual reserved unsupported/mixed objects, failed-query preservation and native
EDIT/STATIC control types, plus ordinary versus read-only EDIT ValuePattern state.
On the tested Windows installation, hidden standard EDIT does not expose
TextPattern2, so this fixture verifies the unsupported branch, not a live positive
GetCaretRange result. `build.ps1 -Test` runs both suites. Neither suite activates,
focuses, shows or types into a window.

## Bounded event observer

`event-observer.cpp` is a separate diagnostic for events that a point-in-time
snapshot may miss. Build and test it without targeting another application:

```powershell
& .\native-ime\experiments\focus-contract\build-event-observer.ps1 -Test
```

For an explicitly selected process and main window:

```powershell
& .\native-ime\out\focus-contract-probe\event-observer.exe <PID> <MAIN-HWND> 60
```

It subscribes to PID-filtered out-of-context MSAA focus/caret events, globally
delivered UIA focus events filtered by the sender's cached/current PID before
reading other properties, and UIA text-selection events in the target subtree.
It reads no names, values or text and performs no input, focus, selection or
settings actions. IA2 discovery uses the published service/interface UUIDs and
queries interface presence only; it never guesses an IA2 method table.

The observation limit is 1–60 seconds, at most 128 accepted events, 512 owner
checks and 4096 metadata calls. A watchdog terminates only this diagnostic after
the requested duration plus five seconds. It cannot cancel a call already running
inside a provider. Late callbacks retain reference-counted state. Teardown reports
each unsubscribe result, active callback count and cleanup failures.

`busy_dropped` reports callbacks skipped while another metadata query is active.
Zero selection/caret events therefore never proves a non-text context. Delivery
is asynchronous and cannot guarantee classification before the first key. The
500 ms UIA connection/transaction limits do not bound all MSAA provider calls.

The callback fixture passes 19 checks. The separate hidden-provider test receives
an actual Windows UIA selection event and MSAA bridge event, verifies unchanged
foreground, and removes all handlers. It explicitly generates its own event and
does not test natural application focus or editing intent. The watchdog test must
exit 124. Live execution produces metadata, not a routing-policy pass/fail verdict.
