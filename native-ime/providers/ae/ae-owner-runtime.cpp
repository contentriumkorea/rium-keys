#include "ae-owner-runtime.h"
#include "ae-owner-runtime-policy.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <climits>

static_assert(sizeof(void*)==8,"AE owner provider is an exact x64 ABI");
struct AeRuntimeModule { HANDLE file; WCHAR path[MAX_PATH]; };
struct AeRuntime {
    AeModules modules;AeRuntimeModule records[kAeModules];
    LONGLONG frequency;AeSnapshot first,second;
};
static SRWLOCK runtimeLock=SRWLOCK_INIT;
static volatile LONG initialized=0;
static AeRuntime* runtime=nullptr;
static bool LocalRead(void*,uintptr_t p,void* out,size_t n){
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,n,&got)&&got==n;
}
static HMODULE ModuleHandle(unsigned i){return GetModuleHandleW(std::wcscmp(kAeModule[i].name,L"AfterFX.exe")==0?nullptr:kAeModule[i].name);}
static void DestroyRuntime(AeRuntime* value){
    if(!value)return;for(auto& record:value->records)if(record.file)CloseHandle(record.file);delete value;
}
static HANDLE VerifyFile(unsigned index,const WCHAR* path){
    const HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(f==INVALID_HANDLE_VALUE)return nullptr;
    LARGE_INTEGER length{};bool ok=GetFileSizeEx(f,&length)&&length.QuadPart>0&&static_cast<uint64_t>(length.QuadPart)==kAeModule[index].fileSize;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(ok)ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    unsigned char buffer[65536],digest[32]{};uint64_t total=0;
    while(ok){
        DWORD got=0;if(!ReadFile(f,buffer,sizeof(buffer),&got,nullptr)){ok=false;break;}if(!got)break;
        total+=got;if(total>kAeModule[index].fileSize||BCryptHashData(hash,buffer,got,0)<0)ok=false;
    }
    if(ok)ok=total==kAeModule[index].fileSize&&BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash&&BCryptDestroyHash(hash)<0)ok=false;
    if(algorithm&&BCryptCloseAlgorithmProvider(algorithm,0)<0)ok=false;
    char actual[65]{};for(unsigned i=0;i<32;++i)std::snprintf(actual+2*i,3,"%02x",digest[i]);
    if(!ok||std::strcmp(actual,kAeModule[index].sha)){CloseHandle(f);return nullptr;}return f;
}
static bool ModulesMatch(const AeRuntime& value,AeReader& r){
    for(unsigned i=0;i<kAeModules;++i){
        const HMODULE module=ModuleHandle(i);if(!module||reinterpret_cast<uintptr_t>(module)!=value.modules.base[i])return false;
        WCHAR path[MAX_PATH]{};const DWORD n=GetModuleFileNameW(module,path,MAX_PATH);
        if(!n||n>=MAX_PATH||CompareStringOrdinal(path,-1,value.records[i].path,-1,TRUE)!=CSTR_EQUAL)return false;
        const auto base=value.modules.base[i];uint16_t mz=0,magic=0;uint32_t nt=0,sig=0,size=0,stamp=0;
        if(!r.Get(base,&mz)||mz!=0x5A4D||!r.Get(base+0x3C,&nt)||nt>0x1000||!r.Get(base+nt,&sig)||sig!=0x4550||
           !r.Get(base+nt+8,&stamp)||stamp!=kAeModule[i].timeDateStamp||!r.Get(base+nt+24,&magic)||magic!=0x20B||
           !r.Get(base+nt+24+56,&size)||size!=kAeModule[i].imageSize)return false;
    }return true;
}
extern "C" BOOL RiumAeOwner_Initialize(void){
    AcquireSRWLockExclusive(&runtimeLock);
    if(initialized){ReleaseSRWLockExclusive(&runtimeLock);return TRUE;}
    auto* value=new(std::nothrow)AeRuntime{};bool ok=value!=nullptr;
    for(unsigned i=0;ok&&i<kAeModules;++i){
        const HMODULE module=ModuleHandle(i);if(!module){ok=false;break;}
        value->modules.base[i]=reinterpret_cast<uintptr_t>(module);
        const DWORD n=GetModuleFileNameW(module,value->records[i].path,MAX_PATH);ok=n&&n<MAX_PATH;
    }
    for(unsigned i=0;ok&&i<kAeModules;++i){value->records[i].file=VerifyFile(i,value->records[i].path);ok=value->records[i].file!=nullptr;}
    LARGE_INTEGER frequency{};
    if(ok){
        AeReader r{LocalRead,nullptr,value->modules,GetTickCount64()+1000,nullptr};uint32_t badModule=0,badRva=0;
        ok=QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0&&frequency.QuadPart<=LLONG_MAX/15&&
            ModulesMatch(*value,r)&&AeVerifySpans(r,&badModule,&badRva)&&!r.failed&&!r.limited;
    }
    if(!ok){DestroyRuntime(value);ReleaseSRWLockExclusive(&runtimeLock);return FALSE;}
    value->frequency=frequency.QuadPart;runtime=value;InterlockedExchange(&initialized,1);
    ReleaseSRWLockExclusive(&runtimeLock);return TRUE;
}
static bool SameOwner(HWND focus,DWORD pid,DWORD tid){
    DWORD owner=0,foregroundPid=0;GUITHREADINFO info{};info.cbSize=sizeof(info);
    const HWND foreground=GetForegroundWindow();
    return focus&&GetFocus()==focus&&GetWindowThreadProcessId(focus,&owner)==tid&&owner==pid&&
        GetWindowThreadProcessId(foreground,&foregroundPid)==tid&&foregroundPid==pid&&
        GetGUIThreadInfo(tid,&info)&&info.hwndFocus==focus&&info.hwndActive==foreground;
}
static AeInputs Inputs(HWND focus,DWORD pid,DWORD tid){
    AeInputs in{};HWND w=focus;
    // This native class check only excludes Edit/unknown from the timeline
    // command path. Positive ownership still requires exact framework types.
    WCHAR name[32]{};
    if(GetClassNameW(focus,name,32))in.focusClass=_wcsicmp(name,L"Edit")?AeNativeFocusOther:AeNativeFocusEdit;
    for(unsigned i=0;w&&i<8;++i){DWORD owner=0;if(GetWindowThreadProcessId(w,&owner)!=tid||owner!=pid)break;
        in.windows[in.count++]={reinterpret_cast<uintptr_t>(w),reinterpret_cast<uintptr_t>(GetPropW(w,L"dvaui::ui::OS_Window::property"))};
        const auto parent=GetAncestor(w,GA_PARENT);if(parent==w)break;w=parent;
    }return in;
}
static bool PlainLetterDomain(){
    if((GetKeyState(VK_CONTROL)|GetKeyState(VK_MENU)|GetKeyState(VK_SHIFT)|GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)return false;
    const auto layout=GetKeyboardLayout(0);
    for(UINT key='A';key<='Z';++key){const UINT c=MapVirtualKeyExW(key,MAPVK_VK_TO_CHAR,layout);if(c<'A'||c>'Z')return false;}return true;
}
extern "C" void RiumAeOwner_Capture(void*,RiumOwnerStamp* out){
    if(!out)return;std::memset(out,0,sizeof(*out));if(!InterlockedCompareExchange(&initialized,0,0))return;
    const DWORD pid=GetCurrentProcessId(),tid=GetCurrentThreadId();const HWND focus=GetFocus();
    out->provider=3;out->profile=1;out->processId=pid;out->threadId=tid;out->focus=focus;
    if(!SameOwner(focus,pid,tid)||!TryAcquireSRWLockExclusive(&runtimeLock))return;
    if(!initialized||!runtime){ReleaseSRWLockExclusive(&runtimeLock);return;}
    auto& value=*runtime;AeReader r{LocalRead,nullptr,value.modules,GetTickCount64()+100,nullptr};
    LARGE_INTEGER start{},finish{};bool valid=QueryPerformanceCounter(&start)!=FALSE;
    if(valid)r.qpcDeadline=start.QuadPart+(value.frequency*15)/1000;
    uint32_t badModule=0,badRva=0;bool plainLetters=false;
    valid=valid&&ModulesMatch(value,r)&&AeVerifySpans(r,&badModule,&badRva)&&SameOwner(focus,pid,tid);
    if(valid){
        const auto before=Inputs(focus,pid,tid);AeCollect(r,before,&value.first);
        valid=!r.failed&&!r.limited&&SameOwner(focus,pid,tid);
        if(valid){
            const auto between=Inputs(focus,pid,tid);AeCollect(r,between,&value.second);const auto after=Inputs(focus,pid,tid);
            valid=!std::memcmp(&before,&between,sizeof(before))&&!std::memcmp(&between,&after,sizeof(after))&&
                !std::memcmp(&value.first,&value.second,sizeof(value.first))&&
                AeVerifySpans(r,&badModule,&badRva)&&ModulesMatch(value,r);
        }
        if(valid)plainLetters=PlainLetterDomain();
    }
    valid=valid&&!r.failed&&!r.limited&&SameOwner(focus,pid,tid)&&GetTickCount64()<r.deadline&&
        QueryPerformanceCounter(&finish)&&finish.QuadPart<r.qpcDeadline;
    AeMakeOwnerStamp(value.first,value.second,valid,plainLetters,pid,tid,focus,out);
    ReleaseSRWLockExclusive(&runtimeLock);
}
extern "C" void RiumAeOwner_Shutdown(void){
    AcquireSRWLockExclusive(&runtimeLock);InterlockedExchange(&initialized,0);
    DestroyRuntime(runtime);runtime=nullptr;ReleaseSRWLockExclusive(&runtimeLock);
}
