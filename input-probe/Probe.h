#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <string>
using Microsoft::WRL::ComPtr;
// Diagnostic identity only. Never use this identity for a shipping input method.
#ifdef RIUM_REUSED_ENGINE
inline constexpr GUID ServiceId={0xe1985813,0x4fa4,0x4b93,{0x8e,0xf4,0xf8,0xee,0x77,0x77,0xe2,0x91}};
inline constexpr GUID ProfileId={0xea007e57,0x6806,0x4596,{0xbb,0x29,0x88,0xeb,0xfb,0xc6,0x20,0xb5}};
#else
inline constexpr GUID ServiceId={0xf5dc1691,0x47ad,0x4622,{0x9c,0x2d,0x85,0x6c,0xf7,0x42,0xb8,0xa2}};
inline constexpr GUID ProfileId={0x50405494,0xe194,0x4c26,{0x9d,0x26,0x1a,0x8b,0x24,0x6f,0x83,0x78}};
#endif
inline std::wstring MappingName(){return L"Local\\RIUM.Keys.ContextProbe."+std::to_wstring(GetCurrentProcessId());}
struct Value { HRESULT result=E_FAIL; VARTYPE type=VT_EMPTY; LONG value=0; };
inline Value ReadValue(IUnknown* owner,REFGUID id) {
    Value out; if(!owner)return out;
    ComPtr<ITfCompartmentMgr> manager; out.result=owner->QueryInterface(IID_PPV_ARGS(&manager));
    if(FAILED(out.result))return out;
    ComPtr<ITfCompartment> compartment; out.result=manager->GetCompartment(id,&compartment);
    if(FAILED(out.result))return out;
    VARIANT value; VariantInit(&value); out.result=compartment->GetValue(&value);
    out.type=value.vt; if(value.vt==VT_I4)out.value=value.lVal;
    VariantClear(&value); return out;
}
struct Sample {
    DWORD pid=0,tid=0,event=0; ULONGLONG tick=0,document=0,context=0,window=0;
    HRESULT statusResult=E_FAIL,viewResult=E_FAIL;
    DWORD dynamicFlags=0,staticFlags=0;
    Value contextDisabled,contextEmpty,threadDisabled,threadEmpty,open;
};
struct Shared { volatile LONG sequence; volatile LONG writer; DWORD targetPid; BOOL composeFixture; volatile LONG activationEntered; volatile LONG sinkResult; volatile LONG testKeyDownCount; volatile LONG editResult; volatile LONG cleanupResult; DWORD clientId; DWORD activationFlags; volatile LONG focusCommits; volatile LONG contextCommits; volatile LONG externalTerminations; Sample sample; };
inline Sample Observe(ITfThreadMgr* manager,ITfContext* offered,DWORD event) {
    Sample out; out.pid=GetCurrentProcessId();out.tid=GetCurrentThreadId();out.event=event;out.tick=GetTickCount64();
    ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context;
    if(manager && SUCCEEDED(manager->GetFocus(&document)) && document)document->GetTop(&context);
    if(offered)context=offered;
    out.document=reinterpret_cast<ULONG_PTR>(document.Get());out.context=reinterpret_cast<ULONG_PTR>(context.Get());
    out.threadDisabled=ReadValue(manager,GUID_COMPARTMENT_KEYBOARD_DISABLED);
    out.threadEmpty=ReadValue(manager,GUID_COMPARTMENT_EMPTYCONTEXT);
    out.open=ReadValue(manager,GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if(context){
        TF_STATUS status{};out.statusResult=context->GetStatus(&status);out.dynamicFlags=status.dwDynamicFlags;out.staticFlags=status.dwStaticFlags;
        out.contextDisabled=ReadValue(context.Get(),GUID_COMPARTMENT_KEYBOARD_DISABLED);
        out.contextEmpty=ReadValue(context.Get(),GUID_COMPARTMENT_EMPTYCONTEXT);
        ComPtr<ITfContextView> view;out.viewResult=context->GetActiveView(&view);
        if(SUCCEEDED(out.viewResult)&&view){HWND window=nullptr;out.viewResult=view->GetWnd(&window);out.window=reinterpret_cast<ULONG_PTR>(window);}
    }
    return out;
}
