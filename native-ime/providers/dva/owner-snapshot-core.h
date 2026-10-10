#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include "owner-snapshot-contract.h"

// Diagnostic only. No application calls, AddRef, locks, COM, text reads or
// routing policy. Same-thread sampling reduces UI races; it is not ownership.
constexpr unsigned kOwnerWindows = 8, kOwnerParents = 16, kOwnerFilters = 256;
enum OwnerState : uint32_t { OUnknown, ORead, OMissing, OUnsupported, OInvalid };
using OwnerReadFn = bool (*)(void*, uintptr_t, void*, size_t);
struct OwnerReader {
    OwnerReadFn source; void* context; uintptr_t base; uint32_t imageSize;
    ULONGLONG deadline; volatile LONG* stop;
    uint32_t reads = 0, bytes = 0; bool failed = false, limited = false;
    LONGLONG qpcDeadline = 0;
    bool Read(uintptr_t, void*, size_t);
    template<class T> bool Get(uintptr_t p, T* out) { return Read(p, out, sizeof(T)); }
    uintptr_t Ptr(uintptr_t p);
    uint32_t U32(uintptr_t p);
    uint8_t Byte(uintptr_t p);
    uint32_t Rva(uintptr_t pointer) const;
};
struct OwnerObject {
    uintptr_t pointer; uint32_t vtable, colOffset, typeRva, state, node;
};
struct OwnerWindowInput { uintptr_t hwnd, property; };
struct OwnerInputs { uint32_t count; OwnerWindowInput windows[kOwnerWindows]; };
struct OwnerWindow {
    uintptr_t hwnd; OwnerObject property, delegate, monitor, background;
    uintptr_t delegateBlock, backgroundBlock, propertyToken;
    uint32_t propertyRefcount;
};
struct OwnerNode {
    OwnerObject object, delegate, handlerManager, handler;
    uintptr_t selfToken, parentToken, parent, callback, delegateBlock;
    uintptr_t childrenBegin, childrenEnd;
    uint32_t parentMatched, parentContains, callbackEmpty, refcount, flags, keyRva, terminal;
};
struct OwnerFilter {
    OwnerObject object; uintptr_t token, selfToken, callback;
    uint32_t pairMatched, callbackEmpty, refcount, virtual88, filterRva;
};
struct OwnerFilterSet {
    uintptr_t begin, end; uint32_t state, count; OwnerFilter entries[kOwnerFilters];
};
struct OwnerDictionaryValue {
    OwnerObject dictionary; uintptr_t wantedId, foundValue, valueId;
    uint32_t state, count, tag, booleanKnown, booleanValue, textIdentityKnown, textIdentity;
};
struct OwnerDefaultFlag {
    OwnerObject key,keyReference;
    uintptr_t begin,end,value,keyId;
    uint32_t state,initializedKnown,initialized,count,matches,version,tag,booleanKnown,booleanValue;
    uint32_t keyVbtableRva,keyAddRefRva,keyReleaseRva;
};
struct OwnerEndpoint {
    OwnerObject parameter, owner; uintptr_t block, holder;
    OwnerDictionaryValue bypass, varying; uint64_t keyframeCount;
    int32_t value; uint32_t state;
};
struct OwnerSelection {
    OwnerObject manager, component, componentOwner, textBlock;
    uintptr_t managerOwner, managerBlock, componentBlock, componentSentinel, componentFirst;
    uintptr_t textSentinel, textFirst, textOwner, textShared;
    uintptr_t parametersBegin, parametersEnd;
    uint64_t componentCount, textCount; uint32_t mode, state;
    uint32_t modeGetterRva, managerStrongKnown, managerStrong;
    OwnerDictionaryValue matchName; OwnerEndpoint endpoints[2];
    int32_t textEndpoints[2]; uint32_t textEndpointState;
};
struct OwnerMonitor {
    uint32_t state, fourcc, key, entriesRead;
    uint32_t entryContextKnown,entryContext;
    uint32_t modeOverrideKnown,modeOverride;
    uint32_t initEnabledKnown,initEnabled,providerModeKnown,providerMode;
    uint32_t providerModeGetterRva,componentFlagGetterRva;
    OwnerObject background, manipulator, manipulatorOwner, currentComponent, currentOwner, currentProvider;
    uintptr_t sentinel, buckets, mask, bucketFirst, bucketLast, foundEntry, sharedBlock;
    uintptr_t selectedBegin, selectedEnd, selectedIndex, componentBlock, providerOwner, providerBlock;
    uintptr_t contextOwner,contextBlock;
    OwnerDictionaryValue componentFlag;
    OwnerDefaultFlag defaultFlag;
};
enum class OwnerSnapshotKind : uint32_t { Full, NativeEditText, CaptionText };
struct OwnerSnapshot {
    OwnerSnapshotKind kind;
    uint32_t windowCount, parentCount, chainState;
    uint32_t mainThreadId, zeroRefcounts, focusMutationDepth; uintptr_t executor, executorBlock;
    OwnerWindow windows[kOwnerWindows];
    OwnerObject manager, observer;
    uintptr_t focusToken, focusNode, special[3], specialTree;
    uint32_t focusPairMatched, specialTreeEmpty, observerEnabled, modifiers, observerModifiers;
    OwnerNode parents[kOwnerParents]; OwnerFilterSet filters[2]; OwnerSelection selection; OwnerMonitor monitor;
};
OwnerObject OwnerReadObject(OwnerReader&, uintptr_t);
OwnerDictionaryValue OwnerReadDictionary(OwnerReader&, uintptr_t, uint32_t keyRva, bool identity);
OwnerDefaultFlag OwnerReadDefaultBypass(OwnerReader&);
OwnerEndpoint OwnerReadEndpoint(OwnerReader&, uintptr_t raw, uintptr_t owner, uintptr_t block);
bool OwnerVerifySpans(OwnerReader&, uint32_t* failedRva);
void OwnerCollect(OwnerReader&, const OwnerInputs&, OwnerSnapshot*);
// Partial proofs are TEXT-only. Negative results require a fresh full pair,
// never supplementing/reusing these records to infer COMMAND.
void OwnerCollectNativeEditText(OwnerReader&, const OwnerInputs&, OwnerSnapshot*);
void OwnerCollectCaptionText(OwnerReader&, const OwnerInputs&, OwnerSnapshot*);
