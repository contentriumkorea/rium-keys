# Input-owner candidate validation

This report records the 2026-10-10/11 development candidate. It is not a claim of
all-application compatibility or a release approval.

## Installation state

The user approved the Windows administrator prompt on 2026-10-11. The first
upgrade committed `2.0.0-preview.5` at 00:12 KST after an ordinary-user physical
fixture loaded its installed DLL. The subsequent `2.0.0-preview.6` installation
committed at 00:50 KST, with the same fixture passing again. Its installed x64
hash is `7099ECCBB59E088A9F8872C72A9ACBA3DF8D3FB53FE8775FF70C0EFE0363FC46`.
Previous version directories and recovery state are retained. Preview.7 contains
the return-to-text fix described below. Its 00:59 KST upgrade attempt ended with
Windows administrator confirmation cancelled and `FailedRolledBack`; at that
point the active installation remained preview.6. A second attempt at 01:03 KST reached the
physical fixture but was aborted: Windows had switched to the Microsoft Korean
profile, and the original-key check correctly remained ineligible. The controller
reported `RecoveryRequired` for the changed user profile. Independent readback
confirmed both COM views and the installed-app record were rolled back to
preview.6. The registered native profile was then explicitly selected again for
the authorized QA; registered/enabled/six-category/active/default readback passed.
Neither preview.7 attempt is an installed or physical-test success.

Preview.8 includes the same return-to-text correction and the user-requested
**CONTENTRIUM Keys** name. The upgrade preserves existing profile identities and
settings while changing the visible Windows input-method and installed-app names.
Its local upgrade committed at 01:19 KST after the physical fixture produced
exact `한글 한글 ` around original V down/up, with the fork and Korean mode
verified at both key events. The fixture restored its prior profile and exited
0. The installer read back the exact `CONTENTRIUM Keys` description and saved
`Installed`; a separate read confirmed the new installed-app name/version,
both COM paths, both DLL hashes and both profile descriptions. The registered,
enabled, active and default native profile and six categories were retained.

Separate physical letter events produced exact `한글 ` in a native EDIT, the
button received original V down/up while the RIUM Korean profile remained
selected, and returning to the EDIT produced exact `한글 한글 `. The fixture
observed inline composition and verified restoration of its prior local profile.
The installer exited 0 and recorded `Installed`. Registration, enabled status
and all six categories were read back.

The upgrade preserved the pre-install default profile. RIUM was then selected
for the authorized real-application QA, and a fresh Premiere process loaded the
installed preview.5 path, then preview.6 after the next restart. Preview.8
real-application follow-up results are recorded below.
The public updater has not been changed.

## Installed application check on 2026-10-11

A fresh Premiere process loaded the installed preview.5 DLL, SHA-256
`B42131115654A20D125CEB09182377B4A4F8F4F594255343278C9F15982E777B`.
Opening the owned `AdobeShortcutTest.prproj` took several minutes
with the application reporting not responding, then recovered before keyboard
testing. This observation does not establish the input engine as the cause.

The initial graphics-search/timeline attempts overlapped with the user's manual
interaction, so those key outcomes are inconclusive. After the user explicitly
handed over the controls, a first physical C on the timeline selected Razor.
Physical G, K, S in Project search displayed separate `ㅎㅏㄴ`; clicking the
timeline and pressing V once changed Razor to Selection without a floating
composition window. The existing tool bindings include Korean alternatives,
so C/V tool changes alone do not prove original Latin-key delivery.

Each automated physical key action can reactivate the target window. The user
then typed manually and confirmed that the first syllable splits after clicking
away and returning. The resulting Project search value was read back as
`ㅎㅏㄴ글`. This establishes a real user-visible defect, not merely an automation
artifact. Later slow single-key attempts produced `한` correctly; that does not
invalidate the initial failure or pass the transition release gate.

A disposable exact-preview.5 metadata reader uses the already registered
language-bar item to inspect only numeric service state. It verifies the owning
DLL path, item CLSID, vtable module and thread manager, retains/releases the
object, and never reads entered characters. The public TSF parent-compartment
snapshot uses no manager activation or edit session. One-shot transport tests
passed wrong-token, missing-focus, timeout and unhook cleanup cases. These
diagnostics remain in ignored `out/composition-metadata`, outside packages.

The Project EDIT positively exposed a transitory-extension parent before and
after its first character (static flags 4, VT_UNKNOWN nonnull). Slow successful
input kept the same owner epoch and active inline composition. A separate
physical P after clicking the audio-track area produced a floating Korean
composition instead of selecting Pen. The installed service had no adopted
provider at that boundary. Both the installed collector on a copied state and
the independent staged observer classified the audio route UNKNOWN. Its first
two delegate types differ from the previously proved video-track route; this
is a concrete coverage gap, not evidence that every timeline focus works.

The second hands-off manual session used a bounded, 50 ms metadata sampler
without invoking the collector or recording keys/text. The user reported normal
Hangul. Observed compositions survived first-to-following-character updates,
including subsequent focus changes; no sampled demotion occurred. The observer
stopped with unhooked=1, writers=0 and clean=1. The earlier split is still an
unresolved intermittent defect, requiring fresh-load retesting.

An isolated diagnostic of a suspected stale `compTargetCtx` ordering defect did
not reproduce first-key loss: the actual runtime and service helpers delayed
COMMAND in TestKeyDown but adopted it in KeyDown for the same key (4 checks,
0 failures). No production change was made on that hypothesis; that diagnostic
does not model the entire Windows key-delivery path.

## Follow-up: reproduced return-to-text split and candidate fix

At 00:50 KST on 2026-10-11, preview.6 was installed after its native physical
fixture passed. A fresh Premiere process loaded its installed DLL, verified as
`7099ECCBB59E088A9F8872C72A9ACBA3DF8D3FB53FE8775FF70C0EFE0363FC46`.
Its first Project-search G/K/S formed `한` correctly. Clicking the audio track
and pressing physical P selected Pen, whose binding has no Korean alternate;
the installed reader reported COMMAND. This verifies the paired audio fix in
that actual route, without changing the Korean mode or injecting a replacement.

Returning to the same search and pressing R/M/F instead produced `한ㄱㅡㄹ`.
The bounded numeric observer caught the relevant state: the host had ended
the previous composition on blur, the command set `inputOwnerBoundary=1` while
no composition existed, and the first new consonant retained that flag. The
following vowel therefore finalized the new consonant. Ownership stayed TEXT
at epoch 3; no path demotion occurred. The observer removed its hook and reported
zero writers and clean cleanup. Its final pending sample was unknown at the
deadline, so the capture is used only for the completed state rows.

The new regression executes the actual `PrepareInput -> FSM -> Apply` path for
R/M/F after this recorded empty COMMAND boundary. Before the fix it failed the
exact `한글` assertion. After consuming the empty boundary before new input it
passes (223 inline checks on each architecture). Pending finalization, retained
composition retirement and all owner-bound write checks remain unchanged. A
separate code review found no blocker in that narrow fix. The same physical
transition passed on installed preview.8, as recorded next.

## Preview.8 physical application follow-up

Fresh Premiere 26.5.2.5 and Studio One 6.6.4.102451 processes loaded the installed
preview.8 DLL. The actual path was read from each process, and the installed x64
hash matched the package verification report. No Hangul/English toggle was used
in these tests. Test documents belonged to this workspace.

In Premiere Project search, physical G/K/S produced `한`; clicking the audio
track and pressing P selected Pen. Returning to search and pressing R/M/F
produced exact `한글`, where preview.6 had produced `한ㄱㅡㄹ`. A bounded numeric
observer showed the first returning consonant with `inputOwnerBoundary=0`, then
one surviving composition and the same TEXT epoch through the vowel and final
consonant. It recorded no input text, did not activate a thread manager or call
the collector, and exited with its hook removed and no writers. Its last sample
after an application switch was unknown; only completed Premiere rows are used.

Premiere caption editing also passed. Physical G/K/S appended `한` to the owned
caption; clicking the video timeline and pressing H selected Hand. Returning to
the on-screen caption and physically entering R/M/F produced `한글한글` with
inline composition. Leaving text editing for graphic selection and pressing P
selected Pen while preserving exact caption contents. Neither the tested P nor
H binding has a Korean alternate. No floating Korean composition window was
observed in these routes.

In Studio One, browser search G/K/S produced `한`; clicking the empty workspace
and pressing C changed the metronome/Click state. Returning to search, placing
the caret at the end and entering R/M/F produced exact `한글`. After switching
to Premiere and back, C restored the original metronome state, and a new search
G/K/S appended a correctly composed `한`, yielding `한글한`. The user's actual
Click binding was checked as C, with no Korean alternate. The numeric reader
reported the CCL COMMAND route in the workspace and cleaned up after the probe.

These are functional transition checks, not a measured typing-latency bound or
an all-application guarantee. After Effects preview.8 composed `한` inline, but
its first shortcut attempt did not establish the expected command outcome;
its shortcut and return-to-text checks remain incomplete.

## Transparent brand icon candidate

Preview.9 changes the input-method button from the upstream opaque layout tile
to the CONTENTRIUM Keys transparent CK logo. The original PNG is retained in
`assets/contentrium-keys.png`; the native DLL and legacy executable share its
nine-size Windows ICO. This presentation change retains the preview.8 input
routing and composition code. Its package is a manual preview upgrading
preview.8; installation and loaded-resource verification are recorded separately
from the preview.8 functional results.

The final preview.9 package run at 01:49 KST passed the same 23 suites and
1,202 assertions. Its x64 DLL SHA-256 is
`763583B703AC2D7FCFE74BB71B2CFB212A96E9ABE6C286FF5EC08652135B7271`;
x86 is `D2A00AEA09D198761F7241BB74DEDDA9D4E5947AF3FBFD177A5D7E983BCC17A6`.
All 23 installer contracts and preview.8-to-preview.9 preflight passed. Actual
Windows `LoadImage` loaded all nine icon sizes from both architecture resources,
and each preserved transparent pixels and alpha-zero corners. A 32-pixel Windows
readback was visually inspected. The legacy executable also built with zero
warnings and errors. These checks do not by themselves establish installation.

The 01:50 KST preview.9 upgrade attempt ended when Windows administrator
confirmation was cancelled. It recorded `FailedRolledBack` before the machine
upgrade began. Independent readback confirmed preview.8 still `Installed`,
the installed-app name `CONTENTRIUM Keys`, and the native profile still
registered/enabled/active/default with six categories. Preview.9 is built and
published as development source; at that point its logo was not yet installed.

The user requested another administrator prompt and approved it at 06:32 KST.
The preview.9 upgrade committed at 06:34 KST after a fresh ordinary-user fixture
loaded the installed DLL and physical G/K/S/R/M/F/Space produced `한글 `, a
button received original V down/up with the Korean profile still selected, and
returning to the input field produced exact `한글 한글 `. Inline composition
and profile restoration passed; the fixture and installer exited 0. Independent
readback confirmed `Installed`, the CONTENTRIUM Keys name, version preview.9,
both installed DLL hashes matching the verified package and both COM paths.
The native input profile remained registered/enabled/active/default with six
categories. Already running applications may retain an earlier loaded DLL until
they are restarted; they were not forcibly closed for this logo update.

## Routing behavior

The service retains Korean mode. Positive command ownership passes the original
key before composition processing. It does not dispatch an application's command
handler or synthesize a replacement key for that decision.

Bound text operations retain the original owner, native focus and lifetime
metadata. A host callback that changes focus cannot authorize a pending write or
resend on the new target. Provider initialization happens in the background and
is adopted only at a clean input boundary. An unsupported clean input remains
on ordinary TSF until that composition finishes; later positive ownership cannot
strand a composition that started without the provider. A recognized transaction
that loses its proof cannot start a fresh UNKNOWN-bound write. This fixes the
reproduced UNKNOWN-to-TEXT lifetime failure without weakening old write bindings.

The composition-target helper also keeps temporary COM references local until
reentrant callbacks finish. A callback that publishes a newer target prevents
the old caller from overwriting its reference or mixing in another window.

An additional regression reproduced a live composition surviving
TEXT -> COMMAND -> original TEXT. The candidate now permits only the original
DVA composition handle to be ended synchronously after the complete original
TEXT identity, nonzero lifetime token and current TSF context match again.
This path does not write text, move the selection, rebase old write permissions,
or enqueue an asynchronous retry. A replacement created by reentry is preserved.
CCL and unproven identities do not use this recovery.

## Automated evidence

The full preview.8 package run at 2026-10-11 01:16 KST passed 23 required suites
with 1,202 assertions after the selected-graphic, clean-boundary, target-publication,
staged collection, AE canvas/timeline, paired audio route and empty-boundary
changes. The suites cover the
engine, real DLL routing contracts, inline composition, edit sessions, owner
binding, pending commits, delayed resend and runtime lifecycle. x64 additionally
checks the DVA, CCL and AE providers. The package gate binds every run to the
candidate DLL SHA-256 and verifies the staged package has the same binaries.

Full output is generated in `native-ime/out/package-verification.json`.
The ignored experiments and one-shot observers are not package payloads.
The final staged x64 DLL SHA-256 is
`FBA564CB6733108F5AA5D8602099D00D303E1C6B94F69D34CDADDF03E3B45B40`;
x86 is `9070ED7A8BC1AA0D1FA31F35C8549BE112634AF14A84257B7B17141C4680BFDB`.
All 23 installer-script contracts and the preview.6-to-preview.8 upgrade preflight
also passed. These automated checks are separate from the successful local
installation and physical fixture recorded above.

## Read-only application evidence

| Application | Collected evidence | What remains unverified |
| --- | --- | --- |
| Premiere 26.5.2.5 | Search, caption, audio/video timeline and selected-graphic physical transitions passed on installed preview.8, including the previously failing return-to-text sequence | Fast continuous typing, additional panels and other builds |
| Studio One 6.6.4.102451 | Search / workspace C / return to search and application-switch transitions passed on installed preview.8 | Additional text controls, continuous typing and other builds |
| After Effects 26.5.0.89 | Actual v4 canvas samples distinguish a bound text transaction from explicit absence while the same native focus remains; v5 timeline COMMAND and active rename UNKNOWN samples are stable and clean; both reviewed predicates are integrated | Physical candidate input and transitions |

The After Effects v4 observer reports TEXT for the verified text transaction and
COMMAND for explicit absence in the focused composition. It still reports
`routing=UNKNOWN` because it does not route keys. The two canvas states share
the same focused DVA node, so native focus or window type alone is insufficient.
The read-only probes remove their hooks and
confirm no outstanding writer and no diagnostic DLL in the target afterward.
These cleanup results and metadata timings do not establish keyboard behavior
or end-to-end typing latency.

The Premiere v9 staged observer reused the approved collection order in the
actual application: native search TEXT in 0.1750 ms, caption TEXT in 0.2964 ms,
selected graphic COMMAND in 1.1480 ms, and timeline COMMAND in 1.0015 ms.
Both TEXT paths stopped before command collection and used zero key-mapping
calls. All four samples were stable, stayed within the shared 15 ms/read limits,
and removed the observer cleanly. These QPC times cover header/span checks,
fresh metadata pairs and classification; they exclude hashing, hook transport,
installation and physical key dispatch. The observer did not call the production
runtime entry point or route a key.

## Release checks still required

Preview.8 installation, loaded DLL paths, native original-key down/up and the
Premiere/Studio One transitions above passed. Complete After Effects physical
shortcut and return-to-text tests, fast continuous typing, and the modern text
host presentation check in a freshly restarted host. Literal Unicode automation
is not a physical IME test. Preview.9 installation, installed payload/resource
hashes and the native physical transition now passed. Native unattended updates and unsupported application builds are not
validated by these checks; the stable updater is unchanged.

Only verified application behavior can expand the documented support scope.

## Preview.10 status display and public installer

Preview.10 separates the white 가/A mode button from the fixed CK profile brand,
updates the stale preview.2 profile icon path, and adds a single NSIS installer
with Korean physical-test instructions. All 25 suites (1,506 assertions) and
30 installer contracts passed. On 2026-10-11 at 07:10:26 KST the packaged EXE
successfully upgraded preview.9 after the user approved elevation and completed
the physical Korean / original V down-up / Korean fixture. All installed payload
hashes, both COM paths, both profile icon paths, enabled/default/active profile
and installed-app version were read back. Same-version packaged preflight passed.

The detailed scope and exact release hashes are in
[preview-10-release.md](preview-10-release.md). Earlier application-specific
results remain preview.8 evidence; this presentation/installer change does not
expand the tested application scope or enable unattended native updates.
