#include "owner-monitor-classifier.h"
static bool Is(const OwnerObject& o,uint32_t vt){
    if(!o.pointer||o.state!=ORead||o.vtable!=vt)return false;
    for(const auto& c:kOwnerTypes)if(c.vtable==vt)return o.colOffset==c.offset&&o.typeRva==c.type;
    return false;
}
static bool Filters(const OwnerSnapshot& s){
    for(unsigned f=0;f<2;++f){const auto& set=s.filters[f];if(set.state!=ORead||set.count>kOwnerFilters)return false;
        for(unsigned i=0;i<set.count;++i){const auto& e=set.entries[i];
            if(!Is(e.object,e.object.vtable)||!e.refcount||!e.token||e.token!=e.selfToken||!e.pairMatched||!e.callbackEmpty||e.virtual88!=0x1915D250)return false;
            const auto vt=e.object.vtable;const bool base=vt==0x27878AA8||vt==0x26E30F30||vt==0x27875E58||vt==0x26E182C8;
            if(f==0){if(!base||e.filterRva!=0x191627C0)return false;continue;}
            if(base&&e.filterRva==0x191627D0)continue;
            switch(vt){
            case 0x2655DA38:if(e.filterRva==0x193A5340)continue;break;
            case 0x26ECAD08:if(e.filterRva==0x1F0EF580)continue;break;
            case 0x26ECCA08:if(e.filterRva==0x1F1070E0)continue;break;
            case 0x2776D2E8:case 0x2776DA20:if(e.filterRva==0x191627D0)continue;break;
            case 0x27941B78:if(e.filterRva==0x235EED60)continue;break;
            }return false;
        }
    }return true;
}
static bool InitializedScalarDefault(const OwnerMonitor& m){
    const auto& d=m.defaultFlag;
    if(m.componentFlagGetterRva!=0x19EAB180||m.componentFlag.state!=OMissing||
        !Is(m.componentFlag.dictionary,0x263D95F8)||m.componentFlag.count>4)return false;
    if(d.state!=ORead||!d.initializedKnown||d.initialized!=1||!d.keyId||
        !Is(d.key,0x263D41B8)||!Is(d.keyReference,0x263D41C8)||
        d.key.pointer>UINTPTR_MAX-0x28||d.keyReference.pointer!=d.key.pointer+0x28||
        d.keyVbtableRva!=0x263D41E0||d.keyAddRefRva!=0x184E25C8||d.keyReleaseRva!=0x184E5D48||
        !d.begin||d.end<d.begin||(d.end-d.begin)%40||!d.count||d.count>128||
        (d.end-d.begin)/40!=d.count||d.value<d.begin||d.value>=d.end||(d.value-d.begin)%40||
        d.matches!=1||d.version!=7||d.tag!=3||!d.booleanKnown||d.booleanValue)return false;
    return true;
}
static bool ProviderQueryPathClosed(const OwnerMonitor& m){
    if(!m.modeOverrideKnown)return false;
    // 23975150's mode1 result is byte[interface+A58]. A zero return does
    // not reach component-property lookup or provider initialization.
    if(!m.modeOverride)return true;
    // The actual v8 path: fixed-capacity dictionary misses Bypass; its
    // initialized unique version7 default is a native boolfalse. The key's
    // AddRef/Release are verified no-op leaves, and tag3 converts directly.
    // Provider mode1 and nonzero entry context skip all plugin init methods.
    return m.modeOverride==1&&m.initEnabledKnown&&m.initEnabled==1&&
        InitializedScalarDefault(m)&&m.providerModeKnown&&m.providerMode==1&&
        m.providerModeGetterRva==0x239CBA30&&m.entryContextKnown&&m.entryContext;
}
bool OwnerMonitorCommand(const OwnerCommandEvidence& e){
    if(!e.snapshot||!e.exactImageVerified||!e.loadedSpansVerified||!e.ownerBefore||!e.ownerAfter||!e.inputsStable||!e.samplesEqual||e.readFailed||e.limited)return false;
    const auto& s=*e.snapshot;
    if(s.kind!=OwnerSnapshotKind::Full)return false;
    if(!e.targetThreadId||s.mainThreadId!=e.targetThreadId||!s.executor||!s.executorBlock||s.zeroRefcounts||s.focusMutationDepth)return false;
    if(e.virtualKey<'A'||e.virtualKey>'Z'||!e.mappedCharacterKnown||e.mappedCharacter<'A'||e.mappedCharacter>'Z'||!e.dvaModifiersKnown||e.dvaModifiers||s.modifiers||s.observerModifiers)return false;
    if(!s.windowCount||s.windowCount>kOwnerWindows||!e.nativeFocus||s.windows[0].hwnd!=e.nativeFocus||!Is(s.manager,0x267421F8)||!s.focusPairMatched||!s.focusToken)return false;
    if(s.chainState!=ORead||s.parentCount!=8||s.parents[0].object.pointer!=s.focusNode||s.parents[0].selfToken!=s.focusToken)return false;
    constexpr uint32_t nodes[8]={0x27878AA8,0x26E30F30,0x26E30F30,0x27875E58,0x267429D8,0x26743D80,0x26E190E0,0x26E0E7C8};
    constexpr uint32_t delegates[4]={0x27922188,0x27292E88,0x272943F8,0x27294F90};
    constexpr uint32_t handlers[7]={0x191626B0,0x1E937120,0x1E937120,0x1E8E0880,0x1917CCD0,0x1917CCD0,0x1E8E3E00};
    for(unsigned i=0;i<8;++i){const auto& n=s.parents[i];if(!Is(n.object,nodes[i]))return false;
        for(unsigned j=0;j<i;++j)if(s.parents[j].object.pointer==n.object.pointer)return false;
        if(i==7){if(!n.terminal)return false;break;}
        if(n.terminal||!n.refcount||!n.selfToken||!n.parentMatched||!n.parentContains||!n.parentToken||(n.flags&0xC)||!n.callbackEmpty||
            n.parent!=s.parents[i+1].object.pointer||(i<6&&n.parentToken!=s.parents[i+1].selfToken)||n.keyRva!=handlers[i])return false;
        if(i<4&&!Is(n.delegate,delegates[i]))return false;
    }
    const auto& w=s.windows[0];const auto& m=s.monitor;const auto& v=s.selection;
    if(!Is(w.property,0x27875E58)||w.property.pointer!=s.parents[3].object.pointer||!Is(w.delegate,0x27294F90)||
        w.delegate.pointer!=s.parents[3].delegate.pointer||!w.delegateBlock||!Is(w.monitor,0x272943F8)||w.monitor.pointer!=s.parents[2].delegate.pointer||
        !Is(w.background,0x27292E88)||w.background.pointer!=s.parents[1].delegate.pointer)return false;
    if(!ProviderQueryPathClosed(m)||m.state!=ORead||m.fourcc!=0x76636F6D||m.background.pointer!=w.background.pointer||
        !Is(m.manipulator,0x27996E48)||!Is(m.manipulatorOwner,0x279970E8)||!Is(m.currentComponent,0x26662DA0)||
        !Is(m.currentOwner,0x266632C0)||!Is(m.currentProvider,0x269D5F00)||!m.providerOwner||!m.providerBlock||!m.componentBlock)return false;
    if(v.state!=ORead||!Is(v.manager,0x2723DF48)||v.mode!=1||v.modeGetterRva!=0x20F08500||!v.managerStrongKnown||!v.managerStrong||
        v.componentCount!=1||v.textCount||!Is(v.component,0x26662DA0)||
        v.component.pointer!=m.currentComponent.pointer||!v.matchName.textIdentityKnown||!v.matchName.textIdentity||
        v.endpoints[0].state!=ORead||v.endpoints[1].state!=ORead||v.endpoints[0].value!=-1||v.endpoints[1].value!=-1)return false;
    const auto& top=s.parents[6];if(!Is(top.handlerManager,0x26E0E7C8)||top.handlerManager.pointer!=s.parents[7].object.pointer||top.handler.pointer||top.handler.state!=OMissing)return false;
    if(s.special[0]||s.special[1]||s.special[2]||!s.specialTreeEmpty||s.observerEnabled||!Is(s.observer,0x264F22E8)||!Filters(s))return false;
    return true;
}
