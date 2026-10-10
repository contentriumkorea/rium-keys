#pragma once
#include "owner-snapshot-core.h"

// Pure experimental proof evaluator. It performs no calls, memory reads,
// hashes, host mutations or routing. The caller must provide a fresh snapshot;
// there is intentionally no HWND cache or persistent last-known-text state.
enum class OwnerCommandClass : uint32_t { Unknown, TimelineCommandCandidate };
enum class OwnerCommandReason : uint32_t {
    None, Capture, Lifetime, Transition, KeyDomain, Modifiers, Focus,
    Chain, Callback, Delegate, Filter, Special, Observer, AlternateHandler
};
struct OwnerCommandEvidence {
    const OwnerSnapshot* snapshot;
    uint32_t exactImageVerified, loadedSpansVerified, ownerBefore, ownerAfter;
    uint32_t inputsStable, samplesEqual, readFailed, limited;
    uint32_t targetThreadId; uintptr_t nativeFocus;
    uint32_t virtualKey, mappedCharacterKnown, mappedCharacter, dvaModifiersKnown, dvaModifiers;
};
struct OwnerCommandResult { OwnerCommandClass classification; OwnerCommandReason reason; };
OwnerCommandResult OwnerClassifyCommand(const OwnerCommandEvidence&);
