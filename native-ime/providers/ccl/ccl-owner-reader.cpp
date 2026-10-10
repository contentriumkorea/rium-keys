#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "ccl-owner-core.h"
#include <cstring>
#include <cwchar>

namespace {
struct CodeSpan { uint32_t image, rva, size; const unsigned char* bytes; };
#include "ccl-owner-fingerprints.inc"
struct LastErrorGuard { DWORD previous=GetLastError(); ~LastErrorGuard(){SetLastError(previous);} };
bool ReadOwn(void*, uint64_t address, void* output, size_t size) {
    if(!address || address>=0x0000800000000000ULL || !size || size>4096) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),output,size,&read) && read==size;
}
bool ModuleHeaders(uint64_t base, uint32_t timestamp, uint32_t image_size) {
    MEMORY_BASIC_INFORMATION memory{};
    if(VirtualQuery(reinterpret_cast<const void*>(static_cast<uintptr_t>(base)),&memory,sizeof(memory))!=sizeof(memory) ||
       memory.Type!=MEM_IMAGE || memory.AllocationBase!=reinterpret_cast<void*>(static_cast<uintptr_t>(base))) return false;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    return ReadOwn(nullptr,base,&dos,sizeof(dos)) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>=64 && dos.e_lfanew<=4096 &&
        ReadOwn(nullptr,base+static_cast<uint32_t>(dos.e_lfanew),&nt,sizeof(nt)) &&
        nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.TimeDateStamp==timestamp &&
        nt.OptionalHeader.SizeOfImage==image_size;
}
bool CodeMatches(void*, const CclOwnerContext* c) {
    if(c->process_id!=GetCurrentProcessId() ||
       reinterpret_cast<uintptr_t>(GetModuleHandleW(L"cclgui.dll"))!=c->ccl_base ||
       reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=c->exe_base ||
       !ModuleHeaders(c->ccl_base,1728053861,0x67C000) || !ModuleHeaders(c->exe_base,1728054128,62439424)) return false;
    unsigned char buffer[4096];
    for(const auto& span:kCodeSpans) {
        const uint64_t base=span.image ? c->exe_base : c->ccl_base;
        if(span.size>sizeof(buffer) || !ReadOwn(nullptr,base+span.rva,buffer,span.size) || std::memcmp(buffer,span.bytes,span.size)) return false;
    }
    return true;
}
bool DiskDigest(HMODULE module, const unsigned char expected[32]) {
    wchar_t path[32768];
    const DWORD count=GetModuleFileNameW(module,path,static_cast<DWORD>(sizeof(path)/sizeof(path[0])));
    if(!count || count>=sizeof(path)/sizeof(path[0])) return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    if(!GetFileSizeEx(file,&length) || length.QuadPart<=0 || length.QuadPart>64*1024*1024) {CloseHandle(file);return false;}
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char object[1024], digest[32], buffer[65536];
    DWORD object_size=0, got=0;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&got,0)>=0 && object_size<=sizeof(object);
    if(ok) ok=BCryptCreateHash(algorithm,&hash,object,object_size,nullptr,0,0)>=0;
    uint64_t total=0;
    while(ok) {
        DWORD read=0;
        if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)) {ok=false;break;}
        if(!read) break;
        total+=read;
        if(total>64*1024*1024 || BCryptHashData(hash,buffer,read,0)<0) {ok=false;break;}
    }
    if(ok) ok=total==static_cast<uint64_t>(length.QuadPart) && BCryptFinishHash(hash,digest,sizeof(digest),0)>=0 && std::memcmp(digest,expected,sizeof(digest))==0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file); return ok;
}
bool WindowLong(HWND hwnd, int index, uint64_t& output) {
    SetLastError(0); const auto value=GetWindowLongPtrW(hwnd,index);
    if(!value && GetLastError()!=0) return false;
    output=static_cast<uint64_t>(value); return true;
}
bool WindowMetadata(HWND hwnd, DWORD process, DWORD thread, CclWindowMetadata& output) {
    if(!hwnd) return false;
    DWORD owner_process=0; const DWORD owner_thread=GetWindowThreadProcessId(hwnd,&owner_process);
    if(owner_process!=process || owner_thread!=thread) return false;
    wchar_t class_name[64];
    if(!GetClassNameW(hwnd,class_name,64)) return false;
    output.hwnd=reinterpret_cast<uintptr_t>(hwnd); output.process=owner_process; output.thread=owner_thread;
    output.root=reinterpret_cast<uintptr_t>(GetAncestor(hwnd,GA_ROOT)); output.parent=reinterpret_cast<uintptr_t>(GetParent(hwnd));
    if(!std::wcscmp(class_name,L"CCLWindowClass")) output.kind=CCL_CLASS_WINDOW;
    else if(!std::wcscmp(class_name,L"CCLDialogClass")) output.kind=CCL_CLASS_DIALOG;
    else if(CompareStringOrdinal(class_name,-1,L"Edit",-1,TRUE)==CSTR_EQUAL) output.kind=CCL_CLASS_EDIT;
    else return false;
    uint64_t style=0;
    if(!WindowLong(hwnd,GWLP_WNDPROC,output.procedure) || !WindowLong(hwnd,GWLP_USERDATA,output.userdata) || !WindowLong(hwnd,GWL_STYLE,style)) return false;
    output.style=static_cast<uint32_t>(style);
    if(output.kind==CCL_CLASS_DIALOG && !WindowLong(hwnd,DWLP_DLGPROC,output.dialog_procedure)) return false;
    return true;
}
bool Focus(void*, CclFocusMetadata* output) {
    std::memset(output,0,sizeof(*output));
    output->process=GetCurrentProcessId(); output->thread=GetCurrentThreadId();
    HWND foreground=GetForegroundWindow();
    DWORD foreground_process=0;
    output->foreground_thread=GetWindowThreadProcessId(foreground,&foreground_process);
    output->foreground_process=foreground_process;
    if(output->foreground_process!=output->process || output->foreground_thread!=output->thread) return false;
    GUITHREADINFO info{}; info.cbSize=sizeof(info);
    if(!GetGUIThreadInfo(output->thread,&info)) return false;
    output->foreground=reinterpret_cast<uintptr_t>(foreground); output->active=reinterpret_cast<uintptr_t>(info.hwndActive);
    output->capture=reinterpret_cast<uintptr_t>(info.hwndCapture); output->gui_flags=info.flags;
    if(!WindowMetadata(info.hwndFocus,output->process,output->thread,output->focus)) return false;
    if(output->focus.kind==CCL_CLASS_EDIT)
        return WindowMetadata(reinterpret_cast<HWND>(static_cast<uintptr_t>(output->focus.parent)),output->process,output->thread,output->owner);
    output->owner=output->focus; return true;
}
}

extern "C" uint32_t CclOwnerInitialize(CclOwnerContext* context) {
    LastErrorGuard last_error;
    if(!context) return CCL_REASON_NOT_INITIALIZED;
    std::memset(context,0,sizeof(*context)); context->size=sizeof(*context); context->version=CCL_OWNER_VERSION;
    if(sizeof(void*)!=8) return CCL_REASON_BUILD_MISMATCH;
    HMODULE ccl=GetModuleHandleW(L"cclgui.dll"), exe=GetModuleHandleW(nullptr);
    if(!ccl || !exe || !DiskDigest(ccl,kCclSha256) || !DiskDigest(exe,kExeSha256)) return CCL_REASON_BUILD_MISMATCH;
    context->process_id=GetCurrentProcessId(); context->ccl_base=reinterpret_cast<uintptr_t>(ccl); context->exe_base=reinterpret_cast<uintptr_t>(exe);
    if(!CodeMatches(nullptr,context)) return CCL_REASON_BUILD_MISMATCH;
    context->initialized=1; return CCL_REASON_OK;
}
extern "C" uint32_t CclOwnerProbe(const CclOwnerContext* context, CclOwnerResult* result) {
    LastErrorGuard last_error;
    CclOwnerOps ops{nullptr,ReadOwn,Focus,CodeMatches};
    return CclOwnerProbeSource(context,ops,result);
}
extern "C" void CclOwnerCapture(void* context, RiumOwnerStamp* out) {
    if(!out) return;
    CclOwnerResult result{};
    CclOwnerProbe(static_cast<const CclOwnerContext*>(context),&result);
    CclOwnerMakeStamp(&result,out);
}
extern "C" void CclOwnerShutdown(CclOwnerContext* context) {
    if(context) std::memset(context,0,sizeof(*context));
}
