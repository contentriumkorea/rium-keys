# Exact AE current-owner provider

Provider 3/profile 1 accepts only the seven exact audited x64 module builds in
`ae-owner-contract.h`. It is a current canvas/timeline owner adapter, not a general Adobe
or Windows keyboard policy. It does not call Adobe functions, read entered text,
send keys, change language/focus, install hooks, or create threads.

`RiumAeOwner_Initialize()` belongs on the embedding TIP's verification worker,
outside DllMain and key callbacks. It checks already-loaded paths, disk hashes,
PE metadata and selected loaded spans, retaining read-only file handles until
`RiumAeOwner_Shutdown()`. It never loads a host module. Shutdown must run outside
DllMain while the embedding TIP owns code lifetime.

`RiumAeOwner_Capture(void*, RiumOwnerStamp*)` runs on the focused UI thread. It
uses two equal snapshots, exact native focus/foreground scope, loaded module and
span checks, the framework focus token/mutation state, and native HWND reciprocal
ownership. Reads are capped at 4096 operations/64 KiB with a 15 ms QPC budget
checked at read boundaries and after capture; synchronous Windows calls cannot
be forcibly interrupted. Disk hashing is never performed during Capture.

The audited canvas path is `CDirProjItem -> CPanoProjComp -> CComposition ->
BEE_CompItem -> BEE_Project -> undo/current transaction`:

- An explicit null current transaction yields COMMAND for the unmodified Latin
  A-Z virtual-key domain. Unsupported/nontext transactions and failed reads do
  not become COMMAND.
- An exact text transaction with an audited BEE_Layer/BEE_TextLayer in the same
  current composition yields conservative TEXT. This corresponds to the owner
  guard of `HasActiveTextEdit(false)`. The time-conversion condition of the true
  overload is not needed for this positive protection; the production collector
  reads no time, offset, stretch, or item-marker fields.
- Every unsupported/mismatched/changed sample yields UNKNOWN.

The separate timeline path requires the exact focused `CTLDir`, native HWND
reciprocity, a current direct `CComposition` from its audited context reference,
and the same BEE project/undo relationship. Only an explicit null transaction
yields COMMAND. A native Edit or unreadable native class is excluded before
reading that context. Any nonnull transaction, CLayer context fallback, rename
node, search control, or other build remains UNKNOWN; a text transaction in the
timeline's composition does not prove that the timeline owns text editing.

The TEXT stamp includes the native DVA object, text layer, current transaction,
and framework focus token. COMMAND has no text target. Failed samples retain
provider/profile identity with cleared object fields. `deferredSafe=0`: these
numbers are immediate observation identities, not retained references or
permission to replay a delayed edit or rebase an old owner epoch.

The root-controlled v4 observer pair distinguished selection COMMAND from a
visible text caret TEXT in the same actual canvas node/token, with equal
metadata and clean hook/module removal. The v5 pair distinguished ordinary
timeline COMMAND from an actual native Edit rename node UNKNOWN, with both
snapshots stable and cleanly unloaded. The product collector omits observer-
only time reads. The 79 core/stamp and 7 own-process lifecycle tests cover the
limited predicates and cleanup, including the sampled rename node with a
timeline ancestor and stale timeline data; they create no windows. Physical Korean-key and
composition-transition QA of the integrated candidate remains required.

Build inputs: `ae-owner-core.cpp`, `ae-owner-runtime-policy.cpp`, and
`ae-owner-runtime.cpp`, x64 C++17, bcrypt/user32. Core tests link the first two
with `ae-owner-fixture.cpp`; lifecycle tests link all three with
`ae-owner-lifecycle-fixture.cpp`. Shared registry/build ownership is separate.
