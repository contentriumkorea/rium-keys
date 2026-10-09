#include "Probe.h"
#include <cstdio>
#include <string>
#include <stdexcept>
#include <cstring>
using Entry=HRESULT(__stdcall*)(REFCLSID,REFIID,void**);
static void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Check(HRESULT hr,const char* why){if(FAILED(hr)){printf("HRESULT 0x%08lx: %s\n",static_cast<unsigned long>(hr),why);throw std::runtime_error(why);}}
static std::wstring DllPath(){wchar_t path[MAX_PATH];Require(GetModuleFileNameW(nullptr,path,MAX_PATH)!=0,"module path");std::wstring out=path;return out.substr(0,out.find_last_of(L'\\')+1)+L"RiumContextProbe.dll";}
static void SelfTest(){
    ComPtr<ITfThreadMgr> manager;Check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager)),"thread manager");
    auto missing=ReadValue(manager.Get(),ProfileId);Require(SUCCEEDED(missing.result)&&missing.type==VT_EMPTY,"Missing is not false");
    ComPtr<ITfCompartmentMgr> compartments;Check(manager.As(&compartments),"compartment manager");ComPtr<ITfCompartment> compartment;Check(compartments->GetCompartment(ProfileId,&compartment),"compartment");
    TfClientId client;Check(manager->Activate(&client),"activate fixture");
    VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=1;Check(compartment->SetValue(client,&value),"set disabled");Require(ReadValue(manager.Get(),ProfileId).value==1,"nonzero preserved");
    value.lVal=0;Check(compartment->SetValue(client,&value),"set zero");auto zero=ReadValue(manager.Get(),ProfileId);Require(zero.type==VT_I4&&zero.value==0,"explicit zero distinct from missing");
    Require(FAILED(ReadValue(nullptr,ProfileId).result),"missing owner is unknown");
    HMODULE dll=LoadLibraryW(DllPath().c_str());Require(dll!=nullptr,"load diagnostic DLL");
    auto entry=reinterpret_cast<Entry>(GetProcAddress(dll,"DllGetClassObject"));Require(entry!=nullptr,"DLL factory export");
    ComPtr<IClassFactory> factory;Check(entry(ServiceId,IID_PPV_ARGS(&factory)),"factory");
    ComPtr<ITfTextInputProcessorEx> service;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&service)),"service");
    // Manual activation is not a system-activation test. Record its result only;
    // actual TSF activation is verified separately by the process-local fixture.
    auto activation=service->ActivateEx(manager.Get(),client,0);printf("Manual diagnostic activation HRESULT: 0x%08lx\n",static_cast<unsigned long>(activation));
    ComPtr<ITfKeyEventSink> sink;Check(service.As(&sink),"key event sink");
    BOOL eaten=TRUE;Check(sink->OnTestKeyDown(nullptr,'A',0,&eaten),"test key");Require(!eaten,"test callback must never consume input");
    eaten=TRUE;Check(sink->OnKeyDown(nullptr,'A',0,&eaten),"key down");Require(!eaten,"key down must never consume input");
    eaten=TRUE;Check(sink->OnKeyUp(nullptr,'A',0,&eaten),"key up");Require(!eaten,"key up must never consume input");
    Check(service->Deactivate(),"deactivate");Check(service->Deactivate(),"idempotent deactivate");sink.Reset();service.Reset();factory.Reset();FreeLibrary(dll);compartments->ClearCompartment(client,ProfileId);manager->Deactivate();
    puts("PASS: TSF missing/zero/nonzero/error states; COM lifecycle; diagnostic key pass-through.");
}
static std::wstring ClassPath(){wchar_t guid[40];StringFromGUID2(ServiceId,guid,40);return std::wstring(L"Software\\Classes\\CLSID\\")+guid;}
static bool CategoryExists(ITfCategoryMgr* manager){
    ComPtr<IEnumGUID> list;Check(manager->EnumCategoriesInItem(ServiceId,&list),"enumerate diagnostic categories");GUID id;ULONG count=0;
    while(list->Next(1,&id,&count)==S_OK)if(id==GUID_TFCAT_TIP_KEYBOARD)return true;return false;
}
static void RecoverRegistration(){
    // Recovery is intentionally restricted to this diagnostic CLSID and the DLL
    // beside this controller. It never removes an arbitrary input method.
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,MappingName);if(mapping){CloseHandle(mapping);throw std::runtime_error("Diagnostic still running; recovery refused");}
    Require(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot establish that diagnostic is stopped");
    HKEY key;auto opened=RegOpenKeyExW(HKEY_CURRENT_USER,(ClassPath()+L"\\InprocServer32").c_str(),0,KEY_READ,&key);bool exists=opened==ERROR_SUCCESS;
    Require(exists||opened==ERROR_FILE_NOT_FOUND,"Cannot inspect diagnostic COM entry");
    if(exists){wchar_t path[MAX_PATH]{};DWORD type=0,size=sizeof(path);auto read=RegQueryValueExW(key,nullptr,nullptr,&type,reinterpret_cast<BYTE*>(path),&size);RegCloseKey(key);Require(read==ERROR_SUCCESS&&type==REG_SZ&&size>=sizeof(wchar_t)&&size<=sizeof(path)&&path[size/sizeof(wchar_t)-1]==0&&_wcsicmp(path,DllPath().c_str())==0,"COM entry belongs to a different location; recovery refused");}
    ComPtr<ITfCategoryMgr> manager;Check(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager)),"recovery category manager");
    if(CategoryExists(manager.Get()))Check(manager->UnregisterCategory(ServiceId,GUID_TFCAT_TIP_KEYBOARD,ServiceId),"recover diagnostic category");
    Require(!CategoryExists(manager.Get()),"diagnostic category remains");
    if(exists)Require(RegDeleteTreeW(HKEY_CURRENT_USER,ClassPath().c_str())==ERROR_SUCCESS,"recover diagnostic COM entry");
    HKEY remaining;auto result=RegOpenKeyExW(HKEY_CURRENT_USER,ClassPath().c_str(),0,KEY_READ,&remaining);if(result==ERROR_SUCCESS)RegCloseKey(remaining);Require(result==ERROR_FILE_NOT_FOUND,"diagnostic COM entry remains");
    puts("Verified: diagnostic category and per-user COM registration absent.");
}
struct Registration{
    bool com=false,localProfile=false,category=false;
    ComPtr<ITfInputProcessorProfiles> profiles;ComPtr<ITfCategoryMgr> categories;
    Registration(){Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles)),"profile manager");Check(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&categories)),"category manager");}
    void Install(){
        HKEY existing;if(RegOpenKeyExW(HKEY_CURRENT_USER,ClassPath().c_str(),0,KEY_READ,&existing)==ERROR_SUCCESS){RegCloseKey(existing);throw std::runtime_error("Diagnostic class already exists; inspect before replacing");}
        HKEY key;DWORD disposition;auto result=RegCreateKeyExW(HKEY_CURRENT_USER,(ClassPath()+L"\\InprocServer32").c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key,&disposition);
        Require(result==ERROR_SUCCESS,"create per-user diagnostic class");if(disposition!=REG_CREATED_NEW_KEY){RegCloseKey(key);throw std::runtime_error("Diagnostic registration appeared concurrently; left untouched");}com=true;auto dll=DllPath();auto pathResult=RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(dll.c_str()),static_cast<DWORD>((dll.size()+1)*sizeof(wchar_t)));
        const wchar_t threading[]=L"Apartment";auto modelResult=RegSetValueExW(key,L"ThreadingModel",0,REG_SZ,reinterpret_cast<const BYTE*>(threading),sizeof(threading));RegCloseKey(key);Require(pathResult==ERROR_SUCCESS&&modelResult==ERROR_SUCCESS,"class registration values");
        const wchar_t title[]=L"RIUM Context Probe (diagnostic)";
        auto categoryHr=categories->RegisterCategory(ServiceId,GUID_TFCAT_TIP_KEYBOARD,ServiceId);printf("Keyboard category registration HRESULT: 0x%08lx\n",static_cast<unsigned long>(categoryHr));category=categoryHr==S_OK;
        Require(category,"Keyboard-category registration failed; no input profile was selected");
        ComPtr<ITfInputProcessorProfileMgr> manager;Check(profiles.As(&manager),"local profile manager");Check(manager->RegisterProfile(ServiceId,0x412,ProfileId,title,static_cast<ULONG>(wcslen(title)),dll.c_str(),static_cast<ULONG>(dll.size()),0,nullptr,0,TRUE,TF_RP_LOCALPROCESS|TF_RP_HIDDENINSETTINGUI),"register process-local diagnostic TIP");localProfile=true;
    }
    bool Cleanup(){
        bool ok=true;
        if(localProfile){ComPtr<ITfInputProcessorProfileMgr> manager;auto hr=profiles.As(&manager);if(SUCCEEDED(hr))hr=manager->UnregisterProfile(ServiceId,0x412,ProfileId,TF_URP_LOCALPROCESS);if(hr==S_OK)localProfile=false;else{printf("Profile cleanup HRESULT: 0x%08lx\n",static_cast<unsigned long>(hr));ok=false;}}
        if(category){auto hr=categories->UnregisterCategory(ServiceId,GUID_TFCAT_TIP_KEYBOARD,ServiceId);if(hr==S_OK)category=false;else{printf("Category cleanup HRESULT: 0x%08lx\n",static_cast<unsigned long>(hr));ok=false;}}
        if(com){auto result=RegDeleteTreeW(HKEY_CURRENT_USER,ClassPath().c_str());if(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND)com=false;else{printf("COM cleanup Win32 error: %ld\n",result);ok=false;}}
        return ok;
    }
    ~Registration(){Cleanup();}
};
static void PrintValue(const char* name,const Value& value){printf("\"%s\":{\"hr\":%ld,\"type\":%u,\"value\":%ld}",name,static_cast<long>(value.result),value.type,value.value);}
static void PrintSample(const Sample& s){
    printf("{\"pid\":%lu,\"tid\":%lu,\"event\":%lu,\"tick\":%llu,\"document\":%llu,\"context\":%llu,\"hwnd\":%llu,\"statusHr\":%ld,\"dynamic\":%lu,\"static\":%lu,",s.pid,s.tid,s.event,s.tick,s.document,s.context,s.window,static_cast<long>(s.statusResult),s.dynamicFlags,s.staticFlags);
    PrintValue("disabled",s.contextDisabled);putchar(',');PrintValue("empty",s.contextEmpty);putchar(',');PrintValue("threadDisabled",s.threadDisabled);putchar(',');PrintValue("threadEmpty",s.threadEmpty);putchar(',');PrintValue("open",s.open);puts("}");fflush(stdout);
}
static void Session(DWORD seconds){
    const DWORD pid=GetCurrentProcessId();
    Require(pid>0&&seconds>0&&seconds<=120,"valid target and 1-120 second duration required");
    ComPtr<ITfThreadMgr> thread;TfClientId client;Check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&thread)),"diagnostic thread manager");Check(thread->Activate(&client),"diagnostic thread activation");
    ComPtr<ITfInputProcessorProfileMgr> manager;Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager)),"profile control");
    TF_INPUTPROCESSORPROFILE previous{};Check(manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&previous),"capture previous input profile");
    Registration registration;registration.Install();
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),MappingName);Require(mapping!=nullptr,"create diagnostic mapping");
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mapping);throw std::runtime_error("Another diagnostic session exists");}
    auto shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(!shared){CloseHandle(mapping);throw std::runtime_error("map diagnostic state");}
    shared->targetPid=pid;
    const DWORD scope=TF_IPPMF_FORPROCESS;
    HRESULT hr=manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,ServiceId,ProfileId,nullptr,scope);
    TF_INPUTPROCESSORPROFILE active{};auto activeHr=manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);
    bool matched=activeHr==S_OK&&active.clsid==ServiceId&&active.guidProfile==ProfileId;
    printf("Activate HRESULT 0x%08lx; active keyboard profile matches diagnostic: %s\n",static_cast<unsigned long>(hr),matched?"yes":"no");
    if(hr==S_OK){
        puts("Process-local activation requested; verifying callback delivery.");fflush(stdout);
        auto end=GetTickCount64()+static_cast<ULONGLONG>(seconds)*1000;LONG last=0;
        while(GetTickCount64()<end){
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            auto sequence=InterlockedCompareExchange(&shared->sequence,0,0);
            if(sequence>0&&sequence!=last&&!(sequence&1)){Sample sample=shared->sample;MemoryBarrier();if(sequence==InterlockedCompareExchange(&shared->sequence,0,0)){PrintSample(sample);last=sequence;}}
            Sleep(10);
        }
    }
    bool callback=shared->sequence>0;
    auto restored=manager->ActivateProfile(previous.dwProfileType,previous.langid,previous.clsid,previous.guidProfile,previous.hkl,scope);
    UnmapViewOfFile(shared);CloseHandle(mapping);
    thread->Deactivate();Require(restored==S_OK,"restore previous local profile");Require(registration.Cleanup(),"temporary registration cleanup failed");Require(hr==S_OK&&matched&&callback,"System activation and diagnostic callback not verified");puts("Process-local profile and activation callback verified; temporary registration removed.");
}
int wmain(int argc,wchar_t** argv){
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(init))return 2;int result=0;
    try{if(argc==2&&wcscmp(argv[1],L"--self-test")==0)SelfTest();else if(argc==2&&wcscmp(argv[1],L"--registration-test")==0)Session(1);else if(argc==2&&wcscmp(argv[1],L"--cleanup-registration")==0)RecoverRegistration();else throw std::runtime_error("Use --self-test, --registration-test (isolated process only), or --cleanup-registration");}
    catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());result=1;}CoUninitialize();return result;
}
