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
int main(){
    auto s=std::make_unique<OwnerSnapshot>();OwnerNativeEditFacts edit{};auto e=TimelineWitness(*s);
    Check(OwnerRuntimeClassify(e,edit)==RIUM_OWNER_COMMAND,"actual-timeline-witness-command");
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
