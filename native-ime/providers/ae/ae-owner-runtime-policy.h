#pragma once
#include "ae-owner-core.h"
#include "../../input-owner.h"

// Called only by an initialized exact-build provider. All failure outcomes keep
// provider/profile identity, while their object identity and kind are cleared.
void AeMakeOwnerStamp(const AeSnapshot&,const AeSnapshot&,bool validCapture,
    bool plainLetterDomain,DWORD pid,DWORD tid,HWND focus,RiumOwnerStamp*);
