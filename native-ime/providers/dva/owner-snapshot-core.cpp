#include "owner-snapshot-core.h"
#include <cstring>
#include <cmath>
#include <limits>

bool OwnerReader::Read(uintptr_t p, void* out, size_t n) {
    if (out && n) std::memset(out, 0, n);
    if (!out || !n || n > 128 || p < 0x10000 || p > UINTPTR_MAX - n) { failed = true; return false; }
    if(qpcDeadline && (reads&127)==0){LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now)||now.QuadPart>=qpcDeadline){limited=true;return false;}}
    if (reads >= 24000 || bytes + n > 384 * 1024 || GetTickCount64() >= deadline ||
        (stop && InterlockedCompareExchange(stop, 0, 0))) { limited = true; return false; }
    ++reads; bytes += static_cast<uint32_t>(n);
    if (!source(context, p, out, n)) { failed = true; return false; }
    return true;
}
uintptr_t OwnerReader::Ptr(uintptr_t p) { uintptr_t x = 0; Get(p, &x); return x; }
uint32_t OwnerReader::U32(uintptr_t p) { uint32_t x = 0; Get(p, &x); return x; }
uint8_t OwnerReader::Byte(uintptr_t p) { uint8_t x = 0; Get(p, &x); return x; }
uint32_t OwnerReader::Rva(uintptr_t p) const { return p >= base && p - base < imageSize ? static_cast<uint32_t>(p - base) : 0; }
static const OwnerTypeContract* Contract(uint32_t vt) {
    for (const auto& c : kOwnerTypes) if (c.vtable == vt) return &c;
    return nullptr;
}
OwnerObject OwnerReadObject(OwnerReader& r, uintptr_t p) {
    OwnerObject out{}; out.pointer = p;
    if (!p) { out.state = OMissing; return out; }
    const auto vt = r.Ptr(p); out.vtable = r.Rva(vt);
    const auto* c = Contract(out.vtable);
    if (!c) { out.state = OUnsupported; return out; }
    const auto col = r.Ptr(vt - 8); uint32_t words[6]{};
    if (col != r.base + c->col || !r.Read(col, words, sizeof(words)) || words[0] != 1 ||
        words[1] != c->offset || words[3] != c->type || words[5] != c->col) { out.state = OInvalid; return out; }
    out.colOffset = c->offset; out.typeRva = c->type; out.node = c->node; out.state = ORead;
    return out;
}
static bool Is(const OwnerObject& o, uint32_t vt) { return o.state == ORead && o.vtable == vt; }
static uintptr_t Intern(OwnerReader& r, uintptr_t wrapper) {
    const auto raw = r.Ptr(wrapper); return raw ? r.Ptr(raw + 0x10) : 0;
}
OwnerDefaultFlag OwnerReadDefaultBypass(OwnerReader& r) {
    OwnerDefaultFlag out{};uint8_t initialized=0;
    out.initializedKnown=r.Get(r.base+0x2D421EB8,&initialized);out.initialized=initialized;
    if(!out.initializedKnown||initialized!=1)return out;
    out.key=OwnerReadObject(r,r.Ptr(r.base+0x2D421B98));
    if(!Is(out.key,0x263D41B8))return out;
    const auto p=out.key.pointer;out.keyVbtableRva=r.Rva(r.Ptr(p+8));
    if(out.keyVbtableRva!=0x263D41E0||r.U32(r.base+0x263D41E4)!=0x20)return out;
    out.keyReference=OwnerReadObject(r,p+0x28);
    if(!Is(out.keyReference,0x263D41C8))return out;
    out.keyAddRefRva=r.Rva(r.Ptr(r.base+out.keyReference.vtable));
    out.keyReleaseRva=r.Rva(r.Ptr(r.base+out.keyReference.vtable+8));
    if(out.keyAddRefRva!=0x184E25C8||out.keyReleaseRva!=0x184E5D48)return out;
    out.keyId=r.Ptr(p+0x10);out.begin=r.Ptr(r.base+0x2D421EA8);out.end=r.Ptr(r.base+0x2D421EB0);
    if(!out.keyId||!out.begin||out.end<out.begin||(out.end-out.begin)%40||(out.end-out.begin)/40>128)return out;
    out.count=static_cast<uint32_t>((out.end-out.begin)/40);
    uintptr_t previousId=0;uint32_t previousVersion=0;
    for(unsigned i=0;i<out.count&&!r.failed&&!r.limited;++i){
        const auto entry=out.begin+i*40;const auto id=Intern(r,entry+0x20);const auto version=r.U32(entry+0x18);
        if(!id||(i&&(id<previousId||(id==previousId&&version<previousVersion))))return out;
        previousId=id;previousVersion=version;
        if(id==out.keyId){++out.matches;out.value=entry;out.version=version;}
    }
    // A unique version7 excludes the comparator's version0 value-comparison
    // branch. The default's scalar conversion cannot call an object method.
    if(r.failed||r.limited||out.matches!=1||out.version!=7)return out;
    out.tag=r.Byte(out.value+0x10);if(out.tag!=3)return out;
    out.booleanValue=r.Byte(out.value)!=0;out.booleanKnown=1;
    if(!r.failed&&!r.limited)out.state=ORead;return out;
}
OwnerDictionaryValue OwnerReadDictionary(OwnerReader& r, uintptr_t p, uint32_t key, bool identity) {
    OwnerDictionaryValue out{}; out.dictionary = OwnerReadObject(r, p);
    if (out.dictionary.state != ORead) return out;
    uint64_t cap = 0, count = 0; uintptr_t begin = 0;
    switch (out.dictionary.vtable) {
    case 0x263D95F8: cap = 4; break; case 0x263D9690: cap = 8; break;
    case 0x263D9728: cap = 16; break; case 0x263D97C0: cap = 32; break;
    case 0x263D9858: cap = 64; break;
    case 0x263D98F0: {
        begin = r.Ptr(p + 8); const auto end = r.Ptr(p + 0x10);
        if (!begin || end < begin || (end - begin) % 32 || (end - begin) / 32 > 128) { out.state = OInvalid; return out; }
        count = (end - begin) / 32; break;
    }
    default: out.state = OUnsupported; return out;
    }
    if (cap) { begin = p + 8; count = r.Ptr(begin + cap * 32); if (count > cap) { out.state = OInvalid; return out; } }
    out.count = static_cast<uint32_t>(count); out.wantedId = Intern(r, r.base + key);
    if (!out.wantedId) return out;
    unsigned matches = 0;
    for (uint64_t i = 0; i < count && !r.limited && !r.failed; ++i) {
        if (Intern(r, begin + i * 32) == out.wantedId) { ++matches; out.foundValue = begin + i * 32 + 8; }
    }
    if (r.failed || r.limited) return out;
    if (!matches) { out.state = OMissing; return out; }
    if (matches != 1) { out.state = OInvalid; return out; }
    out.tag = r.Byte(out.foundValue + 0x10); out.state = ORead;
    if (!identity) {
        // Conservative subset. Other present types remain unknown, not false.
        if (out.tag == 3) { out.booleanValue = r.Byte(out.foundValue) != 0; out.booleanKnown = 1; }
    } else if (out.tag == 7) {
        const auto contents = OwnerReadObject(r, r.Ptr(out.foundValue));
        if (Is(contents, 0x263D5648) || Is(contents, 0x263D56B8)) {
            out.valueId = Intern(r, contents.pointer + 0x18);
            const auto expected = Intern(r, r.base + 0x2D457F28);
            if (out.valueId && expected) { out.textIdentityKnown = 1; out.textIdentity = out.valueId == expected; }
        }
    }
    return out;
}
OwnerEndpoint OwnerReadEndpoint(OwnerReader& r, uintptr_t p, uintptr_t owner, uintptr_t block) {
    OwnerEndpoint out{}; out.parameter = OwnerReadObject(r, p); out.owner = OwnerReadObject(r, owner); out.block = block;
    if (!Is(out.parameter, 0x265C8B48) || !Is(out.owner, 0x265C9388) || !block ||
        owner < out.owner.colOffset || owner - out.owner.colOffset != p) return out;
    out.holder = r.Ptr(p + 0x38); const auto flags = r.Ptr(p + 0x60);
    out.bypass = OwnerReadDictionary(r, flags, 0x2D3F0F20, false);
    out.varying = OwnerReadDictionary(r, flags, 0x2D3F0EA0, false);
    if (!out.holder) return out;
    out.keyframeCount = r.Ptr(out.holder + 0x10);
    const bool noBypass = out.bypass.state == OMissing || (out.bypass.booleanKnown && !out.bypass.booleanValue);
    const bool noAnimation = out.keyframeCount == 0 || (out.varying.booleanKnown && !out.varying.booleanValue);
    if (!noBypass || !noAnimation || r.failed || r.limited) return out;
    double value = 0;
    if (!r.Get(out.holder + 0x50, &value) || !std::isfinite(value) || value < -1 || value > 100000000 || std::trunc(value) != value) return out;
    out.value = static_cast<int32_t>(value); out.state = ORead; return out;
}
bool OwnerVerifySpans(OwnerReader& r, uint32_t* failedRva) {
    *failedRva = 0;
    for (const auto& s : kOwnerSpans) {
        unsigned char b[32]{};
        if (!s.count||s.count>sizeof(b)||!r.Read(r.base+s.rva,b,s.count)||std::memcmp(b,s.bytes,s.count)) { *failedRva=s.rva; return false; }
    }
    for(const auto& s:kOwnerSlots){
        uintptr_t target=0;
        if(!r.Get(r.base+s.vtable+s.offset,&target)||target!=r.base+s.target){*failedRva=s.vtable+s.offset;return false;}
    }
    return true;
}
static uint32_t Slot(OwnerReader& r, const OwnerObject& o, uintptr_t off) {
    return o.state == ORead ? r.Rva(r.Ptr(r.base + o.vtable + off)) : 0;
}
static bool Empty(OwnerReader& r, uintptr_t sentinel) { return sentinel && r.Ptr(sentinel) == sentinel; }
static void Monitor(OwnerReader& r,const OwnerWindow& window,OwnerMonitor* out,bool commandMetadata) {
    if (!Is(window.property,0x27875E58) || !Is(window.delegate,0x27294F90) || !Is(window.monitor,0x272943F8) ||
        !Is(window.background,0x27292E88) || !window.delegateBlock || !window.backgroundBlock) return;
    out->background=window.background; const auto p=out->background.pointer;
    out->fourcc=r.U32(p+0x204); if(out->fourcc!=0x76636F6D)return;
    out->key=r.U32(p+0x3C4);out->sentinel=r.Ptr(p+0x3D0);out->buckets=r.Ptr(p+0x3E0);out->mask=r.Ptr(p+0x3F8);
    if(!out->sentinel||!out->buckets||out->mask>65535||((out->mask+1)&out->mask))return;
    uint64_t hash=14695981039346656037ULL;
    for(unsigned i=0;i<4;++i)hash=(hash^((out->key>>(i*8))&255))*1099511628211ULL;
    const auto bucket=out->buckets+(hash&out->mask)*16;
    out->bucketFirst=r.Ptr(bucket);out->bucketLast=r.Ptr(bucket+8);
    uintptr_t seen[16]{};auto entry=out->bucketLast;
    for(unsigned i=0;i<16&&entry&&entry!=out->sentinel&&!r.failed&&!r.limited;++i) {
        for(unsigned j=0;j<i;++j)if(seen[j]==entry){out->state=OInvalid;return;}
        seen[i]=entry;++out->entriesRead;
        if(r.U32(entry+0x10)==out->key){out->foundEntry=entry;break;}
        if(entry==out->bucketFirst)break;entry=r.Ptr(entry+8);
    }
    if(!out->foundEntry)return;
    out->manipulator=OwnerReadObject(r,r.Ptr(out->foundEntry+0x18));
    out->manipulatorOwner=OwnerReadObject(r,r.Ptr(out->foundEntry+0x20));out->sharedBlock=r.Ptr(out->foundEntry+0x28);
    if(!Is(out->manipulator,0x27996E48)||!Is(out->manipulatorOwner,0x279970E8)||!out->sharedBlock||
        out->manipulator.pointer<out->manipulator.colOffset||out->manipulatorOwner.pointer<out->manipulatorOwner.colOffset||
        out->manipulator.pointer-out->manipulator.colOffset!=out->manipulatorOwner.pointer-out->manipulatorOwner.colOffset||
        Slot(r,out->manipulator,0xA0)!=0x23975260)return;
    const auto g=out->manipulator.pointer;
    if(commandMetadata){
        uint8_t modeOverride=0;out->modeOverrideKnown=r.Get(g+0xA58,&modeOverride);out->modeOverride=modeOverride;
        uint8_t initEnabled=0,providerMode=0;
        out->initEnabledKnown=r.Get(g+0x142,&initEnabled);out->initEnabled=initEnabled;
        out->providerModeKnown=r.Get(g+0x1FD,&providerMode);out->providerMode=providerMode;
        out->providerModeGetterRva=Slot(r,out->manipulator,0x190);
        out->contextOwner=r.Ptr(g+0x980);out->contextBlock=r.Ptr(g+0x988);
    }
    out->selectedBegin=r.Ptr(g+0x9D8);out->selectedEnd=r.Ptr(g+0x9E0);out->selectedIndex=r.Ptr(g+0x9F0);
    if(!out->selectedBegin||out->selectedEnd<out->selectedBegin||(out->selectedEnd-out->selectedBegin)%0x58||
        (out->selectedEnd-out->selectedBegin)/0x58>128||out->selectedIndex>=(out->selectedEnd-out->selectedBegin)/0x58)return;
    const auto selected=out->selectedBegin+out->selectedIndex*0x58;
    if(commandMetadata)out->entryContextKnown=r.Get(selected+0x38,&out->entryContext);
    out->currentComponent=OwnerReadObject(r,r.Ptr(selected+8));out->currentOwner=OwnerReadObject(r,r.Ptr(selected+0x10));out->componentBlock=r.Ptr(selected+0x18);
    out->currentProvider=OwnerReadObject(r,r.Ptr(selected+0x20));out->providerOwner=r.Ptr(selected+0x28);out->providerBlock=r.Ptr(selected+0x30);
    if(Is(out->currentComponent,0x26662DA0)&&Is(out->currentOwner,0x266632C0)&&out->componentBlock&&
        out->currentComponent.pointer>=out->currentComponent.colOffset&&out->currentOwner.pointer>=out->currentOwner.colOffset&&
        out->currentComponent.pointer-out->currentComponent.colOffset==out->currentOwner.pointer-out->currentOwner.colOffset&&
        out->currentProvider.pointer&&out->providerOwner&&out->providerBlock){
        out->state=ORead;
        if(commandMetadata){
            out->componentFlagGetterRva=Slot(r,out->currentComponent,0x50);
            out->componentFlag=OwnerReadDictionary(r,r.Ptr(out->currentComponent.pointer+0x518),0x2D421B98,false);
            if(out->componentFlag.state==OMissing)out->defaultFlag=OwnerReadDefaultBypass(r);
        }
    }
}
static void Selection(OwnerReader& r, OwnerSelection* out) {
    const auto p = r.Ptr(r.base + 0x2D9FF6F8);
    out->manager = OwnerReadObject(r, p);
    out->managerOwner = r.Ptr(r.base + 0x2D9FF700); out->managerBlock = r.Ptr(r.base + 0x2D9FF708);
    if (!Is(out->manager, 0x2723DF48) || !out->managerOwner || !out->managerBlock) return;
    out->modeGetterRva=Slot(r,out->manager,0x68);
    out->managerStrongKnown=r.Get(out->managerBlock+8,&out->managerStrong);
    out->mode = r.U32(p + 0x158); out->state = ORead;
    out->componentCount = r.Ptr(p + 0x98); out->textCount = r.Ptr(p + 0xA8);
    if (out->componentCount > 0 && out->componentCount <= 128) {
        out->componentSentinel = r.Ptr(p + 0x90);
        if (!out->componentSentinel) return;
        out->componentFirst = r.Ptr(out->componentSentinel);
        if (!out->componentFirst || out->componentFirst == out->componentSentinel) return;
        const auto c = r.Ptr(out->componentFirst + 0x10);
        out->component = OwnerReadObject(r, c); out->componentOwner = OwnerReadObject(r, r.Ptr(out->componentFirst + 0x18));
        out->componentBlock = r.Ptr(out->componentFirst + 0x20);
        if (!Is(out->component, 0x26662DA0) || !Is(out->componentOwner, 0x266632C0) || !out->componentBlock ||
            out->componentOwner.pointer < out->componentOwner.colOffset || c < out->component.colOffset ||
            out->componentOwner.pointer - out->componentOwner.colOffset != c - out->component.colOffset) return;
        out->matchName = OwnerReadDictionary(r, r.Ptr(c + 0x688), 0x2D422098, true);
        if (!out->matchName.textIdentityKnown || !out->matchName.textIdentity) return;
        out->parametersBegin = r.Ptr(c + 0x548); out->parametersEnd = r.Ptr(c + 0x550);
        const auto begin = out->parametersBegin, end = out->parametersEnd;
        if (!begin || end < begin || (end - begin) % 24 || (end - begin) / 24 < 14 || (end - begin) / 24 > 128) return;
        for (unsigned i = 0; i < 2; ++i) {
            const auto entry = begin + (12 + i) * 24;
            out->endpoints[i] = OwnerReadEndpoint(r, r.Ptr(entry), r.Ptr(entry + 8), r.Ptr(entry + 16));
        }
    } else if (out->componentCount == 0 && out->textCount > 0 && out->textCount <= 128) {
        out->textSentinel = r.Ptr(p + 0xA0); if (!out->textSentinel) return;
        out->textFirst = r.Ptr(out->textSentinel); if (!out->textFirst || out->textFirst == out->textSentinel) return;
        out->textBlock = OwnerReadObject(r, r.Ptr(out->textFirst + 0x10));
        out->textOwner = r.Ptr(out->textFirst + 0x18); out->textShared = r.Ptr(out->textFirst + 0x20);
        if (!Is(out->textBlock, 0x26603510) || !out->textOwner || !out->textShared) return;
        int32_t values[2]{};
        if (r.Read(out->textBlock.pointer + 0x560, values, sizeof(values)) && values[0] >= -1 && values[1] >= -1 &&
            values[0] <= 100000000 && values[1] <= 100000000) {
            out->textEndpoints[0] = values[0]; out->textEndpoints[1] = values[1]; out->textEndpointState = ORead;
        }
    }
}
static void Collect(OwnerReader& r,const OwnerInputs& in,OwnerSnapshot* out,OwnerSnapshotKind kind) {
    std::memset(out, 0, sizeof(*out));
    out->kind=kind;const bool full=kind==OwnerSnapshotKind::Full;
    out->mainThreadId = r.U32(r.base + 0x2D386FE0);
    out->executor = r.Ptr(r.base + 0x2AAD6560); out->executorBlock = r.Ptr(r.base + 0x2AAD6568);
    out->windowCount = in.count <= kOwnerWindows ? in.count : kOwnerWindows;
    if(!full&&out->windowCount)out->windowCount=1;
    for (unsigned i = 0; i < out->windowCount; ++i) {
        auto& w = out->windows[i]; w.hwnd = in.windows[i].hwnd; w.property = OwnerReadObject(r, in.windows[i].property);
        if(w.property.state==ORead&&w.property.node){w.propertyToken=r.Ptr(w.property.pointer+0x10);w.propertyRefcount=r.U32(w.property.pointer+0x18);if(!w.propertyRefcount)++out->zeroRefcounts;}
        if (kind!=OwnerSnapshotKind::NativeEditText&&(Is(w.property, 0x26E182C8) || Is(w.property, 0x27875E58)) && Slot(r, w.property, 0x190) == 0x1E8E0880) {
            w.delegate = OwnerReadObject(r, r.Ptr(w.property.pointer + 0x6F8)); w.delegateBlock = r.Ptr(w.property.pointer + 0x700);
            if (Is(w.delegate, 0x27294F90)) {
                w.monitor = OwnerReadObject(r, r.Ptr(w.delegate.pointer + 0x60));
                if (Is(w.monitor, 0x272943F8) && Slot(r, w.monitor, 0x228) == 0x21399020) {
                    w.background = OwnerReadObject(r, r.Ptr(w.monitor.pointer + 0x220)); w.backgroundBlock = r.Ptr(w.monitor.pointer + 0x228);
                }
            }
        }
    }
    const auto m = r.Ptr(r.base + 0x2D3E0558); out->manager = OwnerReadObject(r, m);
    if(Is(out->manager,0x267421F8))out->focusMutationDepth=r.U32(m+0x120);
    if(kind==OwnerSnapshotKind::NativeEditText)return;
    if(out->windowCount)Monitor(r,out->windows[0],&out->monitor,full);
    Selection(r, &out->selection);  // Explicitly GLOBAL metadata, not focus ownership.
    if (!Is(out->manager, 0x267421F8)) return;
    out->focusToken = r.Ptr(m + 0x80); out->focusNode = r.Ptr(m + 0x88);
    const auto first = OwnerReadObject(r, out->focusNode);
    if (first.state == ORead && first.node && out->focusToken) out->focusPairMatched = r.Ptr(out->focusNode + 0x10) == out->focusToken;
    for (unsigned f = 0; full&&f < 2 && !r.failed && !r.limited; ++f) {
        auto& set = out->filters[f]; const auto off = f == 0 ? 0x48 : 0x30;
        set.begin = r.Ptr(m + off); set.end = r.Ptr(m + off + 8);
        if (set.end < set.begin || (set.end - set.begin) % 16 || (set.end - set.begin) / 16 > kOwnerFilters || (!set.begin && set.end)) { set.state = OInvalid; continue; }
        set.count = static_cast<uint32_t>((set.end - set.begin) / 16); set.state = ORead;
        for (unsigned i = 0; i < set.count && !r.failed && !r.limited; ++i) {
            auto& e = set.entries[i]; const auto entry = set.begin + i * 16;
            e.token = r.Ptr(entry); e.object = OwnerReadObject(r, r.Ptr(entry + 8));
            if (e.object.state != ORead || !e.object.node) continue;
            const auto p = e.object.pointer; e.selfToken = r.Ptr(p + 0x10); e.pairMatched = e.token && e.token == e.selfToken;
            e.refcount = r.U32(p + 0x18);
            if (!e.refcount) { ++out->zeroRefcounts; continue; }
            e.virtual88 = Slot(r, e.object, 0x88); e.filterRva = Slot(r, e.object, f == 0 ? 0x180 : 0x188);
            e.callback = r.Ptr(p + (f == 0 ? 0x410 : 0x3F8)); e.callbackEmpty = Empty(r, e.callback);
        }
    }
    if(full){
        out->special[0] = r.Ptr(m + 0xE8); out->special[1] = r.Ptr(m + 0x100); out->special[2] = r.Ptr(m + 0x108);
        out->specialTree = r.Ptr(m + 0xF0); out->specialTreeEmpty = Empty(r, out->specialTree);
        out->modifiers = r.Byte(m + 0x208); out->observerModifiers = r.Byte(r.base + 0x2D3E1370);
        out->observerEnabled = r.Byte(r.base + 0x2D3E1B19); out->observer = OwnerReadObject(r, r.Ptr(r.base + 0x2D3E1B38));
    }
    uintptr_t p = out->focusPairMatched ? out->focusNode : 0;
    for (unsigned i = 0; p && i < kOwnerParents && !r.failed && !r.limited; ++i) {
        bool cycle = false; for (unsigned j = 0; j < i; ++j) if (out->parents[j].object.pointer == p) cycle = true;
        if (cycle) { out->chainState = OInvalid; break; }
        auto& n = out->parents[out->parentCount++]; n.object = OwnerReadObject(r, p);
        if (Is(n.object, 0x26E0E7C8)) { n.terminal = 1; out->chainState = ORead; break; }
        if (n.object.state != ORead || !n.object.node) { out->chainState = OUnsupported; break; }
        n.refcount = r.U32(p + 0x18);
        if (!n.refcount) { ++out->zeroRefcounts; out->chainState = OUnknown; break; }
        n.selfToken = r.Ptr(p + 0x10); n.parentToken = r.Ptr(p + 0x1F0); n.parent = r.Ptr(p + 0x1F8);
        n.flags = r.U32(p + 0x204);
        if(full){n.callback=r.Ptr(p+0x428);n.callbackEmpty=Empty(r,n.callback);n.keyRva=Slot(r,n.object,0x190);}
        uintptr_t off = Is(n.object, 0x26E30F30) ? 0x520 : Is(n.object, 0x27878AA8) ? 0x540 :
            (Is(n.object, 0x26E182C8) || Is(n.object, 0x27875E58)) ? 0x6F8 : 0;
        if (off) { n.delegate = OwnerReadObject(r, r.Ptr(p + off)); n.delegateBlock = r.Ptr(p + off + 8); }
        if (full&&Is(n.object, 0x26E190E0)) {
            n.handlerManager = OwnerReadObject(r, r.Ptr(p + 0x620));
            if (Is(n.handlerManager, 0x26E0E7C8)) n.handler = OwnerReadObject(r, r.Ptr(n.handlerManager.pointer + 0xE8));
        }
        if (!n.parent && !n.parentToken) { out->chainState = ORead; break; }
        const auto parent = OwnerReadObject(r, n.parent);
        if (parent.state != ORead || (!parent.node && !Is(parent, 0x26E0E7C8)) || !n.parentToken || r.Ptr(n.parent + 0x10) != n.parentToken) { out->chainState = OInvalid; break; }
        n.parentMatched = 1; n.childrenBegin = r.Ptr(n.parent + 0xA0); n.childrenEnd = r.Ptr(n.parent + 0xA8);
        if (n.childrenEnd < n.childrenBegin || (n.childrenEnd - n.childrenBegin) % 8 || (n.childrenEnd - n.childrenBegin) / 8 > 4096 || (!n.childrenBegin && n.childrenEnd)) { out->chainState = OInvalid; break; }
        for (uintptr_t c = n.childrenBegin; c < n.childrenEnd && !r.failed && !r.limited; c += 8) if (r.Ptr(c) == p) n.parentContains = 1;
        if (!n.parentContains) { out->chainState = OInvalid; break; }
        p = n.parent;
        if (i + 1 == kOwnerParents) out->chainState = OInvalid;
    }
}
void OwnerCollect(OwnerReader& r,const OwnerInputs& in,OwnerSnapshot* out){Collect(r,in,out,OwnerSnapshotKind::Full);}
void OwnerCollectNativeEditText(OwnerReader& r,const OwnerInputs& in,OwnerSnapshot* out){Collect(r,in,out,OwnerSnapshotKind::NativeEditText);}
void OwnerCollectCaptionText(OwnerReader& r,const OwnerInputs& in,OwnerSnapshot* out){Collect(r,in,out,OwnerSnapshotKind::CaptionText);}
