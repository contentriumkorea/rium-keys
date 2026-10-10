#include "ae-owner-core.h"
#include "ae-owner-runtime-policy.h"
#include <map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
struct Memory {
    std::map<uintptr_t,unsigned char> bytes;
    template<class T> void Put(uintptr_t p,T value){const auto* b=reinterpret_cast<const unsigned char*>(&value);for(size_t i=0;i<sizeof(value);++i)bytes[p+i]=b[i];}
    static bool Read(void* c,uintptr_t p,void* out,size_t n){auto& m=*static_cast<Memory*>(c);auto* b=static_cast<unsigned char*>(out);for(size_t i=0;i<n;++i){auto x=m.bytes.find(p+i);if(x==m.bytes.end())return false;b[i]=x->second;}return true;}
};
static int checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
int main(){
    Memory m;AeModules mods{};for(unsigned i=0;i<kAeModules;++i)mods.base[i]=0x100000000ULL+i*0x10000000ULL;
    AeReader r{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};
    const AeTypeContract* chosen=nullptr;for(const auto& c:kAeTypes)if(c.module==0&&c.nodeOffset==0&&c.offset==0){chosen=&c;break;}
    if(!chosen){std::puts("FAIL no static node contract");return 1;}
    const auto& c=*chosen;const uintptr_t object=0x70000000,base=mods.base[0];
    m.Put(object,base+c.vtable);m.Put(base+c.vtable-8,base+c.col);
    const uint32_t col[6]={1,c.offset,0,c.type,c.chd,c.col};for(unsigned i=0;i<6;++i)m.Put(base+c.col+i*4,col[i]);
    auto o=AeReadObject(r,object);Check(o.state==AeVerifiedLayout&&o.nodeKnown&&o.node==object,"known-node-layout");
    r.failed=false;m.Put(base+c.col+20,c.col+4);o=AeReadObject(r,object);Check(o.state==AeInvalid,"bad-col-self-rejected");
    m.Put(base+c.col+20,c.col);r.failed=false;m.Put(object,uintptr_t(0x90000000));o=AeReadObject(r,object);Check(o.state==AeUnsupported&&!o.nodeKnown,"foreign-vtable-not-dereferenced");
    r.failed=false;unsigned char b=0;Check(!r.Read(1,&b,1)&&r.failed,"low-address-rejected");
    r.failed=false;r.deadline=GetTickCount64();Check(!r.Read(object,&b,1)&&r.limited,"expired-budget");
    const AeTypeContract *manager=nullptr,*window=nullptr;
    for(const auto& t:kAeTypes){if(t.module==0&&t.type==kAeModule[0].managerType&&t.offset==0)manager=&t;if(t.module==0&&t.nodeOffset==0&&t.windowOffset==0&&t.offset==0)window=&t;}
    if(!manager||!window){std::puts("FAIL missing manager/window static contracts");return 1;}
    auto mapObject=[&](uintptr_t addr,const AeTypeContract& t){const auto image=mods.base[t.module];m.Put(addr,image+t.vtable);m.Put(image+t.vtable-8,image+t.col);const uint32_t words[6]={1,t.offset,0,t.type,t.chd,t.col};for(unsigned j=0;j<6;++j)m.Put(image+t.col+j*4,words[j]);};
    const uintptr_t mgr=0x71000000,node=0x72000000,win=0x73000000;
    mapObject(mgr,*manager);mapObject(node,c);mapObject(win,*window);
    m.Put(base+kAeManagerGlobal,mgr);m.Put(mgr+0x120,uint32_t(0));m.Put(mgr+0x80,uintptr_t(0x123456));m.Put(mgr+0x88,node);
    m.Put(node+0x10,uintptr_t(0x123456));m.Put(node+0x1f0,uintptr_t(0x654321));m.Put(node+0x1f8,win);
    m.Put(win+0x10,uintptr_t(0x654321));m.Put(win+0x1f0,uintptr_t(0));m.Put(win+0x1f8,uintptr_t(0));m.Put(win+0x4f8,uintptr_t(0x9988));
    AeInputs in{};in.count=1;in.windows[0]={0x9988,win};AeSnapshot a{},second{};
    auto collect=[&](AeSnapshot& out){AeReader rr{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};AeCollect(rr,in,&out);return !rr.failed&&!rr.limited;};
    Check(collect(a)&&a.focusPair&&a.nodeCount==2&&a.windows[0].reciprocal&&a.nodes[0].parentPair,"owned-fixture-numeric-chain");
    Check(collect(second)&&!std::memcmp(&a,&second,sizeof(a)),"two-identical-metadata-snapshots");
    m.Put(node+0x10,uintptr_t(0x777));Check(collect(second)&&!second.focusPair&&std::memcmp(&a,&second,sizeof(a)),"changed-weak-identity-observed");m.Put(node+0x10,uintptr_t(0x123456));
    m.Put(win+0x4f8,uintptr_t(0x9999));Check(collect(second)&&!second.windows[0].reciprocal,"wrong-native-reciprocal-observed");m.Put(win+0x4f8,uintptr_t(0x9988));
    m.Put(mgr+0x120,uint32_t(1));Check(collect(second)&&second.mutationDepth==1,"transition-depth-not-hidden");m.Put(mgr+0x120,uint32_t(0));
    // Hand-checked installed COLs from the first actual AE timeline sample.
    // The old OS_Window+0x20 read sees ThemeOwner's vtable, not the HWND.
    const AeTypeContract* timeline=nullptr;
    for(const auto& t:kAeTypes)if(t.module==1&&t.vtable==0x1ABD870)timeline=&t;
    if(!timeline){std::puts("FAIL missing sampled timeline static contract");return 1;}
    const uintptr_t timelineComplete=0x74000000,timelineOs=timelineComplete+0x38;
    mapObject(timelineOs,*timeline);
    m.Put(timelineOs+0x20,mods.base[1]+0x1ABDE98);
    m.Put(timelineComplete+0x510+0x20,uintptr_t(0x9988));
    in.windows[0].property=timelineOs;
    Check(collect(second)&&second.windows[0].nativeHwnd==0x9988&&second.windows[0].reciprocal,
        "sampled-secondary-ui-window-hwnd-not-theme-vtable");
    in.windows[0].property=win;
    const AeTypeContract workspaceManager{1,0x1A38910,0x1BDE240,0,0x20A13A0,0x1BDE268,-1,-1,-1,0};
    mapObject(mgr,workspaceManager);
    Check(collect(second)&&second.focusNode==node&&second.focusPair&&second.nodeCount==2,
        "sampled-derived-manager-keeps-verified-base-layout");
    m.Put(mods.base[1]+workspaceManager.col+12,uint32_t(0x20A13A8));
    Check(collect(second)&&!second.focusNode&&second.chainState==AeUnsupported,
        "derived-manager-wrong-col-type-never-reads-focus");
    mapObject(mgr,*manager);
    m.Put(win+0x1f0,uintptr_t(0x123456));m.Put(win+0x1f8,node);Check(collect(second)&&second.chainState==AeInvalid,"parent-cycle-bounded");
    AeReader stopped{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};volatile LONG stop=1;stopped.stop=&stop;Check(!stopped.Read(node,&b,1)&&stopped.limited,"stop-before-read");
    AeReader full{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};full.reads=4096;Check(!full.Read(node,&b,1)&&full.limited,"read-budget-enforced");
    auto exactContract=[&](uint32_t mi,uint32_t vt)->const AeTypeContract&{
        for(const auto& t:kAeTypes)if(t.module==mi&&t.vtable==vt)return t;
        std::printf("FAIL missing sampled static contract %u:%x\n",mi,vt);std::abort();
    };
    const uintptr_t dir=0x75000000,pano=0x75100000,item=0x75200000,beeComp=0x75300000,project=0x75400000,undoImpl=0x75500000,tx=0x75600000,layer=0x75700000;
    mapObject(dir+0x38,exactContract(1,0x19BF9A0));mapObject(pano,exactContract(1,0x19B1E20));
    mapObject(item,exactContract(1,0x19A2DB8));mapObject(beeComp,exactContract(6,0xA1E0F0));
    mapObject(project,exactContract(6,0xA21E28));mapObject(tx,exactContract(6,0xA6AF38));mapObject(layer,exactContract(6,0x9F63C8));
    m.Put(dir+0x8A0,pano);m.Put(dir+0x38+0x700,pano+0x20);m.Put(pano+0x310,dir);m.Put(dir+0x888,item);
    m.Put(item+0x58,beeComp);m.Put(beeComp+0x38,project);m.Put(project+0x370,undoImpl);m.Put(undoImpl+0x10+0x58,tx);
    m.Put(tx+0x40,layer);m.Put(layer+0x260,beeComp);m.Put(beeComp+8,uint16_t(0xBEE1));m.Put(beeComp+0x48,uint16_t(4));
    m.Put(beeComp+0x280,AeTime{300,600});m.Put(tx+0x48,AeTime{30,60});m.Put(layer+0x280,AeTime{0,600});m.Put(layer+0x288,uint32_t(1));m.Put(layer+0x28C,uint32_t(1));
    AeSnapshot scoped{};scoped.focusNode=dir+0x38;scoped.focusPair=1;scoped.nodeCount=1;scoped.windowCount=1;
    AeReader objectReader{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};scoped.nodes[0].object=AeReadObject(objectReader,dir+0x38);
    scoped.windows[0].property=scoped.nodes[0].object;scoped.windows[0].reciprocal=1;
    AeCanvas canvas{};
    auto canvasCollect=[&](){AeReader rr{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};AeCollectCanvas(rr,scoped,&canvas);return !rr.failed&&!rr.limited;};
    Check(canvasCollect()&&canvas.ownerPair&&canvas.compositionMatch&&canvas.currentTransaction==tx&&canvas.layerComp==beeComp,
        "canvas-transaction-is-bound-to-focused-panel");
    Check(canvas.stage==8&&!canvas.timeRead,"production-stops-at-bound-owner-without-time-reads");
    m.Put(dir+0x38+0x700,pano+0x28);Check(canvasCollect()&&!canvas.ownerPair&&!canvas.item.pointer,"canvas-wrong-command-delegate-blocks-item-read");m.Put(dir+0x38+0x700,pano+0x20);
    m.Put(pano+0x310,dir+8);Check(canvasCollect()&&!canvas.ownerPair&&!canvas.item.pointer,"canvas-wrong-backlink-blocks-item-read");m.Put(pano+0x310,dir);
    m.Put(layer+0x260,uintptr_t(0x75800000));Check(canvasCollect()&&!canvas.compositionMatch&&!canvas.timeRead,"canvas-other-composition-transaction-is-not-current-owner");m.Put(layer+0x260,beeComp);
    m.Put(undoImpl+0x68,uintptr_t(0));Check(canvasCollect()&&!canvas.currentTransaction&&!canvas.compositionMatch,"canvas-null-transaction-is-explicit-absence");m.Put(undoImpl+0x68,tx);
    mapObject(tx,exactContract(6,0x9F63C8));Check(canvasCollect()&&!canvas.layer.pointer&&!canvas.compositionMatch,"canvas-nontext-transaction-blocks-layer-read");mapObject(tx,exactContract(6,0xA6AF38));
    m.bytes.erase(tx+0x48);m.bytes.erase(layer+0x280);m.bytes.erase(beeComp+0x280);
    Check(canvasCollect()&&canvas.stage==8&&canvas.compositionMatch&&!canvas.timeRead,
        "production-unreadable-diagnostic-times-cannot-revoke-bound-text-owner");
    m.Put(tx+0x48,AeTime{30,60});m.Put(layer+0x280,AeTime{0,600});m.Put(beeComp+0x280,AeTime{300,600});
    scoped.mutationDepth=1;Check(canvasCollect()&&!canvas.stage,"canvas-focus-transition-does-not-read-owner-chain");scoped.mutationDepth=0;
    scoped.windows[0].reciprocal=0;Check(canvasCollect()&&!canvas.stage,"canvas-native-owner-mismatch-does-not-read-owner-chain");
    scoped.windows[0].reciprocal=1;
    scoped.focusToken=0x276;scoped.selfToken=0x276;scoped.nodes[0].selfToken=0x276;
    scoped.windows[0].hwnd=0x8B2338;scoped.windows[0].nativeHwnd=0x8B2338;
    // Literal installed COL from the actual v3 text sample. BEE_Layer is a
    // nonvirtual offset-zero base of this BEE_TextLayer, not an exact base object.
    const AeTypeContract textLayer{6,0xA30D20,0xB0AA90,0,0xE598D8,0xB0AAB8,-1,-1,-1,-1};
    mapObject(layer,textLayer);
    Check(canvasCollect()&&canvas.stage==8&&canvas.compositionMatch&&!canvas.timeRead,
        "sampled-text-layer-preserves-verified-base-fields");
    auto classify=[&](bool valid=true){const bool readOk=canvasCollect();scoped.canvas=canvas;return AeClassifyCanvasOwner(scoped,scoped,valid&&readOk);};
    auto evidence=classify();
    Check(evidence.decision==AeOwnerText&&evidence.reason==AeOwnerBoundTextTransaction,
        "owner-text-requires-current-canvas-bound-text-transaction");
    const auto textSnapshot=scoped;
    m.Put(undoImpl+0x68,uintptr_t(0));evidence=classify();
    Check(evidence.decision==AeOwnerCommand&&evidence.reason==AeOwnerExplicitNoTransaction,
        "owner-command-requires-explicit-null-transaction");
    Check(AeClassifyCanvasOwner(textSnapshot,scoped,true).decision==AeOwnerUnknown,
        "owner-changing-transaction-between-snapshots-is-unknown");
    Check(AeClassifyCanvasOwner(scoped,scoped,false).decision==AeOwnerUnknown,
        "owner-null-from-invalid-capture-is-not-command");
    m.bytes.erase(undoImpl+0x68);evidence=classify();
    Check(evidence.decision==AeOwnerUnknown,"owner-failed-transaction-read-is-not-command");m.Put(undoImpl+0x68,tx);
    m.Put(layer+0x260,uintptr_t(0x75800000));evidence=classify();
    Check(evidence.decision==AeOwnerUnknown,"owner-other-composition-text-transaction-is-unknown");m.Put(layer+0x260,beeComp);
    mapObject(tx,exactContract(6,0x9F63C8));evidence=classify();
    Check(evidence.decision==AeOwnerUnknown,"owner-nontext-transaction-is-not-null-transaction");mapObject(tx,exactContract(6,0xA6AF38));
    m.Put(layer,uintptr_t(0x90000000));evidence=classify();
    Check(evidence.decision==AeOwnerUnknown&&!canvas.layerComp&&!canvas.timeRead,
        "owner-foreign-layer-never-reads-layer-fields");mapObject(layer,textLayer);
    m.Put(mods.base[6]+textLayer.col+12,uint32_t(textLayer.type+8));evidence=classify();
    Check(evidence.decision==AeOwnerUnknown&&!canvas.layerComp,
        "owner-corrupt-text-layer-col-blocks-fields");mapObject(layer,textLayer);
    m.Put(pano+0x310,dir+8);evidence=classify();
    Check(evidence.decision==AeOwnerUnknown,"owner-broken-pano-backlink-is-unknown");m.Put(pano+0x310,dir);
    scoped.focusPair=0;evidence=classify();Check(evidence.decision==AeOwnerUnknown,"owner-no-focus-pair-is-unknown");scoped.focusPair=1;
    scoped.nodes[0].selfToken=0x277;evidence=classify();Check(evidence.decision==AeOwnerUnknown,"owner-stale-node-identity-is-unknown");scoped.nodes[0].selfToken=0x276;
    scoped.windows[0].nativeHwnd=0x9999;evidence=classify();Check(evidence.decision==AeOwnerUnknown,"owner-mismatched-native-window-is-unknown");scoped.windows[0].nativeHwnd=0x8B2338;
    scoped.mutationDepth=1;evidence=classify();Check(evidence.decision==AeOwnerUnknown,"owner-mutation-window-is-unknown");scoped.mutationDepth=0;
    m.Put(beeComp+0x280,AeTime{301,600});evidence=classify();
    Check(evidence.decision==AeOwnerText,"owner-conservative-text-does-not-assert-full-time-equality");m.Put(beeComp+0x280,AeTime{300,600});
    m.Put(tx+0x48,AeTime{30,0});evidence=classify();
    Check(evidence.decision==AeOwnerText,"owner-conservative-text-guard-does-not-evaluate-time-fields");m.Put(tx+0x48,AeTime{30,60});
    LARGE_INTEGER expiredQpc{};QueryPerformanceCounter(&expiredQpc);
    AeReader qpcReader{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};qpcReader.qpcDeadline=expiredQpc.QuadPart;
    Check(!qpcReader.Read(node,&b,1)&&qpcReader.limited,"production-qpc-budget-expires-before-a-read");
    const HWND nativeFocus=reinterpret_cast<HWND>(scoped.windows[0].hwnd);
    RiumOwnerStamp stamp{};classify();
    AeMakeOwnerStamp(scoped,scoped,true,true,101,202,nativeFocus,&stamp);
    Check(stamp.provider==3&&stamp.profile==1&&stamp.kind==RIUM_OWNER_TEXT&&stamp.processId==101&&stamp.threadId==202&&stamp.focus==nativeFocus,
        "runtime-text-stamp-retains-provider-and-native-scope");
    Check(stamp.windowObject==scoped.focusNode&&stamp.logicalObject==layer&&stamp.textObject==tx&&stamp.lifetimeToken==scoped.focusToken&&!stamp.deferredSafe,
        "runtime-text-stamp-binds-layer-and-transaction-without-deferred-permission");
    const auto textStamp=stamp;const auto textOwner=scoped;
    AeMakeOwnerStamp(scoped,scoped,true,false,101,202,nativeFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_TEXT,"runtime-text-ownership-does-not-depend-on-letter-modifiers");
    m.Put(undoImpl+0x68,uintptr_t(0));classify();
    AeMakeOwnerStamp(scoped,scoped,true,true,101,202,nativeFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_COMMAND&&stamp.logicalObject==scoped.focusNode&&!stamp.textObject&&!stamp.deferredSafe,
        "runtime-command-stamp-has-no-text-target");
    AeMakeOwnerStamp(scoped,scoped,true,false,101,202,nativeFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_UNKNOWN&&stamp.provider==3&&!stamp.textObject,
        "runtime-command-outside-plain-letter-domain-is-unknown");
    std::memset(&stamp,0xA5,sizeof(stamp));AeMakeOwnerStamp(scoped,scoped,false,true,101,202,nativeFocus,&stamp);
    Check(stamp.provider==3&&stamp.profile==1&&stamp.kind==RIUM_OWNER_UNKNOWN&&!stamp.windowObject&&!stamp.logicalObject&&!stamp.textObject&&!stamp.lifetimeToken&&!stamp.deferredSafe,
        "runtime-failed-capture-clears-stale-object-identity-but-keeps-provider");
    AeMakeOwnerStamp(scoped,scoped,true,true,101,202,reinterpret_cast<HWND>(0x9999),&stamp);
    Check(stamp.kind==RIUM_OWNER_UNKNOWN,"runtime-different-native-focus-cannot-reuse-snapshot");
    AeMakeOwnerStamp(scoped,scoped,true,true,0,202,nativeFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_UNKNOWN,"runtime-invalid-native-process-is-unknown");
    auto otherText=textOwner;otherText.canvas.currentTransaction+=0x100;otherText.canvas.transaction.pointer+=0x100;otherText.canvas.transaction.complete+=0x100;
    AeMakeOwnerStamp(otherText,otherText,true,true,101,202,nativeFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_TEXT&&stamp.textObject!=textStamp.textObject&&std::memcmp(&stamp,&textStamp,sizeof(stamp)),
        "runtime-new-text-transaction-changes-owner-stamp");
    otherText=textOwner;otherText.canvas.layer.pointer+=0x100;otherText.canvas.layer.complete+=0x100;
    AeMakeOwnerStamp(otherText,otherText,true,true,101,202,nativeFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_TEXT&&stamp.logicalObject!=textStamp.logicalObject&&std::memcmp(&stamp,&textStamp,sizeof(stamp)),
        "runtime-different-text-layer-changes-owner-stamp");
    AeMakeOwnerStamp(textOwner,otherText,true,true,101,202,nativeFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_UNKNOWN,"runtime-mixed-owner-snapshots-cannot-produce-text-stamp");
    AeMakeOwnerStamp(scoped,scoped,true,true,101,202,nativeFocus,nullptr);
    mapObject(timelineComplete,exactContract(1,0x1ABD6C8));
    const uintptr_t itemRef=0x75900000,itemBox=0x75901000;
    m.Put(timelineComplete+0x7C8,itemRef);m.Put(itemRef,itemBox);m.Put(itemBox,item);
    AeSnapshot tl{};tl.focusClass=AeNativeFocusOther;tl.focusNode=timelineOs;tl.focusToken=tl.selfToken=0x22D;
    tl.focusPair=1;tl.windowCount=tl.nodeCount=1;tl.nodes[0].selfToken=0x22D;
    AeReader tlObjectReader{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};tl.nodes[0].object=AeReadObject(tlObjectReader,timelineOs);
    tl.windows[0].property=tl.nodes[0].object;tl.windows[0].hwnd=tl.windows[0].nativeHwnd=0x9988;tl.windows[0].reciprocal=1;
    auto tlCollect=[&](){AeReader rr{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};AeCollectTimeline(rr,tl,&tl.timeline);return !rr.failed&&!rr.limited;};
    auto tlClassify=[&](bool valid=true){const bool readOk=tlCollect();return AeClassifyTimelineOwner(tl,tl,valid&&readOk);};
    m.Put(undoImpl+0x68,uintptr_t(0));evidence=tlClassify();
    Check(tl.timeline.stage==5&&tl.timeline.currentItem==item&&tl.timeline.itemRef==itemRef&&tl.timeline.itemBox==itemBox,
        "timeline-current-item-comes-from-verified-focused-panel-context");
    Check(evidence.decision==AeOwnerCommand&&evidence.reason==AeOwnerExplicitNoTimelineTransaction,
        "timeline-null-current-transaction-is-command-candidate");
    Check(AeClassifyCurrentOwner(tl,tl,true).decision==AeOwnerCommand,"current-owner-dispatches-exact-timeline-type");
    const auto timelineNull=tl;
    m.Put(undoImpl+0x68,tx);evidence=tlClassify();
    Check(evidence.decision==AeOwnerUnknown,"timeline-live-text-transaction-never-grants-command-or-text-owner");
    Check(AeClassifyTimelineOwner(timelineNull,tl,true).decision==AeOwnerUnknown,"timeline-changing-transaction-is-unknown");
    m.Put(undoImpl+0x68,uintptr_t(0));tl.focusClass=AeNativeFocusEdit;evidence=tlClassify();
    Check(evidence.decision==AeOwnerUnknown&&!tl.timeline.stage,"timeline-native-Edit-first-blocks-command-even-with-stale-framework-focus");
    tl.focusClass=AeNativeFocusClassUnknown;evidence=tlClassify();
    Check(evidence.decision==AeOwnerUnknown&&!tl.timeline.stage,"timeline-unreadable-native-class-is-unknown");tl.focusClass=AeNativeFocusOther;
    tl.windows[0].reciprocal=0;evidence=tlClassify();Check(evidence.decision==AeOwnerUnknown&&!tl.timeline.stage,"timeline-different-native-owner-blocks-context-read");tl.windows[0].reciprocal=1;
    tl.nodes[0].selfToken=0x22E;evidence=tlClassify();Check(evidence.decision==AeOwnerUnknown,"timeline-stale-framework-token-is-unknown");tl.nodes[0].selfToken=0x22D;
    tl.mutationDepth=1;evidence=tlClassify();Check(evidence.decision==AeOwnerUnknown&&!tl.timeline.stage,"timeline-framework-transition-blocks-context-read");tl.mutationDepth=0;
    m.Put(timelineComplete+0x7C8,uintptr_t(0));evidence=tlClassify();
    Check(evidence.decision==AeOwnerUnknown&&!tl.timeline.item.pointer,"timeline-no-context-is-not-no-transaction");m.Put(timelineComplete+0x7C8,itemRef);
    m.bytes.erase(itemRef);evidence=tlClassify();Check(evidence.decision==AeOwnerUnknown,"timeline-failed-reference-read-is-not-command");m.Put(itemRef,itemBox);
    const AeTypeContract layerContext{1,0x19E04D0,0x1BBB160,0,0x20386E0,0x1BBB188,-1,-1,-1,-1};
    mapObject(item,layerContext);evidence=tlClassify();
    Check(evidence.decision==AeOwnerUnknown&&tl.timeline.stage==2&&!tl.timeline.beeComp.pointer,
        "timeline-unsupported-CLayer-context-stops-before-composition-read");mapObject(item,exactContract(1,0x19A2DB8));
    mapObject(timelineComplete,exactContract(1,0x19A2DB8));evidence=tlClassify();
    Check(evidence.decision==AeOwnerUnknown&&!tl.timeline.itemRef,"timeline-wrong-primary-type-blocks-context-member");mapObject(timelineComplete,exactContract(1,0x1ABD6C8));
    evidence=tlClassify(false);Check(evidence.decision==AeOwnerUnknown,"timeline-invalid-capture-does-not-publish-command");
    const HWND timelineFocus=reinterpret_cast<HWND>(tl.windows[0].hwnd);
    tlClassify();AeMakeOwnerStamp(tl,tl,true,true,101,202,timelineFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_COMMAND&&stamp.logicalObject==timelineOs&&stamp.windowObject==timelineOs&&
        stamp.lifetimeToken==0x22D&&!stamp.textObject&&!stamp.deferredSafe,
        "runtime-timeline-stamp-binds-current-node-with-no-text-target");
    AeMakeOwnerStamp(tl,tl,true,false,101,202,timelineFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_UNKNOWN&&!stamp.logicalObject,"runtime-timeline-outside-plain-letter-domain-is-unknown");
    // Actual v5 rename sample: a native Edit HWND and an OS_EditTextItem_WIN
    // focus node replace the timeline node. The timeline remains an ancestor.
    const uintptr_t renameNode=0x75A00000,renameHwnd=0xB232C6E;
    const auto& renameType=exactContract(0,0x8C9C80);
    Check(renameType.type==0xD40FF0&&renameType.offset==0&&renameType.platformOffset==0x4D8,
        "actual-rename-static-contract-matches-sampled-node");
    mapObject(renameNode,renameType);
    m.Put(renameNode+0x4F8,renameHwnd);m.Put(renameNode+0x10,uintptr_t(0x49123));
    m.Put(renameNode+0x1F0,uintptr_t(0x22D));m.Put(renameNode+0x1F8,timelineOs);
    m.Put(timelineOs+0x10,uintptr_t(0x22D));m.Put(timelineOs+0x1F0,uintptr_t(0));m.Put(timelineOs+0x1F8,uintptr_t(0));
    m.Put(mgr+0x80,uintptr_t(0x49123));m.Put(mgr+0x88,renameNode);
    AeInputs renameInput{};renameInput.count=2;renameInput.focusClass=AeNativeFocusEdit;
    renameInput.windows[0]={renameHwnd,renameNode};renameInput.windows[1]={0x9988,timelineOs};
    AeSnapshot renameSnapshot{};
    auto renameCollect=[&](){AeReader rr{Memory::Read,&m,mods,GetTickCount64()+1000,nullptr};AeCollect(rr,renameInput,&renameSnapshot);return !rr.failed&&!rr.limited;};
    Check(renameCollect()&&renameSnapshot.focusPair&&renameSnapshot.focusToken==0x49123&&
        renameSnapshot.nodes[0].object.typeRva==0xD40FF0&&renameSnapshot.windows[0].reciprocal&&
        renameSnapshot.nodeCount==2&&renameSnapshot.nodes[1].object.typeRva==0x2036798,
        "actual-rename-node-and-native-HWND-own-focus-with-timeline-parent");
    Check(!renameSnapshot.timeline.stage&&AeClassifyCurrentOwner(renameSnapshot,renameSnapshot,true).decision==AeOwnerUnknown,
        "actual-rename-native-Edit-never-inherits-timeline-command");
    AeMakeOwnerStamp(renameSnapshot,renameSnapshot,true,true,101,202,reinterpret_cast<HWND>(renameHwnd),&stamp);
    Check(stamp.kind==RIUM_OWNER_UNKNOWN&&stamp.provider==3&&!stamp.windowObject&&!stamp.logicalObject&&
        !stamp.textObject&&!stamp.lifetimeToken&&!stamp.deferredSafe,
        "actual-rename-unknown-stamp-clears-all-command-identities");
    renameInput.focusClass=AeNativeFocusOther;
    Check(renameCollect()&&!renameSnapshot.timeline.stage&&AeClassifyCurrentOwner(renameSnapshot,renameSnapshot,true).decision==AeOwnerUnknown,
        "actual-rename-node-type-blocks-command-even-with-non-Edit-class");
    renameSnapshot.timeline=timelineNull.timeline;
    Check(AeClassifyCurrentOwner(renameSnapshot,renameSnapshot,true).decision==AeOwnerUnknown,
        "actual-rename-node-cannot-reuse-stale-timeline-context");
    AeMakeOwnerStamp(timelineNull,renameSnapshot,true,true,101,202,timelineFocus,&stamp);
    Check(stamp.kind==RIUM_OWNER_UNKNOWN&&!stamp.logicalObject,
        "runtime-timeline-to-actual-rename-transition-is-unknown");
    std::printf("ae_core checks=%d failures=%d windows=0 externalTarget=NOT_EXERCISED\n",checks,failures);return failures?1:0;
}
