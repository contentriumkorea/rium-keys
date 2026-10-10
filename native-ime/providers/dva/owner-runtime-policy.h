#pragma once
#include "owner-command-classifier.h"
#include "owner-runtime.h"
struct OwnerNativeEditFacts {
    uintptr_t instanceProc, classProc, classModule;
    uint32_t standardClass, writable;
    uintptr_t infoW,infoA,moduleInfoW,moduleInfoA;
    uintptr_t qualifiedInfoW;
    uint32_t unicode,moduleKind;
};
bool OwnerReadNativeEdit(HWND, OwnerNativeEditFacts*);
RiumOwnerKind OwnerRuntimeClassify(const OwnerCommandEvidence&, const OwnerNativeEditFacts&);
