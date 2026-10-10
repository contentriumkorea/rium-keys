#include "owner-runtime-policy.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <memory>

// Test the actual Capture coordinator with deterministic collector boundaries.
// Only our message-only STATIC HWND is used. GetFocus is substituted: this
// fixture never activates a window, changes focus or contacts another process.
static HWND MockFocus();
static BOOL WINAPI MockReadProcessMemory(HANDLE,LPCVOID,LPVOID,SIZE_T,SIZE_T*);
static BOOL WINAPI MockCounter(LARGE_INTEGER*);
static SHORT WINAPI MockKeyState(int);
static UINT WINAPI MockMap(UINT,UINT,HKL);
static bool MockEdit(HWND,OwnerNativeEditFacts*);
[[maybe_unused]] static void MockNative(OwnerReader&,const OwnerInputs&,OwnerSnapshot*);
[[maybe_unused]] static void MockCaption(OwnerReader&,const OwnerInputs&,OwnerSnapshot*);
static void MockFull(OwnerReader&,const OwnerInputs&,OwnerSnapshot*);
#define GetFocus MockFocus
#define ReadProcessMemory MockReadProcessMemory
#define QueryPerformanceCounter MockCounter
#define GetKeyState MockKeyState
#define MapVirtualKeyExW MockMap
#define OwnerReadNativeEdit MockEdit
#define OwnerCollectNativeEditText MockNative
#define OwnerCollectCaptionText MockCaption
#define OwnerCollect MockFull
#include "owner-runtime.cpp"
#undef GetFocus
#undef ReadProcessMemory
#undef QueryPerformanceCounter
#undef GetKeyState
#undef MapVirtualKeyExW
#undef OwnerReadNativeEdit
#undef OwnerCollectNativeEditText
#undef OwnerCollectCaptionText
#undef OwnerCollect
#include "owner-command-timeline-witness.inc"
#include "owner-command-red-entry-witness.inc"

enum class Case { Native, Caption, Timeline, PartialOnly };
enum class Fault { None, Read, Limit, Timeout, Unstable, Focus, Inputs, EditRead, EditChange, Header, MappingTimeout };
static HWND ownedWindow=nullptr;
static constexpr uintptr_t fakeBase=0x1400000000ULL;
static Case scenario=Case::Timeline;
static Fault fault=Fault::None;
static unsigned faultStage=0,faultCall=1,calls[3]{},editCalls=0,mapCalls=0,headerReads=0;
static uint32_t entryReads[6]{};
static LONGLONG entryDeadlines[6]{},captureDeadline=0;
static unsigned totalCalls=0;
static bool lostFocus=false,forceTimeout=false,budgetMonotonic=true;
static uintptr_t property=0;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok)++failures;std::printf("%s capture-coordinator %s\n",ok?"PASS":"FAIL",name);}
static HWND MockFocus(){return lostFocus?nullptr:ownedWindow;}
static BOOL WINAPI MockCounter(LARGE_INTEGER* out){
    const BOOL ok=QueryPerformanceCounter(out);if(ok&&forceTimeout&&captureDeadline)out->QuadPart=captureDeadline+1;return ok;
}
static SHORT WINAPI MockKeyState(int){return 0;}
static UINT WINAPI MockMap(UINT key,UINT,HKL){++mapCalls;if(fault==Fault::MappingTimeout)forceTimeout=true;return key;}
template<class T> static bool Value(uintptr_t p,uintptr_t at,void* out,size_t n,const T& value){
    if(p!=at||n!=sizeof(value))return false;std::memcpy(out,&value,n);return true;
}
static BOOL WINAPI MockReadProcessMemory(HANDLE process,LPCVOID address,LPVOID out,SIZE_T n,SIZE_T* got){
    if(got)*got=0;if(process!=GetCurrentProcess())return FALSE;
    const auto p=reinterpret_cast<uintptr_t>(address);bool ok=false;
    if(p==fakeBase){++headerReads;if(fault==Fault::Header)return FALSE;}
    ok=Value(p,fakeBase,out,n,uint16_t{0x5A4D})||Value(p,fakeBase+0x3C,out,n,uint32_t{0x100})||
        Value(p,fakeBase+0x100,out,n,uint32_t{0x4550})||Value(p,fakeBase+0x118,out,n,uint16_t{0x20B})||
        Value(p,fakeBase+0x150,out,n,uint32_t{kOwnerImageSize});
    for(const auto& span:kOwnerSpans)if(p==fakeBase+span.rva&&n==span.count){std::memcpy(out,span.bytes,n);ok=true;break;}
    for(const auto& slot:kOwnerSlots)if(Value(p,fakeBase+slot.vtable+slot.offset,out,n,uintptr_t{fakeBase+slot.target})){ok=true;break;}
    if(ok&&got)*got=n;return ok?TRUE:FALSE;
}
static OwnerObject Object(uintptr_t p,uint32_t vt){
    for(const auto& c:kOwnerTypes)if(c.vtable==vt)return {p,vt,c.offset,c.type,ORead,c.node};return {};
}
static bool MockEdit(HWND w,OwnerNativeEditFacts* out){
    ++editCalls;*out={};if(w!=ownedWindow)return false;
    if(scenario==Case::Native){out->standardClass=1;out->writable=1;out->instanceProc=0x123400;out->classProc=0x124400;out->classModule=0x120000;}
    if(fault==Fault::EditRead&&totalCalls>=faultStage*2+faultCall)return false;
    if(fault==Fault::EditChange&&totalCalls>=faultStage*2+faultCall)out->classProc^=0x80;
    return true;
}
static void Collect(unsigned stage,OwnerReader& reader,const OwnerInputs& in,OwnerSnapshot* out){
    const unsigned call=++calls[stage];const unsigned index=totalCalls++;
    if(index<6){entryReads[index]=reader.reads;entryDeadlines[index]=reader.qpcDeadline;}
    captureDeadline=reader.qpcDeadline;
    if(index&&index<6&&(entryReads[index]<=entryReads[index-1]||entryDeadlines[index]!=entryDeadlines[0]))budgetMonotonic=false;
    uint16_t marker=0;reader.Get(fakeBase,&marker);
    if(scenario==Case::Caption)RedEntryWitness(*out);else TimelineWitness(*out);
    out->mainThreadId=GetCurrentThreadId();out->windows[0].hwnd=reinterpret_cast<uintptr_t>(ownedWindow);
    out->kind=stage==0?OwnerSnapshotKind::NativeEditText:stage==1?OwnerSnapshotKind::CaptionText:OwnerSnapshotKind::Full;
    if(scenario==Case::Native){out->windows[0].property=Object(property,0x265085F0);out->windows[0].propertyToken=101;out->windows[0].propertyRefcount=2;}
    // Leave a different stable stamp in each negative partial pair. The full
    // pair must be newly collected; it cannot complete a previous partial.
    if(stage<2&&scenario!=Case::Caption&&scenario!=Case::Native)out->focusToken+=1000+stage;
    if(scenario==Case::PartialOnly&&stage==2)out->focusPairMatched=0;
    if(!in.count||in.windows[0].hwnd!=reinterpret_cast<uintptr_t>(ownedWindow)||in.windows[0].property!=property)reader.failed=true;
    if(stage!=faultStage||call!=faultCall)return;
    switch(fault){
    case Fault::Read: reader.failed=true;break;
    case Fault::Limit: reader.limited=true;break;
    case Fault::Timeout: forceTimeout=true;break;
    case Fault::Unstable: ++out->focusToken;break;
    case Fault::Focus: lostFocus=true;break;
    case Fault::Inputs: SetPropW(ownedWindow,L"dvaui::ui::OS_Window::property",reinterpret_cast<HANDLE>(property+8));break;
    default: break;
    }
}
static void MockNative(OwnerReader& r,const OwnerInputs& i,OwnerSnapshot* s){Collect(0,r,i,s);}
static void MockCaption(OwnerReader& r,const OwnerInputs& i,OwnerSnapshot* s){Collect(1,r,i,s);}
static void MockFull(OwnerReader& r,const OwnerInputs& i,OwnerSnapshot* s){Collect(2,r,i,s);}
static RiumOwnerStamp Run(Case c,Fault f=Fault::None,unsigned stage=0,unsigned call=1){
    scenario=c;fault=f;faultStage=stage;faultCall=call;std::memset(calls,0,sizeof(calls));editCalls=mapCalls=headerReads=totalCalls=0;
    std::memset(entryReads,0,sizeof(entryReads));std::memset(entryDeadlines,0,sizeof(entryDeadlines));captureDeadline=0;lostFocus=forceTimeout=false;budgetMonotonic=true;
    auto witness=std::make_unique<OwnerSnapshot>();if(c==Case::Caption)RedEntryWitness(*witness);else TimelineWitness(*witness);
    property=c==Case::Native?0x20000:witness->windows[0].property.pointer;
    SetPropW(ownedWindow,L"dvaui::ui::OS_Window::property",reinterpret_cast<HANDLE>(property));
    RiumOwnerStamp stamp{};RiumDvaOwner_Capture(nullptr,&stamp);return stamp;
}
static bool Unknown(const RiumOwnerStamp& s){return s.provider==1&&s.profile==1&&s.kind==RIUM_OWNER_UNKNOWN&&!s.textObject&&!s.logicalObject&&!s.lifetimeToken&&!s.deferredSafe;}
int main(){
    ownedWindow=CreateWindowExW(0,L"STATIC",L"",0,0,0,1,1,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!ownedWindow){std::puts("FAIL own message-only fixture window");return 1;}
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);qpcFrequency=frequency.QuadPart;imageBase=fakeBase;
    firstSnapshot=new OwnerSnapshot;secondSnapshot=new OwnerSnapshot;InterlockedExchange(&initialized,1);
    auto s=Run(Case::Native);
    Check(s.kind==RIUM_OWNER_TEXT&&s.textObject==property&&s.logicalObject==property&&s.lifetimeToken==101&&!s.deferredSafe&&calls[0]==2&&!calls[1]&&!calls[2]&&!mapCalls,"native-positive-stops-before-caption-full-and-keyboard-mapping");
    Check(editCalls==2&&headerReads==3,"native-pair-has-two-native-fact-reads-and-one-header-verification");
    s=Run(Case::Caption);
    Check(s.kind==RIUM_OWNER_TEXT&&s.textObject&&s.logicalObject&&s.lifetimeToken&&calls[0]==2&&calls[1]==2&&!calls[2]&&!mapCalls,"caption-positive-uses-fresh-pair-after-negative-native");
    Check(budgetMonotonic&&editCalls==4&&entryDeadlines[0]>0,"native-and-caption-share-read-budget-and-deadline");
    s=Run(Case::Timeline);
    auto expected=std::make_unique<OwnerSnapshot>();TimelineWitness(*expected);
    Check(s.kind==RIUM_OWNER_COMMAND&&s.lifetimeToken==expected->focusToken&&calls[0]==2&&calls[1]==2&&calls[2]==2&&mapCalls==26,"full-command-requires-new-full-pair-with-original-token");
    Check(budgetMonotonic&&editCalls==6&&headerReads==7,"all-three-pairs-share-single-header-verification-and-budgets");
    s=Run(Case::PartialOnly);
    Check(s.kind==RIUM_OWNER_UNKNOWN&&calls[0]==2&&calls[1]==2&&calls[2]==2,"complete-looking-partial-records-never-publish-command");
    for(unsigned stage=0;stage<3;++stage){
        for(unsigned call=1;call<=2;++call){
            s=Run(Case::Timeline,Fault::Read,stage,call);char name[96]{};std::snprintf(name,sizeof(name),"stage-%u-read-%u-failure-stops-without-fallback",stage,call);
            Check(Unknown(s)&&calls[stage]==call&&(stage==2||!calls[stage+1])&&!mapCalls,name);
        }
    }
    s=Run(Case::Timeline,Fault::Limit,1);Check(Unknown(s)&&calls[0]==2&&calls[1]==1&&!calls[2]&&!mapCalls,"shared-read-limit-does-not-reset-for-full-fallback");
    for(unsigned stage=0;stage<3;++stage){
        s=Run(Case::Timeline,Fault::Unstable,stage,2);char name[96]{};std::snprintf(name,sizeof(name),"stage-%u-unstable-pair-exits-unknown",stage);
        Check(Unknown(s)&&calls[stage]==2&&(stage==2||!calls[stage+1])&&!mapCalls,name);
    }
    s=Run(Case::Native,Fault::Timeout);Check(Unknown(s)&&!calls[1]&&!calls[2]&&!mapCalls,"positive-text-after-deadline-is-unknown");
    s=Run(Case::Timeline,Fault::Timeout,1);Check(Unknown(s)&&!calls[2]&&!mapCalls,"caption-timeout-does-not-start-full-stage");
    s=Run(Case::Timeline,Fault::Focus,1);Check(Unknown(s)&&calls[1]==1&&!calls[2]&&!mapCalls,"focus-change-aborts-pair-and-fallback");
    s=Run(Case::Timeline,Fault::Inputs,0,2);Check(Unknown(s)&&calls[0]==2&&!calls[1]&&!calls[2]&&!mapCalls,"native-property-change-aborts-fallback");
    s=Run(Case::Caption,Fault::EditRead,1,2);Check(Unknown(s)&&calls[1]==2&&!calls[2]&&!mapCalls,"native-facts-read-failure-aborts-caption-positive");
    s=Run(Case::Native,Fault::EditChange,0,2);Check(Unknown(s)&&calls[0]==2&&!calls[1]&&!mapCalls,"native-procedure-change-aborts-positive-text");
    s=Run(Case::Timeline,Fault::Header);Check(Unknown(s)&&!totalCalls&&!mapCalls,"loaded-image-failure-never-starts-collector");
    s=Run(Case::Timeline,Fault::MappingTimeout);Check(Unknown(s)&&mapCalls==26,"keyboard-domain-work-shares-final-capture-deadline");
    // An unsuccessful capture must not seed a later one. The second call reads
    // both native samples afresh, without an ownership cache or reused proof.
    s=Run(Case::Native);Check(s.kind==RIUM_OWNER_TEXT&&calls[0]==2&&!calls[1]&&!mapCalls,"subsequent-capture-starts-with-fresh-native-pair");
    RiumDvaOwner_Shutdown();Check(!initialized&&!firstSnapshot&&!secondSnapshot&&!imageBase&&!qpcFrequency,"actual-shutdown-releases-capture-buffers");
    RemovePropW(ownedWindow,L"dvaui::ui::OS_Window::property");Check(DestroyWindow(ownedWindow)!=FALSE,"own-message-only-window-cleanup");
    std::printf("capture_coordinator_checks=%u failures=%u real_external_provider=NOT_EXERCISED collectors=MOCKED focus_actions=NONE\n",checks,failures);return failures?1:0;
}
