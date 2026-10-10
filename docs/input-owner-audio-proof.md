# Paired audio timeline owner proof

Scope: exact Premiere image SHA256 `b24e2b442a8e0350edbc0c214d2fcae2f68c7d9843ae399e5058d5b5a6786094`. Static file inspection only; no application calls or new live input.

| First two delegates | Vtable RVA | COL RVA / offset | Type descriptor RVA |
|---|---|---|---|
| AudioTrackContentView@HandlerTimeline | 272E9598 | 28491FD8 / 0 | 2CBFC7D0 |
| AudioTrackView@HandlerTimeline | 272E9A18 | 284920F8 / 0 | 2B2B35B8 |

Both vtables have the exact same SubView-dispatched slots:

- `+168` keydown -> `1E912F10`
- `+170` keyup -> `1E912F20`
- `+178` character -> `1E912250`

Each target begins and ends with bytes `32 C0 C3` (`xor al,al; ret`). These are the same literal-false leaf implementations used by the previously audited video pair. The contract adds all six relocated slot expectations and all three complete leaf bodies. It adds both exact RTTI contracts; names alone never authorize a route.

Only the complete audio/audio pair is accepted alongside the previous video/video pair. Both mixed pairs fail. The other eight ancestors, native tab binding, parent tokens and membership, callbacks, filters, main-thread/executor guards, capture stability, loaded contract and unmodified A-Z domain remain unchanged.

## Evidence boundary

Source capture: `native-ime/out/composition-metadata/timeline-staged-independent.log` (root's authorized read-only observation). `owner-command-audio-timeline-witness.inc` preserves its ten ancestors and 119 filters and records its SHA256. The old observer reported both audio delegates unsupported with missing type descriptors; this unaltered witness remains UNKNOWN.

The positive fixture explicitly enriches only those two type descriptors/COL offsets/states using the exact disk RTTI proof above. That fixture itself is a candidate interpretation of the recorded route. No observed pointers, tokens, filters, focus or other metadata are invented.

On 2026-10-11, a fresh Premiere process loaded installed preview.6. After G/K/S
formed `한` in Project search, clicking the same audio track area and pressing
physical P selected Pen. The installed service reported COMMAND, with no live
composition. P has no Korean alternate in this test configuration. This is a
live pass for this specific route. Returning to text exposed a separate empty
boundary bug, documented in [input-owner validation](input-owner-validation.md).

## Regression verification

- Runtime/policy fixture: 46 checks, 1 failure before extension; 46/0 afterward. Original unsupported observation stays UNKNOWN; enriched audio pair COMMAND; mixed pairs, wrong type, wrong dispatch and unverified loaded spans UNKNOWN.
- Metadata fixture: 77 checks, 6 failures before adding the six relocated audio-slot guards; 77/0 afterward. Every modified key slot is rejected.
- Actual-source Capture coordinator: see `native-ime/out/review-boundary/audio-capture-green.log` for the independent existing regression run.

Logs: `native-ime/out/review-boundary/audio-{red,green,slots-red,slots-green}.log`.

The generated contract header contains the reviewed audio additions; future regeneration with the ignored research generator must retain these two types, six slots and three leaf spans. No runtime, build, version, IME composition or installed files changed in this patch.
