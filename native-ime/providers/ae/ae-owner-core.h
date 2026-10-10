#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include "ae-owner-contract.h"

enum AeState : uint32_t { AeUnknown, AeMissing, AeMetadata, AeVerifiedLayout, AeUnsupported, AeInvalid };
struct AeModules { uintptr_t base[kAeModules]; };
using AeReadFn=bool(*)(void*,uintptr_t,void*,size_t);
struct AeReader {
    AeReadFn read; void* context; AeModules modules; ULONGLONG deadline; volatile LONG* stop;
    uint32_t reads=0,bytes=0; bool failed=false,limited=false;
    LONGLONG qpcDeadline=0;
    bool Read(uintptr_t,void*,size_t);
    template<class T> bool Get(uintptr_t p,T* out){return Read(p,out,sizeof(*out));}
    uintptr_t Ptr(uintptr_t); uint32_t U32(uintptr_t);
};
struct AeObject {
    uintptr_t pointer,vtable,col,complete,node,window,platform,manager;
    uint32_t state,module,typeRva,colOffset,nodeKnown,windowKnown,platformKnown,managerKnown;
};
struct AeNode {
    AeObject object,parent;
    uintptr_t selfToken,parentToken,parentPointer;
    uint32_t parentPair,delegateLayout; // 0 = unproven; never guess a delegate member offset.
};
struct AeNativeInput { uintptr_t hwnd,property; };
enum AeNativeFocusClass : uint32_t { AeNativeFocusClassUnknown, AeNativeFocusEdit, AeNativeFocusOther };
struct AeInputs { uint32_t count,focusClass; AeNativeInput windows[8]; };
struct AeWindow { uintptr_t hwnd,nativeHwnd; uint32_t reciprocal; AeObject property; };
struct AeTime { int32_t value; uint32_t scale; };
struct AeCanvas {
    AeObject pano,item,beeComp,project,transaction,layer;
    uintptr_t dir,mru,commandDelegate,backDir,undo,currentTransaction,layerComp;
    uint32_t stage,ownerPair,compositionMatch,timeRead,itemMagic,itemType;
    AeTime itemTime,transactionTime,layerOffset;
    uint32_t layerStretchWord0,layerStretchWord1;
};
struct AeTimeline {
    AeObject primary,item,beeComp,project,transaction;
    uintptr_t dir,itemRef,itemBox,currentItem,undo,currentTransaction;
    uint32_t stage;
};
struct AeSnapshot {
    AeObject manager; uintptr_t focusToken,focusNode,selfToken;
    uint32_t mutationDepth,focusPair,windowCount,nodeCount,chainState,focusClass;
    AeWindow windows[8]; AeNode nodes[8];
    AeCanvas canvas;
    AeTimeline timeline;
};
enum AeOwnerDecision : uint32_t { AeOwnerUnknown, AeOwnerText, AeOwnerCommand };
enum AeOwnerReason : uint32_t {
    AeOwnerUnproven, AeOwnerInvalidCapture, AeOwnerChanged, AeOwnerInvalidFocus,
    AeOwnerInvalidCanvas, AeOwnerUnsupportedTransaction, AeOwnerUnboundLayer,
    AeOwnerBoundTextTransaction, AeOwnerExplicitNoTransaction,
    AeOwnerExplicitNoTimelineTransaction, AeOwnerInvalidTimeline
};
struct AeOwnerEvidence { uint32_t decision,reason; };
AeObject AeReadObject(AeReader&,uintptr_t);
bool AeVerifySpans(AeReader&,uint32_t* failedModule,uint32_t* failedRva);
void AeCollect(AeReader&,const AeInputs&,AeSnapshot*);
void AeCollectCanvas(AeReader&,const AeSnapshot&,AeCanvas*);
void AeCollectTimeline(AeReader&,const AeSnapshot&,AeTimeline*);
// Current-owner evidence only. It neither routes keys nor retains
// objects. stableCapture must include scope/module/span/read checks on both reads.
AeOwnerEvidence AeClassifyCanvasOwner(const AeSnapshot&,const AeSnapshot&,bool stableCapture);
AeOwnerEvidence AeClassifyTimelineOwner(const AeSnapshot&,const AeSnapshot&,bool stableCapture);
AeOwnerEvidence AeClassifyCurrentOwner(const AeSnapshot&,const AeSnapshot&,bool stableCapture);
