#include "Probe.h"
#include "FixtureStore.h"
#include <cstdio>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cstdarg>
#include <share.h>
#include <imm.h>
#include <shlobj.h>
static FILE* diagnosticOutput=stdout;
static void Print(const char* format,...){va_list args;va_start(args,format);vfprintf(diagnosticOutput,format,args);va_end(args);fflush(diagnosticOutput);}
static void Line(const char* value){fputs(value,diagnosticOutput);fputc('\n',diagnosticOutput);fflush(diagnosticOutput);}
static void Char(int value){fputc(value,diagnosticOutput);}
using Entry=HRESULT(__stdcall*)(REFCLSID,REFIID,void**);
static void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Check(HRESULT hr,const char* why){if(FAILED(hr)){Print("HRESULT 0x%08lx: %s\n",static_cast<unsigned long>(hr),why);throw std::runtime_error(why);}}
static std::wstring DllPath(){wchar_t path[MAX_PATH];Require(GetModuleFileNameW(nullptr,path,MAX_PATH)!=0,"module path");std::wstring out=path;return out.substr(0,out.find_last_of(L'\\')+1)+
#ifdef RIUM_REUSED_ENGINE
L"RiumKeysInput.dll";
#else
L"RiumContextProbe.dll";
#endif
}
static std::wstring StagedDirectory(){wchar_t path[MAX_PATH];Check(SHGetFolderPathW(nullptr,CSIDL_PROGRAM_FILES,nullptr,SHGFP_TYPE_CURRENT,path),"resolve Program Files");return std::wstring(path)+L"\\RIUM Keys Context Probe-"+std::to_wstring(GetCurrentProcessId());}
static HRESULT ActivateThread(ITfThreadMgr* manager,TfClientId* client){ComPtr<ITfThreadMgrEx> extended;auto hr=manager->QueryInterface(IID_PPV_ARGS(&extended));return FAILED(hr)?hr:extended->ActivateEx(client,0);}
static void SelfTest(){
    ComPtr<ITfThreadMgr> manager;Check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager)),"thread manager");
    auto missing=ReadValue(manager.Get(),ProfileId);Require(SUCCEEDED(missing.result)&&missing.type==VT_EMPTY,"Missing is not false");
    ComPtr<ITfCompartmentMgr> compartments;Check(manager.As(&compartments),"compartment manager");ComPtr<ITfCompartment> compartment;Check(compartments->GetCompartment(ProfileId,&compartment),"compartment");
    TfClientId client;Check(ActivateThread(manager.Get(),&client),"activate fixture");
    ComPtr<ITfThreadMgrEx> threadEx;DWORD activeFlags=0;if(SUCCEEDED(manager.As(&threadEx))&&SUCCEEDED(threadEx->GetActiveFlags(&activeFlags)))Print("Thread-manager active flags: 0x%08lx\n",activeFlags);
    VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=1;Check(compartment->SetValue(client,&value),"set disabled");Require(ReadValue(manager.Get(),ProfileId).value==1,"nonzero preserved");
    value.lVal=0;Check(compartment->SetValue(client,&value),"set zero");auto zero=ReadValue(manager.Get(),ProfileId);Require(zero.type==VT_I4&&zero.value==0,"explicit zero distinct from missing");
    Require(FAILED(ReadValue(nullptr,ProfileId).result),"missing owner is unknown");
    HMODULE dll=LoadLibraryW(DllPath().c_str());Require(dll!=nullptr,"load diagnostic DLL");
    auto entry=reinterpret_cast<Entry>(GetProcAddress(dll,"DllGetClassObject"));Require(entry!=nullptr,"DLL factory export");
    ComPtr<IClassFactory> factory;Check(entry(ServiceId,IID_PPV_ARGS(&factory)),"factory");
    ComPtr<ITfTextInputProcessorEx> service;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&service)),"service");
    // Manual activation is not a system-activation test. Record its result only;
    // actual TSF activation is verified separately by the process-local fixture.
    auto activation=service->ActivateEx(manager.Get(),client,0);Print("Manual diagnostic activation HRESULT: 0x%08lx\n",static_cast<unsigned long>(activation));
    ComPtr<ITfKeyEventSink> sink;Check(service.As(&sink),"key event sink");
    BOOL eaten=TRUE;Check(sink->OnTestKeyDown(nullptr,'A',0,&eaten),"test key");Require(!eaten,"test callback must never consume input");
    eaten=TRUE;Check(sink->OnKeyDown(nullptr,'A',0,&eaten),"key down");Require(!eaten,"key down must never consume input");
    eaten=TRUE;Check(sink->OnKeyUp(nullptr,'A',0,&eaten),"key up");Require(!eaten,"key up must never consume input");
    Check(service->Deactivate(),"deactivate");Check(service->Deactivate(),"idempotent deactivate");sink.Reset();service.Reset();factory.Reset();FreeLibrary(dll);compartments->ClearCompartment(client,ProfileId);manager->Deactivate();
    Line("PASS: TSF missing/zero/nonzero/error states; COM lifecycle; diagnostic key pass-through.");
}
static std::wstring ClassPath(){wchar_t guid[40];StringFromGUID2(ServiceId,guid,40);return std::wstring(L"Software\\Classes\\CLSID\\")+guid;}
static void DumpTip(HKEY key,int depth=0){
    if(depth>4)return;
    for(DWORD i=0;i<20;++i){wchar_t name[256]{};DWORD size=256,type=0,bytes=1024;BYTE data[1024]{};auto result=RegEnumValueW(key,i,name,&size,nullptr,&type,data,&bytes);if(result!=ERROR_SUCCESS)break;if(type==REG_DWORD&&bytes==4){DWORD value;memcpy(&value,data,4);Print("TIP value %d %ls = %lu\n",depth,name,value);}else if(type==REG_SZ&&bytes<sizeof(data))Print("TIP value %d %ls = %ls\n",depth,name,reinterpret_cast<wchar_t*>(data));}
    for(DWORD i=0;i<40;++i){wchar_t name[256];DWORD size=256;auto result=RegEnumKeyExW(key,i,name,&size,nullptr,nullptr,nullptr,nullptr);if(result!=ERROR_SUCCESS)break;Print("TIP depth %d: %ls\n",depth,name);HKEY child;if(RegOpenKeyExW(key,name,0,KEY_READ,&child)==ERROR_SUCCESS){DumpTip(child,depth+1);RegCloseKey(child);}}
}
static void DumpRegisteredTip(){
    wchar_t service[40];StringFromGUID2(ServiceId,service,40);auto path=std::wstring(L"SOFTWARE\\Microsoft\\CTF\\TIP\\")+service;HKEY key;auto result=RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_READ,&key);Print("TIP registry open: %ld\n",result);if(result==ERROR_SUCCESS){DumpTip(key);RegCloseKey(key);}
}
static bool UserTip(bool enable){
    auto input=LoadLibraryExW(L"input.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!input)return false;
    using Install=BOOL(CALLBACK*)(LPCWSTR,DWORD);auto install=reinterpret_cast<Install>(GetProcAddress(input,"InstallLayoutOrTip"));
    wchar_t service[40],profile[40];StringFromGUID2(ServiceId,service,40);StringFromGUID2(ProfileId,profile,40);auto id=std::wstring(L"0x0412:")+service+profile;
    // No default-profile, default-user, or clean-install flags. Only this TIP.
    bool ok=install&&install(id.c_str(),enable?0:1)!=FALSE;FreeLibrary(input);return ok;
}
static HRESULT RefreshTipCache(){
    auto module=LoadLibraryExW(L"msctf.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return HRESULT_FROM_WIN32(GetLastError());
    using Refresh=HRESULT(WINAPI*)();auto refresh=reinterpret_cast<Refresh>(GetProcAddress(module,"TF_InvalidAssemblyListCacheIfExist"));
    auto hr=refresh?refresh():E_NOTIMPL;FreeLibrary(module);Print("TIP description cache refresh: 0x%08lx\n",static_cast<unsigned long>(hr));return hr;
}
static bool CategoryExists(ITfCategoryMgr* manager){
    ComPtr<IEnumGUID> list;Check(manager->EnumCategoriesInItem(ServiceId,&list),"enumerate diagnostic categories");GUID id;ULONG count=0;
    while(list->Next(1,&id,&count)==S_OK)if(id==GUID_TFCAT_TIP_KEYBOARD)return true;return false;
}
static void RecoverRegistration(){
    // Recovery is intentionally restricted to this diagnostic CLSID and the DLL
    // beside this controller. It never removes an arbitrary input method.
    HANDLE mapping=OpenEventW(SYNCHRONIZE,FALSE,L"Local\\RIUM.Keys.Probe.Ready");if(mapping){CloseHandle(mapping);throw std::runtime_error("Diagnostic still running; recovery refused");}
    Require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot establish that diagnostic is stopped");
    HKEY key;auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,(ClassPath()+L"\\InprocServer32").c_str(),0,KEY_READ,&key);bool exists=opened==ERROR_SUCCESS;
    Require(exists||opened==ERROR_FILE_NOT_FOUND,"Cannot inspect diagnostic COM entry");
    if(exists){wchar_t path[MAX_PATH]{};DWORD type=0,size=sizeof(path);auto read=RegQueryValueExW(key,nullptr,nullptr,&type,reinterpret_cast<BYTE*>(path),&size);RegCloseKey(key);Require(read==ERROR_SUCCESS&&type==REG_SZ&&size>=sizeof(wchar_t)&&size<=sizeof(path)&&path[size/sizeof(wchar_t)-1]==0&&_wcsicmp(path,DllPath().c_str())==0,"COM entry belongs to a different location; recovery refused");}
    ComPtr<ITfCategoryMgr> manager;Check(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager)),"recovery category manager");
    if(CategoryExists(manager.Get()))Check(manager->UnregisterCategory(ServiceId,GUID_TFCAT_TIP_KEYBOARD,ServiceId),"recover diagnostic category");
    Require(!CategoryExists(manager.Get()),"diagnostic category remains");
    HKEY tipKey;wchar_t serviceGuid[40];StringFromGUID2(ServiceId,serviceGuid,40);auto tipPath=std::wstring(L"SOFTWARE\\Microsoft\\CTF\\TIP\\")+serviceGuid;
    auto tipOpened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,tipPath.c_str(),0,KEY_READ,&tipKey);
    if(tipOpened==ERROR_SUCCESS){RegCloseKey(tipKey);Require(UserTip(false),"disable diagnostic user profile");ComPtr<ITfInputProcessorProfiles> profiles;Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles)),"recovery profile manager");Check(profiles->Unregister(ServiceId),"recover diagnostic TIP");}
    else Require(tipOpened==ERROR_FILE_NOT_FOUND,"Cannot inspect temporary TIP registration");
    if(exists)Require(RegDeleteTreeW(HKEY_LOCAL_MACHINE,ClassPath().c_str())==ERROR_SUCCESS,"recover diagnostic COM entry");
    HKEY remaining;auto result=RegOpenKeyExW(HKEY_LOCAL_MACHINE,ClassPath().c_str(),0,KEY_READ,&remaining);if(result==ERROR_SUCCESS)RegCloseKey(remaining);Require(result==ERROR_FILE_NOT_FOUND,"diagnostic COM entry remains");
    Line("Verified: diagnostic category and temporary machine COM registration absent.");
}
struct Registration{
    bool com=false,localProfile=false,category=false,immersive=false,display=false,tip=false,userTip=false;
    bool staged=false;std::wstring stagedDirectory,stagedDll;
    ComPtr<ITfInputProcessorProfiles> profiles;ComPtr<ITfCategoryMgr> categories;
    Registration(){Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles)),"profile manager");Check(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&categories)),"category manager");}
    void Install(){
        HKEY existing;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,ClassPath().c_str(),0,KEY_READ,&existing)==ERROR_SUCCESS){RegCloseKey(existing);throw std::runtime_error("Diagnostic class already exists; inspect before replacing");}
        stagedDirectory=StagedDirectory();Require(CreateDirectoryW(stagedDirectory.c_str(),nullptr)!=FALSE,"create isolated Program Files staging directory (must not exist)");staged=true;stagedDll=stagedDirectory+L"\\RiumContextProbe.dll";Require(CopyFileW(DllPath().c_str(),stagedDll.c_str(),TRUE)!=FALSE,"stage diagnostic DLL");
        HKEY key;DWORD disposition;auto result=RegCreateKeyExW(HKEY_LOCAL_MACHINE,(ClassPath()+L"\\InprocServer32").c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key,&disposition);
        Require(result==ERROR_SUCCESS,"create temporary machine diagnostic class");if(disposition!=REG_CREATED_NEW_KEY){RegCloseKey(key);throw std::runtime_error("Diagnostic registration appeared concurrently; left untouched");}com=true;auto dll=stagedDll;auto pathResult=RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(dll.c_str()),static_cast<DWORD>((dll.size()+1)*sizeof(wchar_t)));
        const wchar_t threading[]=L"Apartment";auto modelResult=RegSetValueExW(key,L"ThreadingModel",0,REG_SZ,reinterpret_cast<const BYTE*>(threading),sizeof(threading));RegCloseKey(key);Require(pathResult==ERROR_SUCCESS&&modelResult==ERROR_SUCCESS,"class registration values");
        const wchar_t title[]=L"RIUM Context Probe (diagnostic)";
        HKEY classKey;Require(RegOpenKeyExW(HKEY_LOCAL_MACHINE,ClassPath().c_str(),0,KEY_SET_VALUE,&classKey)==ERROR_SUCCESS,"open diagnostic class description");auto titleResult=RegSetValueExW(classKey,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(title),sizeof(title));RegCloseKey(classKey);Require(titleResult==ERROR_SUCCESS,"set diagnostic class description");
        ComPtr<ITfInputProcessorProfileMgr> modern;Check(profiles.As(&modern),"modern registration interface");
        auto addHr=modern->RegisterProfile(ServiceId,0x412,ProfileId,title,static_cast<ULONG>(wcslen(title)),dll.c_str(),static_cast<ULONG>(dll.size()),0,nullptr,0,TRUE,0);Print("RegisterProfile HRESULT: 0x%08lx\n",static_cast<unsigned long>(addHr));Require(addHr==S_OK,"register temporary diagnostic language profile");localProfile=true;tip=true;
        auto enableHr=profiles->EnableLanguageProfile(ServiceId,0x412,ProfileId,TRUE);Print("EnableLanguageProfile HRESULT: 0x%08lx\n",static_cast<unsigned long>(enableHr));Require(enableHr==S_OK,"enable temporary diagnostic language profile");
        auto categoryHr=categories->RegisterCategory(ServiceId,GUID_TFCAT_TIP_KEYBOARD,ServiceId);Print("Keyboard category registration HRESULT: 0x%08lx\n",static_cast<unsigned long>(categoryHr));category=categoryHr==S_OK;
        Require(category,"Keyboard-category registration failed; no input profile was selected");
        auto displayHr=categories->RegisterCategory(ServiceId,GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,ServiceId);display=displayHr==S_OK;Require(display,"register composition display attributes");
        // This diagnostic has no UI, files, network or required cross-process IPC.
        // An inaccessible optional metadata mapping simply disables recording.
        auto immersiveHr=categories->RegisterCategory(ServiceId,GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,ServiceId);Print("Windows-app compatibility HRESULT: 0x%08lx\n",static_cast<unsigned long>(immersiveHr));immersive=immersiveHr==S_OK;Require(immersive,"register diagnostic Windows-app compatibility");
        userTip=true;Require(UserTip(true),"enable diagnostic TIP for current user");Line("Current-user diagnostic TIP enabled without changing default.");
        Check(RefreshTipCache(),"refresh registered input profile cache");
        Print("Keyboard category enumeration confirms: %d\n",CategoryExists(categories.Get()));
        wchar_t serviceGuid[40];StringFromGUID2(ServiceId,serviceGuid,40);auto tipPath=std::wstring(L"SOFTWARE\\Microsoft\\CTF\\TIP\\")+serviceGuid;
        HKEY tipKey;auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,tipPath.c_str(),0,KEY_READ,&tipKey);Print("Machine TIP registry open result: %ld\n",opened);
        if(opened==ERROR_SUCCESS){for(DWORD i=0;;++i){wchar_t name[256];DWORD count=256;auto enumerated=RegEnumKeyExW(tipKey,i,name,&count,nullptr,nullptr,nullptr,nullptr);if(enumerated!=ERROR_SUCCESS)break;Print("TIP subkey: %ls\n",name);}RegCloseKey(tipKey);}
    }
    bool Cleanup(){
        bool changed=com||localProfile||category||immersive||display||tip||userTip;
        bool ok=true;
        if(userTip){if(UserTip(false))userTip=false;else{Line("Diagnostic user profile disable failed");ok=false;}}
        if(localProfile){auto hr=profiles->RemoveLanguageProfile(ServiceId,0x412,ProfileId);if(hr==S_OK)localProfile=false;else{Print("Profile cleanup HRESULT: 0x%08lx\n",static_cast<unsigned long>(hr));ok=false;}}
        if(category){auto hr=categories->UnregisterCategory(ServiceId,GUID_TFCAT_TIP_KEYBOARD,ServiceId);if(hr==S_OK)category=false;else{Print("Category cleanup HRESULT: 0x%08lx\n",static_cast<unsigned long>(hr));ok=false;}}
        if(immersive){auto hr=categories->UnregisterCategory(ServiceId,GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,ServiceId);if(hr==S_OK)immersive=false;else{Print("Compatibility cleanup HRESULT: 0x%08lx\n",static_cast<unsigned long>(hr));ok=false;}}
        if(display){auto hr=categories->UnregisterCategory(ServiceId,GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,ServiceId);if(hr==S_OK)display=false;else ok=false;}
        if(tip){auto hr=profiles->Unregister(ServiceId);if(hr==S_OK)tip=false;else{Print("TIP cleanup HRESULT: 0x%08lx\n",static_cast<unsigned long>(hr));ok=false;}}
        if(com){auto result=RegDeleteTreeW(HKEY_LOCAL_MACHINE,ClassPath().c_str());if(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND)com=false;else{Print("COM cleanup Win32 error: %ld\n",result);ok=false;}}
        if(changed&&FAILED(RefreshTipCache()))ok=false;
        if(staged){auto deleted=DeleteFileW(stagedDll.c_str());auto error=deleted?ERROR_SUCCESS:GetLastError();
            if(!deleted&&error!=ERROR_FILE_NOT_FOUND){Print("Diagnostic DLL cleanup deferred; loaded by a Windows input host. Win32 error: %lu; file: %ls\n",error,stagedDll.c_str());ok=false;}
            else if(RemoveDirectoryW(stagedDirectory.c_str())||GetLastError()==ERROR_PATH_NOT_FOUND)staged=false;else ok=false;}
        return ok;
    }
    ~Registration(){Cleanup();}
};
static void PrintValue(const char* name,const Value& value){Print("\"%s\":{\"hr\":%ld,\"type\":%u,\"value\":%ld}",name,static_cast<long>(value.result),value.type,value.value);}
static void PrintSample(const Sample& s){
    Print("{\"pid\":%lu,\"tid\":%lu,\"event\":%lu,\"tick\":%llu,\"document\":%llu,\"context\":%llu,\"hwnd\":%llu,\"statusHr\":%ld,\"dynamic\":%lu,\"static\":%lu,",s.pid,s.tid,s.event,s.tick,s.document,s.context,s.window,static_cast<long>(s.statusResult),s.dynamicFlags,s.staticFlags);
    PrintValue("disabled",s.contextDisabled);Char(',');PrintValue("empty",s.contextEmpty);Char(',');PrintValue("threadDisabled",s.threadDisabled);Char(',');PrintValue("threadEmpty",s.threadEmpty);Char(',');PrintValue("open",s.open);Line("}");fflush(diagnosticOutput);
}
static bool CompositionChecks(ITfKeystrokeMgr* keys,ITfThreadMgr* thread,TfClientId client,ITfDocumentMgr* document,ITfContext* context,FixtureStore* store){
    auto press=[&](UINT key){BOOL preview=FALSE,eaten=FALSE;auto flags=1|(static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16);if(keys->TestKeyDown(key,flags,&preview)!=S_OK)return -1;if(!preview)return 0;if(keys->KeyDown(key,flags,&eaten)!=S_OK)return -1;return eaten?1:0;};
    int checked=0;auto check=[&](bool result,const char* name){++checked;if(!result)Print("FAIL composition integration: %s\n",name);return result;};bool ok=true;
    store->Clear();ok&=check(press('R')==1&&store->text==L"\u3131","standalone consonant");ok&=check(press(VK_BACK)==1&&store->text.empty(),"backspace removes preedit");
    ok&=check(press('G')==1,"begin composition before rejected write");store->rejectNextWrite=true;ok&=check(press('K')==0&&store->text==L"\u314e","rejected edit passes key without changing document");store->InsertFromApplication(L"k");ok&=check(press('K')==1&&store->text==L"\u314ek\u314f","new composition does not overwrite application fallback text");press(VK_SPACE);store->Clear();press('G');press('K');
    BYTE original[256]{};GetKeyboardState(original);BYTE modified[256];memcpy(modified,original,sizeof(modified));modified[VK_CONTROL]=0x80;SetKeyboardState(modified);auto chord=press('C');SetKeyboardState(original);ok&=check(chord==0&&store->text==L"\ud558","Ctrl chord commits and passes");
    ComPtr<ITfCompartmentMgr> compartments;context->QueryInterface(IID_PPV_ARGS(&compartments));
    for(auto id:{GUID_COMPARTMENT_KEYBOARD_DISABLED,GUID_COMPARTMENT_EMPTYCONTEXT}){ComPtr<ITfCompartment> part;compartments->GetCompartment(id,&part);VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=1;part->SetValue(client,&value);auto before=store->text;ok&=check(press('V')==0&&store->text==before,"disabled or empty context passes shortcut");compartments->ClearCompartment(client,id);}
    ok&=check(press(VK_HANGUL)==1&&press('A')==0,"explicit Latin mode passes letters");ok&=check(press(VK_HANGUL)==1,"return to Korean mode");
    store->Clear();ok&=check(press('G')==1&&press('K')==1&&store->text==L"\ud558","first field pending composition");
    ComPtr<FixtureStore> second;HWND window=nullptr;store->GetWnd(0,&window);second.Attach(new FixtureStore(window));ComPtr<ITfDocumentMgr> nextDocument;ComPtr<ITfContext> nextContext;TfEditCookie cookie;
    if(FAILED(thread->CreateDocumentMgr(&nextDocument))||FAILED(nextDocument->CreateContext(client,0,static_cast<ITextStoreACP*>(second.Get()),&nextContext,&cookie))||FAILED(nextDocument->Push(nextContext.Get())))return false;
    thread->SetFocus(nextDocument.Get());ok&=check(press('R')==1&&press('K')==1&&second->text==L"\uac00"&&store->text==L"\ud558","focus change keeps separate document composition");press(VK_SPACE);thread->SetFocus(document);nextDocument->Pop(TF_POPF_ALL);
    ok&=check(press('S')==1&&store->text==L"\ud558\u3134","return to first field starts a new syllable");press(VK_SPACE);
    store->Clear();store->rejectNextText=true;ok&=check(press('R')==0&&store->text.empty(),"failed text after StartComposition passes key");store->InsertFromApplication(L"r");ok&=check(press('K')==1&&store->text==L"r\u314f","failed new composition does not capture later text");press(VK_SPACE);
    store->Clear();press('G');press('K');press('S');store->rejectNextSelection=true;ok&=check(press('R')==1&&store->text==L"\ud55c\u3131","post-write failure still consumes inserted letter");ok&=check(press('K')==1&&store->text==L"\ud55c\u3131\u314f","post-write failure cannot overwrite committed prefix");press(VK_SPACE);
    Print("Composition integration checks: %d; passed: %d\n",checked,ok);return ok;
}
// This surface displays the fixture's single ITextStoreACP. A native EDIT has
// its own IMM text store: copying text to it terminates our separate composition.
static LRESULT CALLBACK TextSurface(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_LBUTTONDOWN){SetFocus(window);return 0;}
    if(message==WM_SETFOCUS){CreateCaret(window,nullptr,2,20);SetCaretPos(4,4);ShowCaret(window);return 0;}
    if(message==WM_KILLFOCUS){DestroyCaret();return 0;}
    if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT area{};GetClientRect(window,&area);FillRect(dc,&area,GetSysColorBrush(COLOR_WINDOW));area.left+=4;area.top+=3;auto store=reinterpret_cast<FixtureStore*>(GetWindowLongPtrW(window,GWLP_USERDATA));SetBkMode(dc,TRANSPARENT);if(store)DrawTextW(dc,store->text.c_str(),-1,&area,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);EndPaint(window,&paint);return 0;}
    return DefWindowProcW(window,message,w,l);
}
static void Session(DWORD seconds,bool interactive=false,bool native=false){
    HANDLE ownToken=nullptr;TOKEN_ELEVATION elevation{};DWORD tokenSize=0;
    Require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&ownToken)!=FALSE,"inspect fixture token");
    auto tokenRead=GetTokenInformation(ownToken,TokenElevation,&elevation,sizeof(elevation),&tokenSize);CloseHandle(ownToken);
    Require(tokenRead!=FALSE,"read fixture elevation");Print("Fixture elevated: %lu\n",elevation.TokenIsElevated);
    Require(UserTip(true),"enable diagnostic TIP in actual fixture user");Check(RefreshTipCache(),"refresh actual fixture user profile cache");
    const DWORD pid=GetCurrentProcessId();
    DumpRegisteredTip();
    Require(pid>0&&seconds>0&&seconds<=120,"valid target and 1-120 second duration required");
    WNDCLASSW windowClass{};windowClass.lpfnWndProc=DefWindowProcW;windowClass.hInstance=GetModuleHandleW(nullptr);windowClass.lpszClassName=L"RiumIsolatedInputProbe";RegisterClassW(&windowClass);
    WNDCLASSW textClass{};textClass.lpfnWndProc=TextSurface;textClass.hInstance=windowClass.hInstance;textClass.lpszClassName=L"RiumFixtureTextSurface";textClass.hCursor=LoadCursorW(nullptr,IDC_IBEAM);RegisterClassW(&textClass);
    HWND fixture=CreateWindowExW(0,windowClass.lpszClassName,L"RIUM Keys - isolated input probe",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,520,180,nullptr,nullptr,windowClass.hInstance,nullptr);Require(fixture!=nullptr,"create isolated fixture");
    HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,native?L"EDIT":textClass.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(native?ES_AUTOHSCROLL:0),20,20,450,30,fixture,nullptr,windowClass.hInstance,nullptr);
    HWND shortcut=CreateWindowExW(0,L"BUTTON",L"Shortcut surface (V)",WS_CHILD|WS_VISIBLE|WS_TABSTOP,20,65,210,28,fixture,nullptr,windowClass.hInstance,nullptr);
    // First ShowWindow can be overridden by the launcher's SW_HIDE startup flag.
    // This disposable fixture needs an actual foreground edit context.
    ShowWindow(fixture,SW_SHOW);ShowWindow(fixture,SW_SHOW);SetForegroundWindow(fixture);SetFocus(edit);
    Print("Fixture visible: %d; foreground: %d; edit focused: %d\n",IsWindowVisible(fixture)!=FALSE,GetForegroundWindow()==fixture,GetFocus()==edit);
    auto inputContext=ImmGetContext(edit);Print("Native edit IME context exists: %d\n",inputContext!=nullptr);if(inputContext)ImmReleaseContext(edit,inputContext);
    MSG initialMessage;while(PeekMessageW(&initialMessage,nullptr,0,0,PM_REMOVE)){TranslateMessage(&initialMessage);DispatchMessageW(&initialMessage);}
    ComPtr<ITfThreadMgr> thread;TfClientId client;Check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&thread)),"diagnostic thread manager");Check(ActivateThread(thread.Get(),&client),"diagnostic thread activation");
    ComPtr<ITfMessagePump> messagePump;Check(thread.As(&messagePump),"TSF-aware message pump");
    // The native EDIT creates a transitory IMM compatibility context. Exercise
    // ITfKeystrokeMgr against an explicit TSF host context, as a TSF app does.
    ComPtr<ITfDocumentMgr> hostDocument;ComPtr<ITfContext> hostContext;TfEditCookie hostCookie;
    Check(thread->CreateDocumentMgr(&hostDocument),"create explicit TSF document");
    ComPtr<FixtureStore> hostStore;hostStore.Attach(new FixtureStore(edit));
    if(!native)SetWindowLongPtrW(edit,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(hostStore.Get()));
    Check(hostDocument->CreateContext(client,0,static_cast<ITextStoreACP*>(hostStore.Get()),&hostContext,&hostCookie),"create explicit TSF context");
    Check(hostDocument->Push(hostContext.Get()),"push explicit TSF context");
    Check(thread->SetFocus(hostDocument.Get()),"focus explicit TSF document");
    ComPtr<ITfThreadMgrEx> threadEx;DWORD activeFlags=0;if(SUCCEEDED(thread.As(&threadEx))&&SUCCEEDED(threadEx->GetActiveFlags(&activeFlags)))Print("Thread-manager active flags: 0x%08lx\n",activeFlags);
    ComPtr<ITfInputProcessorProfileMgr> manager;Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager)),"profile control");
    ComPtr<ITfInputProcessorProfiles> legacy;Check(manager.As(&legacy),"legacy profile view");BOOL enabled=FALSE;auto enabledHr=legacy->IsEnabledLanguageProfile(ServiceId,0x412,ProfileId,&enabled);Print("Legacy enabled HRESULT: 0x%08lx; enabled: %d\n",static_cast<unsigned long>(enabledHr),enabled);
    ComPtr<IEnumGUID> services;auto serviceEnumHr=legacy->EnumInputProcessorInfo(&services);bool serviceFound=false;if(SUCCEEDED(serviceEnumHr)&&services){GUID id;ULONG count;while(services->Next(1,&id,&count)==S_OK)if(id==ServiceId)serviceFound=true;}Print("Service enumeration HRESULT: 0x%08lx; found: %d\n",static_cast<unsigned long>(serviceEnumHr),serviceFound);
    ComPtr<ITfCategoryMgr> categoryManager;Check(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&categoryManager)),"fixture category manager");GUID nearest{};const GUID* supported[]={&GUID_TFCAT_TIP_KEYBOARD};auto nearestHr=categoryManager->FindClosestCategory(ServiceId,&nearest,supported,1);Print("Fresh keyboard category HRESULT: 0x%08lx; matches: %d; enumerated: %d\n",static_cast<unsigned long>(nearestHr),nearest==GUID_TFCAT_TIP_KEYBOARD,CategoryExists(categoryManager.Get()));
    BSTR description=nullptr;auto descHr=legacy->GetLanguageProfileDescription(ServiceId,0x412,ProfileId,&description);Print("Profile description HRESULT: 0x%08lx; characters: %u\n",static_cast<unsigned long>(descHr),description?SysStringLen(description):0);SysFreeString(description);
    ComPtr<IEnumTfLanguageProfiles> enumerator;auto enumHr=legacy->EnumLanguageProfiles(0x412,&enumerator);bool found=false;HRESULT nextHr=E_PENDING;ULONG fetched=0,total=0;
    if(SUCCEEDED(enumHr)&&enumerator){TF_LANGUAGEPROFILE item{};while((nextHr=enumerator->Next(1,&item,&fetched))==S_OK){++total;if(item.clsid==ServiceId){found=true;wchar_t category[40];StringFromGUID2(item.catid,category,40);Print("Legacy enumerated diagnostic category: %ls; profile matches: %d\n",category,item.guidProfile==ProfileId);}}}Print("Legacy enumeration HRESULT: 0x%08lx; found: %d; terminal: 0x%08lx; fetched: %lu; count: %lu\n",static_cast<unsigned long>(enumHr),found,static_cast<unsigned long>(nextHr),fetched,total);
    TF_INPUTPROCESSORPROFILE previous{};Check(manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&previous),"capture previous input profile");
    ComPtr<ITfTextInputProcessorEx> registeredService;
    auto serviceHr=CoCreateInstance(ServiceId,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&registeredService));
    Print("Registered COM service HRESULT: 0x%08lx\n",static_cast<unsigned long>(serviceHr));registeredService.Reset();
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),MappingName().c_str());Require(mapping!=nullptr,"create diagnostic mapping");
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mapping);throw std::runtime_error("Another diagnostic session exists");}
    auto shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!shared){CloseHandle(mapping);throw std::runtime_error("map diagnostic state");}
    shared->targetPid=pid;
    const DWORD scope=TF_IPPMF_FORPROCESS;
    TF_INPUTPROCESSORPROFILE requested{};auto profileStart=GetTickCount64();HRESULT requestedHr=E_PENDING;
    do{requestedHr=manager->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,ServiceId,ProfileId,nullptr,&requested);if(requestedHr==S_OK)break;MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(20);}while(GetTickCount64()-profileStart<3000);
    wchar_t categoryGuid[40]{};StringFromGUID2(requested.catid,categoryGuid,40);Print("Requested profile HRESULT: 0x%08lx; flags: 0x%08lx; category: %ls; readiness wait: %llu ms\n",static_cast<unsigned long>(requestedHr),requested.dwFlags,categoryGuid,GetTickCount64()-profileStart);
    HRESULT hr=manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,ServiceId,ProfileId,nullptr,scope);
    TF_INPUTPROCESSORPROFILE active{};auto activeHr=manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);
    bool matched=activeHr==S_OK&&active.clsid==ServiceId&&active.guidProfile==ProfileId;
    wchar_t activeGuid[40]{};StringFromGUID2(active.clsid,activeGuid,40);Print("Active profile HRESULT: 0x%08lx; type: %lu; lang: %u; CLSID: %ls\n",static_cast<unsigned long>(activeHr),active.dwProfileType,active.langid,activeGuid);
    Print("Activate HRESULT 0x%08lx; active keyboard profile matches diagnostic: %s\n",static_cast<unsigned long>(hr),matched?"yes":"no");
    bool dispatched=false,compositionVerified=false,physicalStarted=false,physicalVerified=!interactive;int physicalShortcuts=0;bool enteredFirstWord=false;
    if(hr==S_OK){
        Line("Process-local activation requested; verifying callback delivery.");fflush(diagnosticOutput);
        auto end=GetTickCount64()+static_cast<ULONGLONG>(seconds)*1000;LONG last=0;
        while(GetTickCount64()<end&&IsWindow(fixture)){
            MSG message;BOOL available=FALSE;
            auto nextMessage=[&](){if(native&&physicalStarted)return PeekMessageW(&message,nullptr,0,0,PM_REMOVE)!=FALSE;return messagePump->PeekMessageW(&message,nullptr,0,0,PM_REMOVE,&available)==S_OK&&available;};
            while(nextMessage()){
                if(physicalStarted&&native&&message.message==WM_KEYDOWN&&GetFocus()==shortcut&&message.wParam=='V'){++physicalShortcuts;Print("Native shortcut V passed to application: %d\n",physicalShortcuts);continue;}
                if(physicalStarted&&!native&&(message.message==WM_KEYDOWN||message.message==WM_KEYUP)){
                    Print("Fixture queued key: down=%d; already processed=%d; focus commits=%ld; context commits=%ld; external terminations=%ld; edit result=0x%08lx\n",message.message==WM_KEYDOWN,message.wParam==VK_PROCESSKEY,shared->focusCommits,shared->contextCommits,shared->externalTerminations,static_cast<unsigned long>(shared->editResult));
                    bool inText=GetFocus()==edit;
                    ComPtr<ITfDocumentMgr> focused;thread->GetFocus(&focused);
                    auto desired=inText?hostDocument.Get():nullptr;
                    // SetFocus terminates active compositions even when repeated
                    // for the same document. Only actual control transitions do it.
                    if(focused.Get()!=desired)thread->SetFocus(desired);
                    ComPtr<ITfKeystrokeMgr> keys;thread.As(&keys);BOOL tested=FALSE,eaten=FALSE;
                    if(message.message==WM_KEYDOWN){if(keys->TestKeyDown(message.wParam,message.lParam,&tested)==S_OK&&tested)keys->KeyDown(message.wParam,message.lParam,&eaten);}
                    else if(keys->TestKeyUp(message.wParam,message.lParam,&tested)==S_OK&&tested)keys->KeyUp(message.wParam,message.lParam,&eaten);
                    if(eaten){InvalidateRect(edit,nullptr,TRUE);continue;}
                    if(!inText&&message.message==WM_KEYDOWN&&message.wParam=='V'){++physicalShortcuts;Print("Physical shortcut V passed to application: %d\n",physicalShortcuts);continue;}
                }
                if(physicalStarted&&!native&&message.message==WM_CHAR&&GetFocus()==edit&&message.wParam>=32){hostStore->InsertFromApplication(std::wstring(1,static_cast<wchar_t>(message.wParam)));InvalidateRect(edit,nullptr,TRUE);continue;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(!dispatched&&shared->activationEntered&&shared->sequence>0){
                Check(thread->SetFocus(hostDocument.Get()),"focus host for system dispatch");
                ComPtr<ITfKeystrokeMgr> keys;auto keysHr=thread.As(&keys);BOOL eaten=TRUE;
                if(SUCCEEDED(keysHr))keysHr=keys->TestKeyDown('A',1|(static_cast<LPARAM>(MapVirtualKeyW('A',MAPVK_VK_TO_VSC))<<16),&eaten);
                Print("System keystroke dispatch HRESULT: 0x%08lx; eaten: %d\n",static_cast<unsigned long>(keysHr),eaten);dispatched=true;
                if(keysHr==S_OK&&shared->testKeyDownCount>0&&!eaten){
                    shared->composeFixture=TRUE;bool letters=true;
                    for(auto key:std::wstring(L"GKSRMF")){
                        LPARAM flags=1|(static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16);BOOL preview=FALSE,again=FALSE,handled=FALSE;
                        auto a=keys->TestKeyDown(key,flags,&preview);auto b=keys->TestKeyDown(key,flags,&again);auto c=preview?keys->KeyDown(key,flags,&handled):E_FAIL;
                        if(a!=S_OK||b!=S_OK||c!=S_OK||!preview||!again||!handled){Print("Fixture composition step failed; edit HRESULT: 0x%08lx\n",static_cast<unsigned long>(shared->editResult));letters=false;break;}
                    }
                    BOOL preview=FALSE,handled=TRUE;auto test=keys->TestKeyDown(VK_SPACE,1,&preview);auto commit=preview?keys->KeyDown(VK_SPACE,1,&handled):E_FAIL;
                    compositionVerified=letters&&hostStore->text==L"\ud55c\uae00"&&test==S_OK&&commit==S_OK&&preview&&!handled;
                    Print("Korean document text matches fixture: %d; commit passes original space: %d\n",hostStore->text==L"\ud55c\uae00",!handled);
                    compositionVerified=CompositionChecks(keys.Get(),thread.Get(),client,hostDocument.Get(),hostContext.Get(),hostStore.Get())&&compositionVerified;shared->composeFixture=FALSE;
                    if(interactive&&compositionVerified){hostStore->Clear();InvalidateRect(edit,nullptr,TRUE);shared->composeFixture=TRUE;physicalStarted=true;
                        if(native){thread->SetFocus(nullptr);SetFocus(shortcut);SetFocus(edit);Line("Native EDIT fixture ready; no application TSF key dispatch.");}
                        else{ComPtr<ITfDocumentMgr> old;thread->AssociateFocus(edit,hostDocument.Get(),&old);SetFocus(edit);thread->SetFocus(hostDocument.Get());}
                        Line("Interactive fixture ready: type gksrmf, click shortcut surface and press V, then return and type gksrmf.");}
                }
            }
            auto sequence=InterlockedCompareExchange(&shared->sequence,0,0);
            if(sequence>0&&sequence!=last&&!(sequence&1)){Sample sample=shared->sample;MemoryBarrier();if(sequence==InterlockedCompareExchange(&shared->sequence,0,0)){PrintSample(sample);last=sequence;}}
            if(physicalStarted){std::wstring actual=hostStore->text;if(native){wchar_t text[256]{};GetWindowTextW(edit,text,256);actual=text;}if(actual==L"\ud55c\uae00")enteredFirstWord=true;if(enteredFirstWord&&physicalShortcuts>0&&actual==L"\ud55c\uae00\ud55c\uae00"){physicalVerified=true;break;}}
            else if(shared->activationEntered&&shared->testKeyDownCount>0)break;
            Sleep(10);
        }
    }
    bool callback=shared->sequence>0;
    Print("Physical typing and shortcut transition verified: %d; interactive requested: %d\n",physicalVerified,interactive);(void)shortcut;
    bool keyCallback=shared->testKeyDownCount>0;Print("System-dispatched test-key callbacks: %ld\n",shared->testKeyDownCount);
    Print("ActivateEx entered: %ld; client: %lu; flags: 0x%08lx; sink HRESULT: 0x%08lx\n",shared->activationEntered,shared->clientId,shared->activationFlags,static_cast<unsigned long>(shared->sinkResult));
    activeHr=manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);matched=activeHr==S_OK&&active.clsid==ServiceId&&active.guidProfile==ProfileId;
    Print("After message loop: profile matches %s; service callback observed %s\n",matched?"yes":"no",callback?"yes":"no");
    auto restored=manager->ActivateProfile(previous.dwProfileType,previous.langid,previous.clsid,previous.guidProfile,previous.hkl,scope);
    UnmapViewOfFile(shared);CloseHandle(mapping);
    thread->SetFocus(nullptr);hostDocument->Pop(TF_POPF_ALL);hostContext.Reset();hostDocument.Reset();
    DestroyWindow(fixture);thread->Deactivate();Require(restored==S_OK,"restore previous local profile");Require(hr==S_OK&&matched&&callback&&keyCallback&&compositionVerified&&physicalVerified,"System activation, key dispatch or Korean document composition not verified");Line(interactive?"Physical typing / shortcut / typing transition verified in isolated TSF fixture.":"Process-local activation, system key dispatch and Korean document composition verified (not physical input).");
}
static void RegistrationTest(){
    Registration registration;registration.Install();
    auto fixture=DllPath();fixture.resize(fixture.find_last_of(L'\\')+1);fixture+=L"RiumContextFixture.exe";
    std::wstring command=L"\""+fixture+L"\" --registered-fixture";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    // Registration needs elevation; the input host must model ordinary apps.
    // Use only this process's own linked standard-user token, never another user.
    HANDLE ownToken=nullptr;TOKEN_LINKED_TOKEN linked{};DWORD tokenSize=0;
    Require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&ownToken)!=FALSE,"open own registration token");
    auto linkedRead=GetTokenInformation(ownToken,TokenLinkedToken,&linked,sizeof(linked),&tokenSize);CloseHandle(ownToken);
    Require(linkedRead!=FALSE,"read linked standard-user token");
    TOKEN_ELEVATION elevation{};
    auto elevationRead=GetTokenInformation(linked.LinkedToken,TokenElevation,&elevation,sizeof(elevation),&tokenSize);
    if(!elevationRead||elevation.TokenIsElevated){CloseHandle(linked.LinkedToken);throw std::runtime_error("Fixture requires a non-elevated linked token");}
    PROCESS_INFORMATION child{};auto created=CreateProcessWithTokenW(linked.LinkedToken,LOGON_WITH_PROFILE,fixture.c_str(),command.data(),0,nullptr,nullptr,&startup,&child);
    auto createError=created?ERROR_SUCCESS:GetLastError();CloseHandle(linked.LinkedToken);
    Print("Standard-user fixture launch Win32 result: %lu\n",createError);Require(created!=FALSE,"start standard-user registered fixture");
    DumpRegisteredTip();
    DWORD waited=WAIT_TIMEOUT;auto deadline=GetTickCount64()+10000;
    while(GetTickCount64()<deadline){auto signal=MsgWaitForMultipleObjectsEx(1,&child.hProcess,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);if(signal==WAIT_OBJECT_0){waited=signal;break;}MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
    DWORD result=1;
    if(waited!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,2);WaitForSingleObject(child.hProcess,5000);}
    else GetExitCodeProcess(child.hProcess,&result);
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
    Require(registration.Cleanup(),"temporary registration cleanup failed");Print("Fresh fixture exit code: %lu; temporary registration removed\n",result);
    Require(waited==WAIT_OBJECT_0&&result==0,"Fresh fixture failed; see fixture-result.log");
}
static void ExternalRegistrationTest(){
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\RIUM.Keys.Probe.Ready");
    HANDLE done=OpenEventW(SYNCHRONIZE,FALSE,L"Local\\RIUM.Keys.Probe.Done");
    if(!ready||!done){if(ready)CloseHandle(ready);if(done)CloseHandle(done);throw std::runtime_error("Start using the unelevated registration-test script");}
    try{
        Registration registration;registration.Install();SetEvent(ready);
        DWORD waited=WAIT_TIMEOUT;auto deadline=GetTickCount64()+
#ifdef RIUM_REUSED_ENGINE
        240000;
#else
        105000;
#endif
        while(GetTickCount64()<deadline){auto signal=MsgWaitForMultipleObjectsEx(1,&done,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);if(signal==WAIT_OBJECT_0){waited=signal;break;}MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
        Require(registration.Cleanup(),"temporary registration cleanup failed");Line("Temporary registration removed.");Require(waited==WAIT_OBJECT_0,"External fixture timed out");
    }catch(...){CloseHandle(ready);CloseHandle(done);throw;}
    CloseHandle(ready);CloseHandle(done);
}
#ifdef RIUM_REUSED_ENGINE
#include "../native-ime/Fixture.h"
#endif
int wmain(int argc,wchar_t** argv){
    if(argc==2&&(wcscmp(argv[1],L"--registration-test")==0||wcscmp(argv[1],L"--registered-fixture")==0||wcscmp(argv[1],L"--interactive-fixture")==0||wcscmp(argv[1],L"--native-fixture")==0||wcscmp(argv[1],L"--registration-external-test")==0)){
        auto log=DllPath();log.resize(log.find_last_of(L'\\')+1);log+=(wcscmp(argv[1],L"--registered-fixture")==0||wcscmp(argv[1],L"--interactive-fixture")==0||wcscmp(argv[1],L"--native-fixture")==0)?L"fixture-result.log":L"registration-result.log";
        diagnosticOutput=_wfsopen(log.c_str(),L"w",_SH_DENYWR);if(!diagnosticOutput)return 3;
    }
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 2;int result=0;
#ifdef RIUM_REUSED_ENGINE
    if(argc==2&&wcscmp(argv[1],L"--native-fixture")==0){
        try{ReusedEngineSession();}catch(const std::exception& e){Print("%s\n",e.what());result=1;}
        CoUninitialize();return result;
    }
#endif
    try{if(argc==2&&wcscmp(argv[1],L"--self-test")==0)SelfTest();else if(argc==2&&wcscmp(argv[1],L"--registration-test")==0)RegistrationTest();else if(argc==2&&wcscmp(argv[1],L"--registration-external-test")==0)ExternalRegistrationTest();else if(argc==2&&wcscmp(argv[1],L"--registered-fixture")==0)Session(40);else if(argc==2&&wcscmp(argv[1],L"--interactive-fixture")==0)Session(90,true);else if(argc==2&&wcscmp(argv[1],L"--native-fixture")==0)Session(90,true,true);else if(argc==2&&wcscmp(argv[1],L"--cleanup-registration")==0)RecoverRegistration();else throw std::runtime_error("Use --self-test, --registration-test (isolated process only), or --cleanup-registration");}
    catch(const std::exception& e){Print("%s\n",e.what());result=1;}CoUninitialize();return result;
}
