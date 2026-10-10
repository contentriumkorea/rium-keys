# Logical input ownership

The native TSF service retains Korean mode. A verified command surface receives
the original key before composition processing; the provider does not synthesize
keys, change layouts, or call the application's command handlers. Ordinary TSF
restrictions remain the default for processes without a verified provider.

The current x64 providers recognize these exact installed builds:

| Framework | Application build | Positive text evidence | Command evidence |
| --- | --- | --- | --- |
| DVA | Premiere Pro 26.5.2.5 | Standard native EDIT bound to a live DVA window; active caption text selection bound to the current monitor component | Verified timeline focus ancestry; bound selected graphic in Selection mode with absent text endpoints and verified property defaults; ordinary unmodified A-Z only |
| CCL | Studio One 6.6.4.102451, CCL 4.0.3 | Native EDIT with reciprocal CCL control ownership | Verified workspace control and global handlers for ordinary unmodified A-Z |
| AE DVA/BEE | After Effects 26.5.0.89 with seven audited module hashes | Text transaction bound to the focused composition's text layer | Explicit absence of a transaction in a verified composition canvas or current timeline; native Edit and other focused types excluded; ordinary unmodified A-Z only |

These contracts are deliberately narrower than all-application compatibility.
The selected-graphic proof describes the current input owner. It does not call
an application key handler or claim that later application commands are pure.
After Effects uses its own audited current-owner path; it does not inherit
Premiere's private memory layout. Other builds and x86 retain ordinary TSF.

## Lifetime and latency

Activation starts one background verification job. File hashes are checked once,
away from keyboard callbacks. A service adopts a ready provider only before a
new key when no composition, pending commit, delayed resend, or edit session is
active. Finishing verification cannot change ownership midway through typing.
If a clean input starts without positive ownership, it retains ordinary TSF for
that composition. A later positive sample is adopted only after that input has
finished. A recognized transaction that loses its proof cannot start a new
UNKNOWN-bound write or fall back midway through typing.

Keyboard-time reads are local, bounded, and nonblocking on internal locks. They
read identities and control state, not entered text. They compare fresh native
focus and framework ownership; no process-name-only, HWND-only, or UIA result
cache grants command status. Unknown or changed evidence cannot authorize an
old text operation on a new target. No provider authorizes deferred writes.

Every owned text operation retains its original binding and rechecks it after
reentrant host calls. A successful text write is not retried merely because
focus changed afterward. The CCL reader has no object-generation source and
does not claim protection against unobserved address reuse.

## Verification scope

`build.ps1` builds provider fixtures and the actual-service ownership, inline,
edit-session, pending, resend, routing, and runtime lifecycle regressions.
`test-package.ps1` requires all candidate suites on both architectures; provider
fixtures run only on x64. The DVA fixture additionally checks a Windows
Common-Controls v6 EDIT after its activation context has been restored, and
rejects an application-owned class merely named `Edit`.

Passing these contracts is not evidence that physical typing works in an
application. Installation, the actual loaded DLL path, original shortcut
delivery, uncommitted text transitions, and return-to-text behavior require
separate application tests before release. The installed preview.8 Premiere and
Studio One functional results, remaining After Effects checks, and precise limits
are recorded in [input-owner validation](../../docs/input-owner-validation.md).
