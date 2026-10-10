#include "ae-owner-core.h"
#include <cstring>
#include <algorithm>
bool AeReader::Read(uintptr_t p,void* out,size_t n){
    if(out&&n&&n<=96)std::memset(out,0,n);
    if(failed||limited)return false;
    if(!out||!n||n>96||p<0x10000||p>UINTPTR_MAX-n){failed=true;return false;}
    if(qpcDeadline){LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now)||now.QuadPart>=qpcDeadline){limited=true;return false;}}
    if(reads>=4096||bytes+n>65536||GetTickCount64()>=deadline||(stop&&InterlockedCompareExchange(stop,0,0))){limited=true;return false;}
    ++reads;bytes+=static_cast<uint32_t>(n);
    if(!read(context,p,out,n)){failed=true;return false;}return true;
}
uintptr_t AeReader::Ptr(uintptr_t p){uintptr_t v=0;Get(p,&v);return v;}
uint32_t AeReader::U32(uintptr_t p){uint32_t v=0;Get(p,&v);return v;}
static int Module(const AeReader& r,uintptr_t p,size_t n){
    for(unsigned i=0;i<kAeModules;++i){const auto b=r.modules.base[i];const auto size=kAeModule[i].imageSize;if(b&&p>=b&&n<=size&&p-b<=size-n)return static_cast<int>(i);}return -1;
}
static const AeTypeContract* Contract(uint32_t module,uint32_t vt){
    const auto* begin=std::begin(kAeTypes);const auto* end=std::end(kAeTypes);
    const auto* found=std::lower_bound(begin,end,std::pair<uint32_t,uint32_t>{module,vt},[](const AeTypeContract& a,const auto& b){return a.module<b.first||(a.module==b.first&&a.vtable<b.second);});
    return found!=end&&found->module==module&&found->vtable==vt?found:nullptr;
}
AeObject AeReadObject(AeReader& r,uintptr_t p){
    AeObject o{};o.pointer=p;if(!p){o.state=AeMissing;return o;}
    o.vtable=r.Ptr(p);if(r.failed||r.limited){o.state=AeInvalid;return o;}
    const int mi=Module(r,o.vtable,8);if(mi<0||o.vtable<r.modules.base[mi]+8){o.state=AeUnsupported;return o;}
    o.module=static_cast<uint32_t>(mi)+1;const auto base=r.modules.base[mi];
    o.col=r.Ptr(o.vtable-8);uint32_t words[6]{};
    if(Module(r,o.col,sizeof(words))!=mi||!r.Read(o.col,words,sizeof(words))||words[0]!=1||words[1]>0x10000||words[2]!=0||
       words[5]!=o.col-base||Module(r,base+words[3],24)!=mi||Module(r,base+words[4],16)!=mi||p<words[1]){o.state=AeInvalid;return o;}
    o.colOffset=words[1];o.typeRva=words[3];o.complete=p-o.colOffset;o.state=AeMetadata;
    const auto* c=Contract(static_cast<uint32_t>(mi),static_cast<uint32_t>(o.vtable-base));
    if(!c)return o;
    if(c->col!=o.col-base||c->offset!=words[1]||c->type!=words[3]||c->chd!=words[4]){o.state=AeInvalid;return o;}
    o.state=AeVerifiedLayout;
    if(c->nodeOffset>=0&&o.complete<=UINTPTR_MAX-static_cast<uint32_t>(c->nodeOffset)){o.nodeKnown=1;o.node=o.complete+static_cast<uint32_t>(c->nodeOffset);}
    if(c->windowOffset>=0&&o.complete<=UINTPTR_MAX-static_cast<uint32_t>(c->windowOffset)){o.windowKnown=1;o.window=o.complete+static_cast<uint32_t>(c->windowOffset);}
    if(c->platformOffset>=0&&o.complete<=UINTPTR_MAX-static_cast<uint32_t>(c->platformOffset)){o.platformKnown=1;o.platform=o.complete+static_cast<uint32_t>(c->platformOffset);}
    if(c->managerOffset>=0&&o.complete<=UINTPTR_MAX-static_cast<uint32_t>(c->managerOffset)){o.managerKnown=1;o.manager=o.complete+static_cast<uint32_t>(c->managerOffset);}
    return o;
}
bool AeVerifySpans(AeReader& r,uint32_t* failedModule,uint32_t* failedRva){
    *failedModule=0;*failedRva=0;
    for(const auto& s:kAeSpans){unsigned char b[96]{};if(!r.modules.base[s.module]||!r.Read(r.modules.base[s.module]+s.rva,b,s.length)||std::memcmp(b,s.bytes,s.length)){
        *failedModule=s.module+1;*failedRva=s.rva;return false;
    }}return true;
}
static void AeCollectTree(AeReader& r,const AeInputs& in,AeSnapshot* out){
    *out={};if(in.count>8){out->chainState=AeInvalid;return;}out->windowCount=in.count;out->focusClass=in.focusClass;
    for(unsigned i=0;i<in.count&&!r.failed&&!r.limited;++i){auto& w=out->windows[i];w.hwnd=in.windows[i].hwnd;w.property=AeReadObject(r,in.windows[i].property);
        // The exported OS_Window override receives the secondary UI_Window this.
        // UI_GetPlatformView verifies OS_Window -> UI_Window adjustment +0x4D8.
        if(w.property.windowKnown&&w.property.platformKnown&&w.property.platform>=w.property.window&&w.property.platform-w.property.window==0x4D8){
            w.nativeHwnd=r.Ptr(w.property.platform+0x20);w.reciprocal=w.nativeHwnd==w.hwnd;
        }
    }
    out->manager=AeReadObject(r,r.Ptr(r.modules.base[0]+kAeManagerGlobal));const auto& m=out->manager;
    // The global is a UI_NodeManager pointer; derived types are allowed only when
    // their immutable CHD identifies that exact fixed base at the global pointer.
    if(m.state!=AeVerifiedLayout||!m.managerKnown||m.manager!=m.pointer){out->chainState=AeUnsupported;return;}
    out->mutationDepth=r.U32(m.manager+0x120);out->focusToken=r.Ptr(m.manager+0x80);out->focusNode=r.Ptr(m.manager+0x88);
    uintptr_t p=out->focusNode;
    for(unsigned i=0;p&&i<8&&!r.failed&&!r.limited;++i){
        for(unsigned j=0;j<i;++j)if(out->nodes[j].object.pointer==p){out->chainState=AeInvalid;return;}
        auto& n=out->nodes[out->nodeCount++];n.object=AeReadObject(r,p);
        if(!n.object.nodeKnown||n.object.node!=p){out->chainState=AeUnsupported;return;}
        n.selfToken=r.Ptr(p+0x10);n.parentToken=r.Ptr(p+0x1F0);n.parentPointer=r.Ptr(p+0x1F8);
        if(!i){out->selfToken=n.selfToken;out->focusPair=n.selfToken&&n.selfToken==out->focusToken;}
        if(!n.parentToken&&!n.parentPointer){out->chainState=AeMetadata;return;}
        n.parent=AeReadObject(r,n.parentPointer);
        if(n.parent.nodeKnown&&n.parent.node==n.parentPointer&&n.parentToken)n.parentPair=r.Ptr(n.parentPointer+0x10)==n.parentToken;
        if(!n.parentPair){out->chainState=AeInvalid;return;}p=n.parentPointer;
    }
    out->chainState=p?AeUnsupported:AeMissing;
}
static bool ExactCanvasType(const AeObject& o,unsigned index,uint32_t offset=0){
    const auto& t=kAeCanvasType[index];
    return o.state==AeVerifiedLayout&&o.module==t.module+1&&o.typeRva==t.type&&o.colOffset==offset;
}
static bool AuditedLayer(const AeObject& o){
    // The exact sampled BEE_TextLayer has a fixed, nonvirtual BEE_Layer base
    // at zero. The generator rejects a different base and loaded spans check it.
    return ExactCanvasType(o,6)||ExactCanvasType(o,7);
}
void AeCollectCanvas(AeReader& r,const AeSnapshot& s,AeCanvas* out){
    *out={};
    if(r.failed||r.limited||!s.focusPair||s.mutationDepth||!s.nodeCount||!s.windowCount)return;
    const auto& f=s.nodes[0].object;const auto& w=s.windows[0];
    if(!ExactCanvasType(f,0,0x38)||f.pointer!=s.focusNode||!f.nodeKnown||f.node!=s.focusNode||
       !w.reciprocal||w.property.node!=s.focusNode||w.property.pointer!=f.pointer)return;
    out->stage=1;out->dir=f.complete;
    out->mru=r.Ptr(out->dir+0x8A0);out->commandDelegate=r.Ptr(f.node+0x700);
    out->pano=AeReadObject(r,out->mru);out->stage=2;
    if(!ExactCanvasType(out->pano,1)||r.failed||r.limited)return;
    out->backDir=r.Ptr(out->pano.pointer+0x310);
    out->ownerPair=out->backDir==out->dir&&out->commandDelegate==out->pano.pointer+0x20;
    if(!out->ownerPair||r.failed||r.limited)return;
    out->item=AeReadObject(r,r.Ptr(out->dir+0x888));out->stage=3;
    // This first experiment covers only CComposition, not the CLayer fallback.
    if(!ExactCanvasType(out->item,2)||r.failed||r.limited)return;
    out->beeComp=AeReadObject(r,r.Ptr(out->item.pointer+0x58));out->stage=4;
    if(!ExactCanvasType(out->beeComp,3)||r.failed||r.limited)return;
    out->project=AeReadObject(r,r.Ptr(out->beeComp.pointer+0x38));out->stage=5;
    if(!ExactCanvasType(out->project,4)||r.failed||r.limited)return;
    const auto undoImpl=r.Ptr(out->project.pointer+0x370);if(!undoImpl||undoImpl>UINTPTR_MAX-0x68)return;
    out->undo=undoImpl+0x10;out->currentTransaction=r.Ptr(out->undo+0x58);
    out->transaction=AeReadObject(r,out->currentTransaction);out->stage=6;
    if(!ExactCanvasType(out->transaction,5)||r.failed||r.limited)return;
    out->layer=AeReadObject(r,r.Ptr(out->transaction.pointer+0x40));out->stage=7;
    if(!AuditedLayer(out->layer)||r.failed||r.limited)return;
    out->layerComp=r.Ptr(out->layer.pointer+0x260);out->compositionMatch=out->layerComp==out->beeComp.pointer;
    if(!out->compositionMatch||r.failed||r.limited)return;
    out->stage=8;
    // Current ownership needs no time conversion, entered text, or edit call.
    // The v4 observer retains the separately audited diagnostic time reads.
}
void AeCollect(AeReader& r,const AeInputs& in,AeSnapshot* out){
    AeCollectTree(r,in,out);AeCollectCanvas(r,*out,&out->canvas);AeCollectTimeline(r,*out,&out->timeline);
}
AeOwnerEvidence AeClassifyCanvasOwner(const AeSnapshot& first,const AeSnapshot& second,bool stableCapture){
    if(!stableCapture)return {AeOwnerUnknown,AeOwnerInvalidCapture};
    if(std::memcmp(&first,&second,sizeof(first)))return {AeOwnerUnknown,AeOwnerChanged};
    const auto& s=first;
    if(s.mutationDepth||s.focusPair!=1||!s.focusToken||s.selfToken!=s.focusToken||
       !s.nodeCount||s.nodeCount>8||!s.windowCount||s.windowCount>8||s.nodes[0].selfToken!=s.focusToken)
        return {AeOwnerUnknown,AeOwnerInvalidFocus};
    const auto& f=s.nodes[0].object;const auto& w=s.windows[0];const auto& c=s.canvas;
    if(!ExactCanvasType(f,0,0x38)||f.pointer!=s.focusNode||!f.nodeKnown||f.node!=s.focusNode||
       w.reciprocal!=1||!w.hwnd||w.nativeHwnd!=w.hwnd||w.property.pointer!=f.pointer||
       !w.property.nodeKnown||w.property.node!=s.focusNode||!ExactCanvasType(w.property,0,0x38))
        return {AeOwnerUnknown,AeOwnerInvalidFocus};
    if(c.stage<6||c.stage>8||c.ownerPair!=1||c.dir!=f.complete||c.mru!=c.pano.pointer||
       c.backDir!=c.dir||!ExactCanvasType(c.pano,1)||c.pano.pointer>UINTPTR_MAX-0x20||
       c.commandDelegate!=c.pano.pointer+0x20||!ExactCanvasType(c.item,2)||
       !ExactCanvasType(c.beeComp,3)||!ExactCanvasType(c.project,4)||!c.undo)
        return {AeOwnerUnknown,AeOwnerInvalidCanvas};
    // Explicit absence in the current focused panel's own project is distinct
    // from a failed read, unsupported transaction, or another composition.
    if(c.stage==6&&!c.currentTransaction&&!c.transaction.pointer&&c.transaction.state==AeMissing)
        return {AeOwnerCommand,AeOwnerExplicitNoTransaction};
    if(!c.currentTransaction||c.transaction.pointer!=c.currentTransaction||!ExactCanvasType(c.transaction,5))
        return {AeOwnerUnknown,AeOwnerUnsupportedTransaction};
    if(c.stage!=8||!AuditedLayer(c.layer)||c.compositionMatch!=1||c.layerComp!=c.beeComp.pointer)
        return {AeOwnerUnknown,AeOwnerUnboundLayer};
    // This is the conservative HasActiveTextEdit(false) ownership guard. It
    // preserves text handling even if the additional true/time condition fails.
    // The sampled times are diagnostic only; no full time conversion is claimed.
    return {AeOwnerText,AeOwnerBoundTextTransaction};
}
static bool TimelineFocusBound(const AeSnapshot& s){
    if(s.focusClass!=AeNativeFocusOther||s.mutationDepth||s.focusPair!=1||!s.focusToken||s.selfToken!=s.focusToken||
       !s.nodeCount||s.nodeCount>8||!s.windowCount||s.windowCount>8||s.nodes[0].selfToken!=s.focusToken)return false;
    const auto& f=s.nodes[0].object;const auto& w=s.windows[0];
    return ExactCanvasType(f,8,0x38)&&f.pointer==s.focusNode&&f.nodeKnown&&f.node==s.focusNode&&
        w.reciprocal==1&&w.hwnd&&w.nativeHwnd==w.hwnd&&w.property.pointer==f.pointer&&
        w.property.nodeKnown&&w.property.node==s.focusNode&&ExactCanvasType(w.property,8,0x38);
}
void AeCollectTimeline(AeReader& r,const AeSnapshot& s,AeTimeline* out){
    *out={};if(r.failed||r.limited||!TimelineFocusBound(s))return;
    out->dir=s.nodes[0].object.complete;out->primary=AeReadObject(r,out->dir);
    if(!ExactCanvasType(out->primary,8)||out->primary.complete!=out->dir||r.failed||r.limited)return;
    // CTLDir primary slot B0 (B2EE50) returns complete+7C8. The audited
    // GetItem/GetCompContext path dereferences twice, then casts that CItem.
    out->stage=1;out->itemRef=r.Ptr(out->dir+0x7C8);if(!out->itemRef||r.failed||r.limited)return;
    out->itemBox=r.Ptr(out->itemRef);if(!out->itemBox||r.failed||r.limited)return;
    out->currentItem=r.Ptr(out->itemBox);out->item=AeReadObject(r,out->currentItem);out->stage=2;
    // The separate CLayer+240 fallback is intentionally unsupported here.
    if(!ExactCanvasType(out->item,2)||r.failed||r.limited)return;
    out->beeComp=AeReadObject(r,r.Ptr(out->item.pointer+0x58));out->stage=3;
    if(!ExactCanvasType(out->beeComp,3)||r.failed||r.limited)return;
    out->project=AeReadObject(r,r.Ptr(out->beeComp.pointer+0x38));out->stage=4;
    if(!ExactCanvasType(out->project,4)||r.failed||r.limited)return;
    const auto undoImpl=r.Ptr(out->project.pointer+0x370);if(!undoImpl||undoImpl>UINTPTR_MAX-0x68)return;
    out->undo=undoImpl+0x10;out->currentTransaction=r.Ptr(out->undo+0x58);
    out->transaction=AeReadObject(r,out->currentTransaction);out->stage=5;
}
AeOwnerEvidence AeClassifyTimelineOwner(const AeSnapshot& a,const AeSnapshot& b,bool valid){
    if(!valid)return {AeOwnerUnknown,AeOwnerInvalidCapture};
    if(std::memcmp(&a,&b,sizeof(a)))return {AeOwnerUnknown,AeOwnerChanged};
    if(!TimelineFocusBound(a))return {AeOwnerUnknown,AeOwnerInvalidFocus};
    const auto& c=a.timeline;
    if(c.stage!=5||c.dir!=a.nodes[0].object.complete||!ExactCanvasType(c.primary,8)||
       c.primary.pointer!=c.dir||!c.itemRef||!c.itemBox||c.currentItem!=c.item.pointer||
       !ExactCanvasType(c.item,2)||!ExactCanvasType(c.beeComp,3)||!ExactCanvasType(c.project,4)||!c.undo)
        return {AeOwnerUnknown,AeOwnerInvalidTimeline};
    if(!c.currentTransaction&&!c.transaction.pointer&&c.transaction.state==AeMissing)
        return {AeOwnerCommand,AeOwnerExplicitNoTimelineTransaction};
    // A text transaction in the same composition need not belong to the
    // timeline. Only explicit absence can grant current command ownership.
    return {AeOwnerUnknown,AeOwnerUnsupportedTransaction};
}
AeOwnerEvidence AeClassifyCurrentOwner(const AeSnapshot& a,const AeSnapshot& b,bool valid){
    if(a.nodeCount&&ExactCanvasType(a.nodes[0].object,8,0x38))return AeClassifyTimelineOwner(a,b,valid);
    return AeClassifyCanvasOwner(a,b,valid);
}
