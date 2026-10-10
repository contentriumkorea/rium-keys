#include "owner-runtime-policy.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <new>

// The embedding TIP owns module lifetime and calls Shutdown outside DllMain.
// No hook, observer, thread, host function, COM object or edit session is made.
static SRWLOCK runtimeLock=SRWLOCK_INIT;
static volatile LONG initialized=0;
static HANDLE imageFile=nullptr;
static uintptr_t imageBase=0;
static LONGLONG qpcFrequency=0;
static OwnerSnapshot* firstSnapshot=nullptr;
static OwnerSnapshot* secondSnapshot=nullptr;
static bool LocalRead(void*,uintptr_t p,void* out,size_t n){
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,n,&got)&&got==n;
}
static HANDLE VerifyCurrentFile(){
    WCHAR path[32768]{};const DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if(!n||n>=32768)return nullptr;
    const HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(f==INVALID_HANDLE_VALUE)return nullptr;
    LARGE_INTEGER length{};bool ok=GetFileSizeEx(f,&length)&&length.QuadPart==780886536;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(ok)ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    unsigned char buffer[65536],digest[32]{};DWORD got=0;
    while(ok){if(!ReadFile(f,buffer,sizeof(buffer),&got,nullptr)){ok=false;break;}if(!got)break;if(BCryptHashData(hash,buffer,got,0)<0)ok=false;}
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash&&BCryptDestroyHash(hash)<0)ok=false;
    if(algorithm&&BCryptCloseAlgorithmProvider(algorithm,0)<0)ok=false;
    char text[65]{};for(unsigned i=0;i<32;++i)std::snprintf(text+i*2,3,"%02x",digest[i]);
    ok=ok&&!std::strcmp(text,kOwnerSha256);
    if(!ok){CloseHandle(f);return nullptr;}return f;
}
static bool Header(OwnerReader& r){
    uint16_t mz=0,magic=0;uint32_t nt=0,sig=0,size=0;
    return r.Get(r.base,&mz)&&mz==0x5A4D&&r.Get(r.base+0x3C,&nt)&&nt<=0x1000&&r.Get(r.base+nt,&sig)&&sig==0x4550&&
        r.Get(r.base+nt+24,&magic)&&magic==0x20B&&r.Get(r.base+nt+24+56,&size)&&size==kOwnerImageSize;
}
extern "C" BOOL RiumDvaOwner_Initialize(void){
    AcquireSRWLockExclusive(&runtimeLock);
    if(initialized){ReleaseSRWLockExclusive(&runtimeLock);return TRUE;}
    HANDLE file=VerifyCurrentFile();const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    OwnerReader r{LocalRead,nullptr,base,kOwnerImageSize,GetTickCount64()+1000,nullptr};uint32_t bad=0;
    LARGE_INTEGER frequency{};const bool valid=file&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0&&Header(r)&&OwnerVerifySpans(r,&bad)&&!r.failed&&!r.limited;
    auto* a=valid?new(std::nothrow)OwnerSnapshot:nullptr;auto* b=a?new(std::nothrow)OwnerSnapshot:nullptr;
    if(!a||!b){delete a;delete b;if(file)CloseHandle(file);ReleaseSRWLockExclusive(&runtimeLock);return FALSE;}
    firstSnapshot=a;secondSnapshot=b;imageFile=file;imageBase=base;qpcFrequency=frequency.QuadPart;InterlockedExchange(&initialized,1);
    ReleaseSRWLockExclusive(&runtimeLock);return TRUE;
}
static bool SameOwner(HWND focus,DWORD pid,DWORD tid){
    DWORD owner=0;return focus&&GetFocus()==focus&&GetWindowThreadProcessId(focus,&owner)==tid&&owner==pid;
}
static OwnerInputs Inputs(HWND focus,DWORD pid,DWORD tid){
    OwnerInputs in{};HWND w=focus;
    for(unsigned i=0;w&&i<kOwnerWindows;++i){
        DWORD owner=0;if(GetWindowThreadProcessId(w,&owner)!=tid||owner!=pid)break;
        in.windows[in.count++]={reinterpret_cast<uintptr_t>(w),reinterpret_cast<uintptr_t>(GetPropW(w,L"dvaui::ui::OS_Window::property"))};
        const auto p=GetAncestor(w,GA_PARENT);if(p==w)break;w=p;
    }return in;
}
static bool PlainLetterDomain(){
    if((GetKeyState(VK_CONTROL)|GetKeyState(VK_MENU)|GetKeyState(VK_SHIFT)|GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)return false;
    const auto layout=GetKeyboardLayout(0);
    for(UINT key='A';key<='Z';++key){const UINT c=MapVirtualKeyExW(key,MAPVK_VK_TO_CHAR,layout);if(c<'A'||c>'Z')return false;}
    return true;
}
static bool WithinBudget(OwnerReader& r){
    LARGE_INTEGER now{};
    if(!QueryPerformanceCounter(&now)||now.QuadPart>=r.qpcDeadline)r.limited=true;
    return !r.failed&&!r.limited;
}
using Collector=void (*)(OwnerReader&,const OwnerInputs&,OwnerSnapshot*);
static bool CollectPair(OwnerReader& r,Collector collect,HWND focus,DWORD pid,DWORD tid,
                        OwnerCommandEvidence* e,OwnerNativeEditFacts* edit){
    // Each stage starts from fresh inputs and two fresh snapshots. A stable
    // negative partial proof may advance; any failed or changing read aborts.
    *e={};*edit={};e->snapshot=firstSnapshot;e->exactImageVerified=1;e->loadedSpansVerified=1;
    e->targetThreadId=tid;e->nativeFocus=reinterpret_cast<uintptr_t>(focus);
    e->ownerBefore=SameOwner(focus,pid,tid);if(!e->ownerBefore||!WithinBudget(r))return false;
    const auto before=Inputs(focus,pid,tid);
    if(!OwnerReadNativeEdit(focus,edit)||!WithinBudget(r))return false;
    collect(r,before,firstSnapshot);
    if(!WithinBudget(r)||!SameOwner(focus,pid,tid))return false;
    const auto between=Inputs(focus,pid,tid);
    if(std::memcmp(&before,&between,sizeof(before)))return false;
    collect(r,between,secondSnapshot);
    if(!WithinBudget(r)||!SameOwner(focus,pid,tid))return false;
    const auto after=Inputs(focus,pid,tid);OwnerNativeEditFacts editAfter{};
    if(!OwnerReadNativeEdit(focus,&editAfter))return false;
    e->inputsStable=!std::memcmp(&between,&after,sizeof(after))&&!std::memcmp(edit,&editAfter,sizeof(editAfter));
    e->samplesEqual=!std::memcmp(firstSnapshot,secondSnapshot,sizeof(*firstSnapshot));
    e->ownerAfter=SameOwner(focus,pid,tid);const bool budget=WithinBudget(r);e->readFailed=r.failed;e->limited=r.limited;
    return budget&&e->ownerAfter&&e->inputsStable&&e->samplesEqual;
}
extern "C" void RiumDvaOwner_Capture(void*,RiumOwnerStamp* out){
    if(!out)return;std::memset(out,0,sizeof(*out));
    if(!InterlockedCompareExchange(&initialized,0,0))return;
    out->provider=1;out->profile=1;out->processId=GetCurrentProcessId();out->threadId=GetCurrentThreadId();out->focus=GetFocus();
    if(!SameOwner(out->focus,out->processId,out->threadId))return;
    out->windowObject=reinterpret_cast<uintptr_t>(GetPropW(out->focus,L"dvaui::ui::OS_Window::property"));
    if(!TryAcquireSRWLockExclusive(&runtimeLock))return;
    if(!initialized){ReleaseSRWLockExclusive(&runtimeLock);return;}
    const auto focus=out->focus;OwnerReader r{LocalRead,nullptr,imageBase,kOwnerImageSize,GetTickCount64()+250,nullptr};uint32_t bad=0;
    LARGE_INTEGER started{};if(!QueryPerformanceCounter(&started)){ReleaseSRWLockExclusive(&runtimeLock);return;}
    r.qpcDeadline=started.QuadPart+(qpcFrequency*15)/1000;
    if(Header(r)&&OwnerVerifySpans(r,&bad)&&WithinBudget(r)){
        const Collector stages[]={OwnerCollectNativeEditText,OwnerCollectCaptionText,OwnerCollect};
        for(unsigned stage=0;stage<3;++stage){
            OwnerCommandEvidence e{};OwnerNativeEditFacts edit{};
            if(!CollectPair(r,stages[stage],focus,out->processId,out->threadId,&e,&edit))break;
            RiumOwnerKind kind=OwnerRuntimeClassify(e,edit);
            if(kind!=RIUM_OWNER_TEXT&&stage<2)continue;
            // Only a full fresh pair can classify COMMAND. Positive TEXT
            // needs no keyboard-layout mapping or command-dispatch metadata.
            if(kind!=RIUM_OWNER_TEXT){
                e.virtualKey='A';e.mappedCharacterKnown=PlainLetterDomain();e.mappedCharacter='A';e.dvaModifiersKnown=e.mappedCharacterKnown;
                kind=OwnerRuntimeClassify(e,edit);
            }
            if(!WithinBudget(r)||!SameOwner(focus,out->processId,out->threadId))break;
            const auto& s=*firstSnapshot;out->windowObject=s.windows[0].property.pointer;
            if(edit.standardClass&&s.windows[0].propertyToken&&s.windows[0].propertyRefcount){
                out->logicalObject=out->windowObject;out->lifetimeToken=s.windows[0].propertyToken;
            }else if(s.focusPairMatched&&s.focusToken){out->logicalObject=s.focusNode;out->lifetimeToken=s.focusToken;}
            out->kind=kind;
            if(kind==RIUM_OWNER_TEXT)out->textObject=edit.standardClass?out->windowObject:s.monitor.currentComponent.pointer;
            break;
        }
    }
    // No remembered classification, mode toggles, raw key dispatch, text reads,
    // output, per-key file access, or deferred-ownership promise.
    ReleaseSRWLockExclusive(&runtimeLock);
}
extern "C" void RiumDvaOwner_Shutdown(void){
    AcquireSRWLockExclusive(&runtimeLock);InterlockedExchange(&initialized,0);
    delete firstSnapshot;delete secondSnapshot;firstSnapshot=nullptr;secondSnapshot=nullptr;
    if(imageFile)CloseHandle(imageFile);imageFile=nullptr;imageBase=0;qpcFrequency=0;ReleaseSRWLockExclusive(&runtimeLock);
}
