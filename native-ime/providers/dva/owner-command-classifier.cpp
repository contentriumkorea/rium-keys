#include "owner-command-classifier.h"

static bool Is(const OwnerObject& o, uint32_t vt) {
    if (!o.pointer || o.state != ORead || o.vtable != vt) return false;
    for (const auto& c : kOwnerTypes) if (c.vtable == vt) return o.colOffset == c.offset && o.typeRva == c.type;
    return false;
}
static bool BaseNode(uint32_t vt) {
    return vt==0x27878AA8 || vt==0x26E30F30 || vt==0x27875E58 || vt==0x26E182C8;
}
static bool ExcludesLetters(const OwnerFilter& f, bool pre) {
    if (!f.refcount || !f.token || f.token != f.selfToken || !f.pairMatched || !f.callbackEmpty ||
        !Is(f.object,f.object.vtable) || f.virtual88 != 0x1915D250) return false;
    // Count1 is live. The application's temporary AddRef means it must not be
    // treated as disabled. These exact handlers exclude A-Z even when called.
    if (pre) return BaseNode(f.object.vtable) && f.filterRva==0x191627C0;
    if (BaseNode(f.object.vtable)) return f.filterRva==0x191627D0;
    switch (f.object.vtable) {
    case 0x2655DA38: return f.filterRva==0x193A5340; // TabWell: Space only.
    case 0x26ECAD08: return f.filterRva==0x1F0EF580; // Return/numpadReturn/Escape.
    case 0x26ECCA08: return f.filterRva==0x1F1070E0;
    case 0x2776D2E8: case 0x2776DA20: return f.filterRva==0x191627D0;
    case 0x27941B78: return f.filterRva==0x235EED60; // Backspace/Delete.
    default: return false;
    }
}
OwnerCommandResult OwnerClassifyCommand(const OwnerCommandEvidence& e) {
    using Why=OwnerCommandReason;
    const auto unknown=[](Why why){return OwnerCommandResult{OwnerCommandClass::Unknown,why};};
    if (!e.snapshot || !e.exactImageVerified || !e.loadedSpansVerified || !e.ownerBefore || !e.ownerAfter ||
        !e.inputsStable || !e.samplesEqual || e.readFailed || e.limited) return unknown(Why::Capture);
    const auto& s=*e.snapshot;
    if(s.kind!=OwnerSnapshotKind::Full)return unknown(Why::Capture);
    if (!e.targetThreadId || s.mainThreadId!=e.targetThreadId || !s.executor || !s.executorBlock || s.zeroRefcounts) return unknown(Why::Lifetime);
    if (s.focusMutationDepth) return unknown(Why::Transition);
    // Character mapping is an explicit fact, never inferred from an HWND/class.
    // It excludes the numeric buffer's ':'/';' exception on unusual layouts.
    if (e.virtualKey<'A' || e.virtualKey>'Z' || !e.mappedCharacterKnown || e.mappedCharacter<'A' || e.mappedCharacter>'Z') return unknown(Why::KeyDomain);
    if (!e.dvaModifiersKnown || e.dvaModifiers || s.modifiers!=e.dvaModifiers || s.observerModifiers!=e.dvaModifiers) return unknown(Why::Modifiers);
    if (!Is(s.manager,0x267421F8) || !s.focusPairMatched || !s.focusToken || !s.focusNode ||
        !s.windowCount || s.windowCount>kOwnerWindows || !e.nativeFocus || s.windows[0].hwnd!=e.nativeFocus ||
        !Is(s.windows[0].property,0x26E182C8)) return unknown(Why::Focus);
    if (s.chainState!=ORead || s.parentCount!=10 || s.parents[0].object.pointer!=s.focusNode || s.parents[0].selfToken!=s.focusToken) return unknown(Why::Chain);
    constexpr uint32_t nodeTypes[10]={0x26E30F30,0x26E30F30,0x26E30F30,0x26E30F30,0x26E30F30,0x26E182C8,0x267429D8,0x26743D80,0x26E190E0,0x26E0E7C8};
    constexpr uint32_t owners[6]={0x272E8C50,0x272E90D0,0x272DF190,0x272D48B8,0x272D8800,0x272DA918};
    // Exact paired audio route; all three key slots resolve to the same
    // verified literal-false leaves as video. Mixed routes remain unknown.
    const bool audio=Is(s.parents[0].delegate,0x272E9598)&&Is(s.parents[1].delegate,0x272E9A18);
    constexpr uint32_t handlers[9]={0x1E937120,0x1E937120,0x1E937120,0x1E937120,0x1E937120,0x1E8E0880,0x1917CCD0,0x1917CCD0,0x1E8E3E00};
    for (unsigned i=0;i<10;++i) {
        const auto& n=s.parents[i];
        if (!Is(n.object,nodeTypes[i])) return unknown(Why::Chain);
        for (unsigned j=0;j<i;++j) if (s.parents[j].object.pointer==n.object.pointer) return unknown(Why::Chain);
        if (i==9) { if (!n.terminal) return unknown(Why::Chain); break; }
        if (n.terminal || !n.refcount || !n.selfToken || !n.parentMatched || !n.parentContains ||
            n.parent!=s.parents[i+1].object.pointer || !n.parentToken ||
            (i<8 && n.parentToken!=s.parents[i+1].selfToken) || (n.flags&0xC)) return unknown(Why::Chain);
        if (!n.callbackEmpty) return unknown(Why::Callback);
        if (n.keyRva!=handlers[i]) return unknown(Why::Delegate);
        const uint32_t owner=audio&&i<2?(i?0x272E9A18:0x272E9598):(i<6?owners[i]:0);
        if (i<6 && !Is(n.delegate,owner)) return unknown(Why::Delegate);
    }
    // Bind the actual native focus to this logical route, not a stale global
    // tool/selection. Native Edit focus and the monitor's same-HWND modes stay
    // unknown. Selection.mode/endpoints intentionally play no role here.
    const auto& tab=s.parents[5]; const auto& win=s.windows[0];
    if (win.property.pointer!=tab.object.pointer || win.delegate.pointer!=tab.delegate.pointer ||
        !Is(win.delegate,0x272DA918) || !win.delegateBlock) return unknown(Why::Focus);
    const auto& top=s.parents[8];
    if (!Is(top.handlerManager,0x26E0E7C8) || top.handlerManager.pointer!=s.parents[9].object.pointer ||
        top.handler.pointer || top.handler.state!=OMissing) return unknown(Why::AlternateHandler);
    if (s.special[0] || s.special[1] || s.special[2] || !s.specialTreeEmpty) return unknown(Why::Special);
    if (s.observerEnabled || !Is(s.observer,0x264F22E8)) return unknown(Why::Observer);
    for (unsigned f=0;f<2;++f) {
        const auto& set=s.filters[f];
        if (set.state!=ORead || set.count>kOwnerFilters) return unknown(Why::Filter);
        for (unsigned i=0;i<set.count;++i) if (!ExcludesLetters(set.entries[i],f==0)) return unknown(Why::Filter);
    }
    return {OwnerCommandClass::TimelineCommandCandidate,Why::None};
}
