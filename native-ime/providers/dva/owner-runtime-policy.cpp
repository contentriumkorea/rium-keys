#include "owner-runtime-policy.h"
#include "owner-monitor-classifier.h"
#include <cwchar>

static uintptr_t SxsEditProcedure(HINSTANCE module){
    // Read the loaded module's name, never its file. The Windows Common-
    // Controls assembly registers a version-qualified class. Query that exact
    // class without activating/deactivating any context in the host thread.
    WCHAR path[1024]{},windows[512]{},prefix[768]{},qualified[80]{};
    const DWORD n=GetModuleFileNameW(module,path,1024),wn=GetWindowsDirectoryW(windows,512);
    if(!n||n>=1024||!wn||wn>=512)return 0;
    const int count=_snwprintf(prefix,768,L"%ls\\WinSxS\\amd64_microsoft.windows.common-controls_6595b64144ccf1df_",windows);
    if(count<0||count>=768||_wcsnicmp(path,prefix,count))return 0;
    const auto* version=path+count;const auto* end=std::wcschr(version,L'_');if(!end||end-version<7||end-version>63)return 0;
    unsigned dots=0,digits=0;
    for(auto* p=version;p<end;++p){if(*p>=L'0'&&*p<=L'9'){if(++digits>10)return 0;}else if(*p==L'.'&&digits){++dots;digits=0;}else return 0;}
    if(dots!=3||!digits)return 0;
    const auto* slash=std::wcsrchr(end,L'\\');if(!slash||_wcsicmp(slash,L"\\comctl32.dll")||std::wcschr(end,L'/')||std::wcschr(end,L'\\')!=slash)return 0;
    std::wmemcpy(qualified,version,end-version);std::wcscpy(qualified+(end-version),L"!Edit");
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);
    return GetClassInfoExW(module,qualified,&cls)?reinterpret_cast<uintptr_t>(cls.lpfnWndProc):0;
}
bool OwnerReadNativeEdit(HWND window,OwnerNativeEditFacts* out){
    if(!out)return false;*out={};DWORD pid=0;
    if(!window||GetWindowThreadProcessId(window,&pid)!=GetCurrentThreadId()||pid!=GetCurrentProcessId())return false;
    WCHAR name[32]{};if(!GetClassNameW(window,name,32))return false;
    out->instanceProc=static_cast<uintptr_t>(GetWindowLongPtrW(window,GWLP_WNDPROC));
    out->classProc=static_cast<uintptr_t>(GetClassLongPtrW(window,GCLP_WNDPROC));
    out->classModule=static_cast<uintptr_t>(GetClassLongPtrW(window,GCLP_HMODULE));
    if(_wcsicmp(name,L"Edit"))return true;
    out->unicode=IsWindowUnicode(window)!=FALSE;
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);
    if(!GetClassInfoExW(nullptr,L"Edit",&cls))return false;
    out->infoW=reinterpret_cast<uintptr_t>(cls.lpfnWndProc);
    WNDCLASSEXA clsA{};clsA.cbSize=sizeof(clsA);if(GetClassInfoExA(nullptr,"Edit",&clsA))out->infoA=reinterpret_cast<uintptr_t>(clsA.lpfnWndProc);
    WNDCLASSEXW moduleW{};moduleW.cbSize=sizeof(moduleW);if(GetClassInfoExW(reinterpret_cast<HINSTANCE>(out->classModule),L"Edit",&moduleW))out->moduleInfoW=reinterpret_cast<uintptr_t>(moduleW.lpfnWndProc);
    WNDCLASSEXA moduleA{};moduleA.cbSize=sizeof(moduleA);if(GetClassInfoExA(reinterpret_cast<HINSTANCE>(out->classModule),"Edit",&moduleA))out->moduleInfoA=reinterpret_cast<uintptr_t>(moduleA.lpfnWndProc);
    if(out->classModule==reinterpret_cast<uintptr_t>(GetModuleHandleW(L"user32.dll")))out->moduleKind=1;
    else if(out->classModule==reinterpret_cast<uintptr_t>(GetModuleHandleW(L"comctl32.dll")))out->moduleKind=2;
    // GetClassInfoEx(NULL,"Edit") reports hInstance=NULL for the global
    // system class; GCLP_HMODULE reports USER32. The exact standard procedure
    // comparison is necessary, and USER32 supplies the null-instance module.
    const auto standardModule=GetModuleHandleW(L"user32.dll");
    out->standardClass=out->instanceProc&&standardModule&&out->classProc==reinterpret_cast<uintptr_t>(cls.lpfnWndProc)&&out->classModule==reinterpret_cast<uintptr_t>(standardModule);
    if(!out->standardClass){
        out->qualifiedInfoW=SxsEditProcedure(reinterpret_cast<HINSTANCE>(out->classModule));
        if(out->instanceProc&&out->qualifiedInfoW&&out->classProc==out->qualifiedInfoW){out->standardClass=1;out->moduleKind=2;}
    }
    out->writable=!(GetWindowLongPtrW(window,GWL_STYLE)&(ES_READONLY|WS_DISABLED));
    // A disabled parent also prevents real keyboard editing. No window calls
    // here send messages, invoke an application procedure, or change focus.
    HWND p=GetAncestor(window,GA_PARENT);
    for(unsigned i=0;p&&p!=HWND_MESSAGE&&i<8;++i){
        if(GetWindowLongPtrW(p,GWL_STYLE)&WS_DISABLED)out->writable=0;
        const auto next=GetAncestor(p,GA_PARENT);if(next==p)return false;p=next;
    }
    if(p&&p!=HWND_MESSAGE)out->writable=0;
    return true;
}
static bool Is(const OwnerObject& o,uint32_t vt){
    if(!o.pointer||o.state!=ORead||o.vtable!=vt)return false;
    for(const auto& c:kOwnerTypes)if(c.vtable==vt)return o.colOffset==c.offset&&o.typeRva==c.type;
    return false;
}
static bool CaptureValid(const OwnerCommandEvidence& e){
    if(!e.snapshot||!e.exactImageVerified||!e.loadedSpansVerified||!e.ownerBefore||!e.ownerAfter||!e.inputsStable||!e.samplesEqual||e.readFailed||e.limited)return false;
    const auto& s=*e.snapshot;
    return e.targetThreadId&&s.mainThreadId==e.targetThreadId&&s.executor&&s.executorBlock&&!s.zeroRefcounts&&!s.focusMutationDepth&&
        s.windowCount&&s.windowCount<=kOwnerWindows&&e.nativeFocus&&s.windows[0].hwnd==e.nativeFocus;
}
static bool CaptionText(const OwnerSnapshot& s){
    if(!Is(s.manager,0x267421F8)||!s.focusPairMatched||!s.focusToken||s.chainState!=ORead||s.parentCount!=8||
        s.parents[0].object.pointer!=s.focusNode||s.parents[0].selfToken!=s.focusToken)return false;
    constexpr uint32_t nodes[8]={0x27878AA8,0x26E30F30,0x26E30F30,0x27875E58,0x267429D8,0x26743D80,0x26E190E0,0x26E0E7C8};
    constexpr uint32_t delegates[4]={0x27922188,0x27292E88,0x272943F8,0x27294F90};
    for(unsigned i=0;i<8;++i){
        const auto& n=s.parents[i];if(!Is(n.object,nodes[i]))return false;
        for(unsigned j=0;j<i;++j)if(s.parents[j].object.pointer==n.object.pointer)return false;
        if(i==7){if(!n.terminal)return false;break;}
        if(n.terminal||!n.refcount||!n.selfToken||!n.parentMatched||!n.parentContains||(n.flags&0xC)||
            n.parent!=s.parents[i+1].object.pointer||!n.parentToken||(i<6&&n.parentToken!=s.parents[i+1].selfToken))return false;
        if(i<4&&!Is(n.delegate,delegates[i]))return false;
    }
    const auto& w=s.windows[0];const auto& m=s.monitor;const auto& v=s.selection;
    if(!Is(w.property,0x27875E58)||w.property.pointer!=s.parents[3].object.pointer||!Is(w.delegate,0x27294F90)||
        w.delegate.pointer!=s.parents[3].delegate.pointer||!w.delegateBlock||!Is(w.monitor,0x272943F8)||
        w.monitor.pointer!=s.parents[2].delegate.pointer||!Is(w.background,0x27292E88)||w.background.pointer!=s.parents[1].delegate.pointer)return false;
    if(m.state!=ORead||m.fourcc!=0x76636F6D||m.background.pointer!=w.background.pointer||!Is(m.manipulator,0x27996E48)||
        !Is(m.manipulatorOwner,0x279970E8)||!Is(m.currentComponent,0x26662DA0)||!Is(m.currentOwner,0x266632C0)||
        !m.currentProvider.pointer||!m.providerOwner||!m.providerBlock||!m.componentBlock)return false;
    if(v.state!=ORead||!Is(v.manager,0x2723DF48)||(v.mode!=2&&v.mode!=3)||v.componentCount!=1||!Is(v.component,0x26662DA0)||
        v.component.pointer!=m.currentComponent.pointer||!v.matchName.textIdentityKnown||!v.matchName.textIdentity)return false;
    if(v.endpoints[0].state!=ORead||v.endpoints[1].state!=ORead)return false;
    return v.endpoints[0].value>=0||v.endpoints[1].value>=0;
}
RiumOwnerKind OwnerRuntimeClassify(const OwnerCommandEvidence& e,const OwnerNativeEditFacts& edit){
    if(!CaptureValid(e))return RIUM_OWNER_UNKNOWN;const auto& s=*e.snapshot;const auto& w=s.windows[0];
    const bool full=s.kind==OwnerSnapshotKind::Full;
    if((full||s.kind==OwnerSnapshotKind::NativeEditText)&&Is(s.manager,0x267421F8)&&edit.standardClass&&edit.writable&&edit.instanceProc&&edit.classProc&&w.propertyToken&&w.propertyRefcount&&
        (Is(w.property,0x265085F0)||Is(w.property,0x26507BA8)))return RIUM_OWNER_TEXT;
    if((full||s.kind==OwnerSnapshotKind::CaptionText)&&CaptionText(s))return RIUM_OWNER_TEXT;
    if(!full)return RIUM_OWNER_UNKNOWN;
    if(OwnerClassifyCommand(e).classification==OwnerCommandClass::TimelineCommandCandidate)return RIUM_OWNER_COMMAND;
    if(OwnerMonitorCommand(e))return RIUM_OWNER_COMMAND;
    return RIUM_OWNER_UNKNOWN;
}
