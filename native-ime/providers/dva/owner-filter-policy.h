#pragma once
#include "owner-snapshot-core.h"

// Both callers have already bound a full audited focus route and plain A-Z.
inline bool OwnerFilterExcludesLetters(const OwnerFilter& f,bool pre,const OwnerSnapshot& s){
    const auto& o=f.object;bool exact=false;
    if(o.pointer&&o.state==ORead)for(const auto& c:kOwnerTypes)
        if(c.vtable==o.vtable)exact=o.colOffset==c.offset&&o.typeRva==c.type;
    if(!exact||!f.refcount||!f.token||f.token!=f.selfToken||!f.pairMatched||!f.callbackEmpty)return false;
    const bool base=o.vtable==0x27878AA8||o.vtable==0x26E30F30||o.vtable==0x27875E58||o.vtable==0x26E182C8;
    // The dialog's audited postfilter/OnKey consume navigation keys only.
    // Its inherited visibility policy is distinct from the other filters.
    if(!pre&&o.vtable==0x26539E80){
        if(f.virtual88!=0x19178C10||f.filterRva!=0x192D1C50||!f.dialogDisabledKnown)return false;
        if(f.dialogDisabled)return true;
        if(!f.resolvedParent.known)return false;
        bool parentBound=!f.resolvedParent.parent;
        for(unsigned i=0;i<s.parentCount&&i<kOwnerParents;++i){
            const auto& n=s.parents[i];
            if(f.resolvedParent.parent==n.object.pointer)parentBound=true;
            if(!n.terminal&&(!n.resolvedParent.known||n.resolvedParent.parent!=n.parent))return false;
        }
        return parentBound;
    }
    if(f.virtual88!=0x1915D250)return false;
    if(pre)return base&&f.filterRva==0x191627C0;
    if(base)return f.filterRva==0x191627D0;
    switch(o.vtable){
    case 0x264FACF0:return f.filterRva==0x191D3D10; // RadioGroup: Tab only.
    case 0x2655DA38:return f.filterRva==0x193A5340;
    case 0x26ECAD08:return f.filterRva==0x1F0EF580;
    case 0x26ECCA08:return f.filterRva==0x1F1070E0;
    case 0x2776D2E8:case 0x2776DA20:return f.filterRva==0x191627D0;
    case 0x27941B78:return f.filterRva==0x235EED60;
    default:return false;
    }
}
