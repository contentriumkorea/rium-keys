#include "owner-runtime-policy.h"
#include <memory>
#include <cstdio>
#include <cwchar>
#include "owner-command-timeline-witness.inc"
#include "owner-command-red-entry-witness.inc"
#include "owner-command-blue-entry-witness.inc"
#include "owner-command-blue-entry-v8-focused-witness.inc"
#include "owner-monitor-classifier.h"
#include "owner-command-audio-timeline-witness.inc"
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok)++failures;std::printf("%s runtime-proof %s\n",ok?"PASS":"FAIL",name);}
static OwnerObject Object(uintptr_t p,uint32_t vt){OwnerObject o{};for(const auto& c:kOwnerTypes)if(c.vtable==vt)o={p,vt,c.offset,c.type,ORead,c.node};return o;}
// Static exact-image RTTI enrichment of two unsupported types in the old live
// observer. Other recorded fields remain unchanged; this is not a new live read.
static OwnerCommandEvidence AudioCandidate(OwnerSnapshot& s){
    auto e=AudioTimelineWitness(s);
    s.parents[0].delegate.state=ORead;s.parents[0].delegate.colOffset=0;s.parents[0].delegate.typeRva=0x2CBFC7D0;
    s.parents[1].delegate.state=ORead;s.parents[1].delegate.colOffset=0;s.parents[1].delegate.typeRva=0x2B2B35B8;
    return e;
}
// The 2026-10-26 blank timeline focuses the existing route at its third
// audited delegate. Derive that exact eight-node suffix from the old witness;
// this is a synthetic regression, not another physical-input result.
static OwnerCommandEvidence BlankTimeline(OwnerSnapshot& s){
    auto e=TimelineWitness(s);
    for(unsigned i=0;i<8;++i)s.parents[i]=s.parents[i+2];
    for(unsigned i=8;i<kOwnerParents;++i)s.parents[i]={};
    s.parentCount=8;s.focusNode=s.parents[0].object.pointer;s.focusToken=s.parents[0].selfToken;
    return e;
}
// Exact eight-node time-ruler route observed on 2026-11-02. The first two
// delegates differ from track space; their three key slots are false leaves.
static OwnerCommandEvidence RulerTimeline(OwnerSnapshot& s){
    auto e=BlankTimeline(s);
    s.parents[0].delegate={0x710000,0x278732B8,0,0x2CF6DAF0,ORead,0};
    s.parents[1].delegate={0x720000,0x272D8B68,0,0x2B2AE520,ORead,0};
    return e;
}
int main(){
    auto s=std::make_unique<OwnerSnapshot>();OwnerNativeEditFacts edit{};auto e=TimelineWitness(*s);
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_COMMAND,"actual-timeline-witness-command");
    e=RulerTimeline(*s);
    for(unsigned key='A';key<='Z';++key){e.virtualKey=e.mappedCharacter=key;
        Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_COMMAND,"time-ruler-letter-shortcut");}
    e=RulerTimeline(*s);s->parents[1].delegate=Object(0x720000,0x272D48B8);
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"time-ruler-mixed-track-route-rejected");
    e=RulerTimeline(*s);s->parents[0].delegate.typeRva++;
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"time-ruler-wrong-rtti-rejected");
    e=RulerTimeline(*s);s->parents[1].callbackEmpty=0;
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"time-ruler-custom-key-callback-rejected");
    e=RulerTimeline(*s);s->parents[0].keyRva++;
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"time-ruler-unknown-handler-rejected");
    e=RulerTimeline(*s);s->parents[0].parentToken++;
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"time-ruler-stale-parent-rejected");
    e=RulerTimeline(*s);s->special[0]=0xBAD;
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"time-ruler-active-special-handler-rejected");
    // Exact types observed in the failing ALL PANELS workspace; all other
    // fields derive from the independently recorded timeline/monitor witness.
    for(unsigned mode=0;mode<2;++mode)for(unsigned type=0;type<2;++type){
        e=mode?BlueEntryV8FocusedWitness(*s):TimelineWitness(*s);
        auto& set=s->filters[1];auto& f=set.entries[set.count++];f={};
        f.object={0x700000,type?0x26539E80u:0x264FACF0u,0,type?0x2AFBA1F0u:0x2AFB9E18u,ORead,1};
        f.token=f.selfToken=701;f.pairMatched=f.callbackEmpty=1;f.refcount=1;
        f.virtual88=type?0x19178C10:0x1915D250;f.filterRva=type?0x192D1C50:0x191D3D10;
        f.dialogDisabledKnown=1;f.resolvedParent={0,1};
        for(unsigned i=0;i<s->parentCount;++i)s->parents[i].resolvedParent={s->parents[i].parent,1};
        for(unsigned key='A';key<='Z';++key){e.virtualKey=e.mappedCharacter=key;
            Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_COMMAND,mode?"monitor-navigation-filter-preserves-letter-shortcut":"timeline-navigation-filter-preserves-letter-shortcut");}
        if(type){
            s->parents[0].resolvedParent.parent++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"dialog-registry-parent-differs-from-valid-cache-rejected");s->parents[0].resolvedParent.parent--;
            s->parents[0].resolvedParent.known=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"dialog-unresolved-registry-parent-rejected");s->parents[0].resolvedParent.known=1;
            f.resolvedParent={0xBAD000,1};Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"dialog-own-parent-outside-audited-route-rejected");f.resolvedParent={0,1};
            f.dialogDisabledKnown=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"dialog-unknown-early-guard-rejected");f.dialogDisabledKnown=1;
        }
        f.callbackEmpty=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"navigation-filter-custom-callback-still-blocked");f.callbackEmpty=1;
        f.pairMatched=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"navigation-filter-stale-token-still-blocked");f.pairMatched=1;
        f.filterRva++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"navigation-filter-changed-handler-still-blocked");f.filterRva--;
        f.virtual88++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"navigation-filter-changed-policy-still-blocked");f.virtual88--;
        f.object.typeRva++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"navigation-filter-wrong-type-still-blocked");f.object.typeRva--;
        e.virtualKey='\t';e.mappedCharacter='\t';Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"navigation-filter-does-not-approve-tab");
        e.virtualKey=e.mappedCharacter='P';e.dvaModifiers=2;s->modifiers=s->observerModifiers=2;
        Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"navigation-filter-does-not-expand-modified-key-domain");
        e.dvaModifiers=0;s->modifiers=s->observerModifiers=0;
        s->windows[0].property=Object(0x20000,0x265085F0);s->windows[0].propertyToken=101;s->windows[0].propertyRefcount=2;
        edit.standardClass=edit.writable=1;edit.instanceProc=0x10000;edit.classProc=0x11000;
        Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_TEXT,"navigation-filter-does-not-steal-native-text-input");edit={};
    }
    e=BlankTimeline(*s);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_COMMAND,"blank-timeline-eight-node-audited-suffix-command");
    s->parentCount=9;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-other-depth-not-inferred-command");
    e=BlankTimeline(*s);s->parentCount=7;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-truncated-chain-unknown");
    e=BlankTimeline(*s);s->parents[0].delegate.vtable++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-other-owner-unknown");
    e=BlankTimeline(*s);s->parents[0].keyRva++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-unknown-dispatch-unknown");
    e=BlankTimeline(*s);s->parents[1].parentToken++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-stale-parent-token-unknown");
    e=BlankTimeline(*s);s->parents[0].callbackEmpty=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-custom-key-callback-unknown");
    e=BlankTimeline(*s);s->windows[0].property.pointer++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-different-native-window-unknown");
    e=BlankTimeline(*s);e.samplesEqual=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-changing-metadata-unknown");
    e=BlankTimeline(*s);e.loadedSpansVerified=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-unverified-image-unknown");
    e=BlankTimeline(*s);e.dvaModifiers=2;s->modifiers=2;s->observerModifiers=2;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blank-timeline-shift-outside-command-domain");
    e=BlankTimeline(*s);s->windows[0].property=Object(0x20000,0x265085F0);s->windows[0].propertyToken=101;s->windows[0].propertyRefcount=2;
    edit.standardClass=1;edit.writable=1;edit.instanceProc=0x10000;edit.classProc=0x11000;
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_TEXT,"blank-timeline-native-text-focus-remains-text");edit={};
    e=AudioTimelineWitness(*s);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"original-audio-capture-unsupported-types-remain-unknown");
    e=AudioCandidate(*s);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_COMMAND,"live-audio-route-plus-static-exact-type-proof-command");
    s->parents[0].delegate=Object(s->parents[0].delegate.pointer,0x272E8C50);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"mixed-video-content-audio-track-rejected");
    e=AudioCandidate(*s);s->parents[1].delegate=Object(s->parents[1].delegate.pointer,0x272E90D0);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"mixed-audio-content-video-track-rejected");
    e=AudioCandidate(*s);s->parents[0].delegate.typeRva++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"audio-wrong-exact-type-rejected");
    e=AudioCandidate(*s);s->parents[0].keyRva++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"audio-wrong-dispatch-slot-rejected");
    e=AudioCandidate(*s);e.loadedSpansVerified=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"audio-unverified-loaded-contract-rejected");
    e=RedEntryWitness(*s);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_TEXT,"actual-bound-red-caption-text");
    s->selection.endpoints[0].value=-1;s->selection.endpoints[1].value=-1;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"same-caption-with-no-text-endpoints-unknown");
    e=BlueEntryWitness(*s);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"actual-blue-monitor-unknown-until-keydown-proof");
    e=BlueEntryV8FocusedWitness(*s);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_COMMAND,"actual-v8-blue-monitor-current-owner-command");
    s->monitor.defaultFlag.initializedKnown=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blue-unknown-default-initialization-remains-unknown");
    e=BlueEntryV8FocusedWitness(*s);s->selection.endpoints[0].value=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blue-positive-text-endpoint-not-command");
    e=BlueEntryV8FocusedWitness(*s);e.nativeFocus++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blue-current-owner-requires-exact-native-focus");
    e=BlueEntryV8FocusedWitness(*s);e.dvaModifiers=2;s->modifiers=2;s->observerModifiers=2;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"blue-shift-letter-outside-current-command-domain");
    e=BlueEntryV8FocusedWitness(*s);s->kind=OwnerSnapshotKind::CaptionText;Check(!OwnerMonitorCommand(e)&&OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"partial-caption-cannot-be-used-as-blue-command-proof");
    e=TimelineWitness(*s);s->kind=OwnerSnapshotKind::NativeEditText;Check(OwnerClassifyCommand(e).classification==OwnerCommandClass::Unknown&&OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"partial-native-cannot-be-used-as-timeline-command-proof");
    e=RedEntryWitness(*s);s->kind=OwnerSnapshotKind::CaptionText;s->filters[1]={};s->observer={};Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_TEXT,"partial-caption-positive-does-not-require-command-filters");
    s->kind=OwnerSnapshotKind::NativeEditText;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"partial-native-cannot-claim-caption-proof");
    s->kind=static_cast<OwnerSnapshotKind>(99);Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"unknown-proof-kind-is-not-full-or-text");
    e=RedEntryWitness(*s);s->monitor.currentComponent.pointer++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"different-current-component-rejects-stale-global-selection");
    e=RedEntryWitness(*s);s->monitor.state=OUnknown;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"unbound-current-entry-unknown");
    e=RedEntryWitness(*s);e.nativeFocus++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"red-selection-does-not-own-different-native-focus");
    e=RedEntryWitness(*s);e.samplesEqual=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"red-read-changed-midcapture");
    e=RedEntryWitness(*s);s->focusMutationDepth=1;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"red-focus-transition-unknown");
    e=RedEntryWitness(*s);s->parents[1].parentToken++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"red-stale-parent-token");
    e=RedEntryWitness(*s);s->windows[0].background.pointer++;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"red-background-not-bound-to-window");
    e=RedEntryWitness(*s);e.dvaModifiers=2;s->modifiers=2;s->observerModifiers=2;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_TEXT,"shift-does-not-revoke-positive-text-ownership");
    e=TimelineWitness(*s);s->windows[0].property=Object(0x20000,0x265085F0);s->windows[0].propertyToken=101;s->windows[0].propertyRefcount=2;edit={};edit.instanceProc=0x10000;edit.classProc=0x11000;edit.classModule=0x12000;edit.standardClass=1;edit.writable=1;
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_TEXT,"native-Edit-binds-window-not-stale-logical-timeline");
    s->kind=OwnerSnapshotKind::NativeEditText;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_TEXT,"partial-native-positive-with-current-property");
    const auto nativeManager=s->manager;s->manager={};Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"native-text-missing-manager-does-not-default-transition-depth-to-zero");
    s->manager=nativeManager;s->manager.state=OUnsupported;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"native-text-unsupported-manager-does-not-default-transition-depth-to-zero");s->manager=nativeManager;
    edit.writable=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"read-only-Edit-does-not-own-input");edit.writable=1;
    edit.standardClass=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"class-name-alone-insufficient");edit.standardClass=1;
    s->windows[0].propertyRefcount=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"dead-Edit-property-unknown");s->windows[0].propertyRefcount=2;
    s->windows[0].propertyToken=0;Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_UNKNOWN,"unbound-Edit-lifetime-unknown");
    // OWN hidden controls only; no activation, focus, keyboard, hooks, or host.
    const HWND parent=CreateWindowExW(0,L"STATIC",L"",0,0,0,1,1,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    const HWND child=parent?CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),nullptr):nullptr;
    OwnerNativeEditFacts real{};
    OwnerReadNativeEdit(child,&real);WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);const BOOL found=GetClassInfoExW(nullptr,L"Edit",&cls);
    std::printf("own_Edit_metadata found=%d standard=%u writable=%u instance=%p class=%p module=%p systemClass=%p systemModule=%p\n",found,real.standardClass,real.writable,reinterpret_cast<void*>(real.instanceProc),reinterpret_cast<void*>(real.classProc),reinterpret_cast<void*>(real.classModule),reinterpret_cast<void*>(cls.lpfnWndProc),cls.hInstance);
    Check(child&&OwnerReadNativeEdit(child,&real)&&real.standardClass&&real.writable&&real.instanceProc,"own-hidden-standard-Edit-class-positive");
    if(child)SetWindowLongPtrW(child,GWL_STYLE,GetWindowLongPtrW(child,GWL_STYLE)|ES_READONLY);
    Check(child&&OwnerReadNativeEdit(child,&real)&&real.standardClass&&!real.writable,"own-hidden-readonly-Edit-negative");
    Check(parent&&OwnerReadNativeEdit(parent,&real)&&!real.standardClass,"own-hidden-Static-is-not-Edit");
    const HWND ansi=parent?CreateWindowExA(0,"EDIT","",WS_CHILD,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),nullptr):nullptr;
    Check(ansi&&!IsWindowUnicode(ansi)&&OwnerReadNativeEdit(ansi,&real)&&real.standardClass&&real.writable,"own-hidden-ANSI-Edit-class-positive");
    WNDCLASSEXA clsA{};clsA.cbSize=sizeof(clsA);GetClassInfoExA(nullptr,"Edit",&clsA);
    std::printf("own_ANSI_Edit_metadata unicode=%d instanceW=%p classW=%p instanceA=%p classA=%p infoA=%p infoW=%p module=%p\n",IsWindowUnicode(ansi),reinterpret_cast<void*>(GetWindowLongPtrW(ansi,GWLP_WNDPROC)),reinterpret_cast<void*>(GetClassLongPtrW(ansi,GCLP_WNDPROC)),reinterpret_cast<void*>(GetWindowLongPtrA(ansi,GWLP_WNDPROC)),reinterpret_cast<void*>(GetClassLongPtrA(ansi,GCLP_WNDPROC)),reinterpret_cast<void*>(clsA.lpfnWndProc),reinterpret_cast<void*>(cls.lpfnWndProc),reinterpret_cast<void*>(GetClassLongPtrA(ansi,GCLP_HMODULE)));
    Check(child&&DestroyWindow(child)&&ansi&&DestroyWindow(ansi)&&parent&&DestroyWindow(parent),"own-hidden-control-cleanup");
    WNDCLASSW shadow{};shadow.lpfnWndProc=DefWindowProcW;shadow.hInstance=GetModuleHandleW(nullptr);shadow.lpszClassName=L"Edit";
    const ATOM shadowAtom=RegisterClassW(&shadow);const HWND imitation=shadowAtom?CreateWindowExW(0,L"Edit",L"",0,0,0,1,1,HWND_MESSAGE,nullptr,shadow.hInstance,nullptr):nullptr;
    Check(imitation&&OwnerReadNativeEdit(imitation,&real)&&!real.standardClass,"own-app-class-named-Edit-is-not-standard");
    Check(imitation&&DestroyWindow(imitation)&&UnregisterClassW(L"Edit",shadow.hInstance),"own-app-shadow-class-cleanup");
    WCHAR manifest[32768]{};GetModuleFileNameW(nullptr,manifest,32768);auto* slash=std::wcsrchr(manifest,L'\\');if(slash)std::wcscpy(slash+1,L"owner-fixture-controls.manifest");
    ACTCTXW act{};act.cbSize=sizeof(act);act.lpSource=manifest;const HANDLE context=CreateActCtxW(&act);ULONG_PTR cookie=0;
    const bool active=context!=INVALID_HANDLE_VALUE&&ActivateActCtx(context,&cookie);
    const HWND themed=active?CreateWindowExW(0,L"Edit",L"",0,0,0,1,1,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr):nullptr;
    const BOOL deactivated=active?DeactivateActCtx(0,cookie):FALSE;
    const bool themedRead=themed&&OwnerReadNativeEdit(themed,&real);
    const ATOM atom=static_cast<ATOM>(GetClassLongPtrW(themed,GCW_ATOM));WNDCLASSEXW byAtom{};byAtom.cbSize=sizeof(byAtom);const BOOL atomRead=GetClassInfoExW(reinterpret_cast<HINSTANCE>(real.classModule),MAKEINTRESOURCEW(atom),&byAtom);
    WCHAR modulePath[32768]{};GetModuleFileNameW(reinterpret_cast<HMODULE>(real.classModule),modulePath,32768);
    std::printf("own_SxS_atom=%u query=%d atomProc=%p atomModule=%p\n",atom,atomRead,reinterpret_cast<void*>(byAtom.lpfnWndProc),byAtom.hInstance);std::wprintf(L"own_SxS_module_path=%ls\n",modulePath);
    const wchar_t* qualified[]={L"6.0.0.0!Edit",L"6.0.26100.8521!Edit"};
    for(unsigned qi=0;qi<2;++qi){WNDCLASSEXW qc{};qc.cbSize=sizeof(qc);const BOOL qr=GetClassInfoExW(reinterpret_cast<HINSTANCE>(real.classModule),qualified[qi],&qc);std::printf("own_SxS_qualified index=%u found=%d proc=%p\n",qi,qr,reinterpret_cast<void*>(qc.lpfnWndProc));}
    std::printf("own_SxS_Edit_metadata created=%d read=%d standard=%u moduleKind=%u class=%p module=%p defaultW=%p moduleW=%p unicode=%u\n",themed!=nullptr,themedRead,real.standardClass,real.moduleKind,reinterpret_cast<void*>(real.classProc),reinterpret_cast<void*>(real.classModule),reinterpret_cast<void*>(real.infoW),reinterpret_cast<void*>(real.moduleInfoW),real.unicode);
    Check(themedRead&&real.standardClass&&real.writable,"own-hidden-SxS-Edit-retains-standard-identity-after-context-restored");
    Check(themed&&DestroyWindow(themed)&&deactivated,"own-hidden-SxS-control-cleanup");if(context!=INVALID_HANDLE_VALUE)ReleaseActCtx(context);
    std::printf("runtime_proof_checks=%u failures=%u external_host=NOT_EXERCISED\n",checks,failures);return failures?1:0;
}
