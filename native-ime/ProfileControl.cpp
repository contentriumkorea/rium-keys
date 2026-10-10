// Installation/profile control only. Never hooks keys or polls user applications.
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static const CLSID Service = {0xe1985813,0x4fa4,0x4b93,{0x8e,0xf4,0xf8,0xee,0x77,0x77,0xe2,0x91}};
static const GUID Profile = {0xea007e57,0x6806,0x4596,{0xbb,0x29,0x88,0xeb,0xfb,0xc6,0x20,0xb5}};
static const GUID Categories[] = {
    GUID_TFCAT_TIP_KEYBOARD, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    {0x13a016df,0x560b,0x46cd,{0x94,0x7a,0x4c,0x3a,0xf1,0xe0,0xe3,0x5d}},
    {0x25504fb4,0x7bab,0x4bc1,{0x9c,0x69,0xcf,0x81,0x89,0x0f,0x0e,0xf5}},
    {0xccf05dd7,0x4a87,0x11d7,{0xa6,0xe2,0x00,0x06,0x5b,0x84,0x43,0x5c}},
    {0x49d2f9cf,0x1f5e,0x11d7,{0xa6,0xd3,0x00,0x06,0x5b,0x84,0x43,0x5c}}
};
static void Check(HRESULT hr,const char* what) {
    if(hr!=S_OK){fprintf(stderr,"%s: 0x%08lX\n",what,static_cast<unsigned long>(hr));throw std::runtime_error(what);}
}
static std::wstring Guid(REFGUID guid){wchar_t out[40]{};StringFromGUID2(guid,out,40);return out;}
static std::wstring Tip(LANGID lang,REFCLSID cls,REFGUID profile){
    wchar_t prefix[16];swprintf_s(prefix,L"0x%04X:",lang);return prefix+Guid(cls)+Guid(profile);
}
static void InputApi(const char* name,const std::wstring& tip,DWORD flags){
    HMODULE lib=LoadLibraryExW(L"input.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!lib)throw std::runtime_error("input.dll load failed");
    using Fn=BOOL(CALLBACK*)(LPCWSTR,DWORD);
    auto fn=reinterpret_cast<Fn>(GetProcAddress(lib,name));
    BOOL ok=fn&&fn(tip.c_str(),flags);FreeLibrary(lib);
    if(!ok)throw std::runtime_error(name);
}
static void Refresh(){
    HMODULE lib=LoadLibraryExW(L"msctf.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!lib)return;
    using Fn=HRESULT(WINAPI*)();auto fn=reinterpret_cast<Fn>(GetProcAddress(lib,"TF_InvalidAssemblyListCacheIfExist"));
    if(fn)fn();FreeLibrary(lib);
}
static int CategoryCount(ITfCategoryMgr* categories){
    ComPtr<IEnumGUID> list;Check(categories->EnumCategoriesInItem(Service,&list),"enumerate categories");
    int count=0;GUID item;ULONG fetched;
    while(list->Next(1,&item,&fetched)==S_OK)for(auto& expected:Categories)if(item==expected)++count;
    return count;
}
static LANGID Parse(const std::wstring& tip,CLSID& cls,GUID& profile){
    if(tip.size()!=83||tip.substr(0,2)!=L"0x"||tip[6]!=L':')throw std::runtime_error("Expected a TIP identity");
    for(size_t i=2;i<6;++i)if(!iswxdigit(tip[i]))throw std::runtime_error("Invalid language identity");
    auto lang=static_cast<LANGID>(wcstoul(tip.substr(2,4).c_str(),nullptr,16));
    Check(CLSIDFromString(tip.substr(7,38).c_str(),&cls),"parse service");
    Check(CLSIDFromString(tip.substr(45,38).c_str(),&profile),"parse profile");return lang;
}
static void Activate(ITfInputProcessorProfileMgr* profiles,const std::wstring& tip){
    CLSID cls;GUID profile;
    auto lang=Parse(tip,cls,profile);
    Check(profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,lang,cls,profile,nullptr,
        TF_IPPMF_FORSESSION|TF_IPPMF_ENABLEPROFILE|TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE),"select profile");
    TF_INPUTPROCESSORPROFILE actual{};
    Check(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&actual),"read active profile");
    if(actual.clsid!=cls||actual.guidProfile!=profile)throw std::runtime_error("Active profile readback mismatch");
}
static std::wstring DefaultTip(ITfInputProcessorProfiles* profiles){
    HKL layout=nullptr;CLSID cls;GUID profile;
    if(!SystemParametersInfoW(SPI_GETDEFAULTINPUTLANG,0,&layout,0))throw std::runtime_error("Read default input language failed");
    LANGID lang=LOWORD(reinterpret_cast<ULONG_PTR>(layout));
    // Refuse a configuration whose fallback cannot be captured faithfully.
    Check(profiles->GetDefaultLanguageProfile(lang,GUID_TFCAT_TIP_KEYBOARD,&cls,&profile),"read actual default input profile");
    if(cls==GUID_NULL||profile==GUID_NULL)throw std::runtime_error("Select a Korean input method as the default before installation");
    return Tip(lang,cls,profile);
}
static void VerifyInstalledLoad(const wchar_t* expectedDll){
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot read verification token");
    TOKEN_ELEVATION elevation{};DWORD size=0;
    BOOL read=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size);
    CloseHandle(token);
    if(!read||elevation.TokenIsElevated)throw std::runtime_error("Load verification requires an ordinary user token");
    const wchar_t* key=L"Software\\Classes\\CLSID\\{E1985813-4FA4-4B93-8EF4-F8EE7777E291}\\InprocServer32";
    wchar_t path[32768]{};DWORD bytes=sizeof(path);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,key,nullptr,RRF_RT_REG_SZ,nullptr,path,&bytes)!=ERROR_SUCCESS ||
       _wcsicmp(path,expectedDll)!=0)throw std::runtime_error("Registered DLL path does not match the installed version");
    wchar_t model[32]{};bytes=sizeof(model);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ThreadingModel",RRF_RT_REG_SZ,nullptr,model,&bytes)!=ERROR_SUCCESS ||
       wcscmp(model,L"Apartment")!=0)throw std::runtime_error("Unexpected COM threading model");
    // COM resolves the live registration (including any per-user override).
    // Do not activate a profile, attach a key sink or create a document/window.
    ComPtr<ITfTextInputProcessorEx> service;
    Check(CoCreateInstance(Service,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&service)),"load installed text service");
    ComPtr<ITfKeyEventSink> keySink;Check(service.As(&keySink),"input key interface");
    ComPtr<ITfDisplayAttributeProvider> attributes;Check(service.As(&attributes),"inline composition interface");
    HMODULE module=nullptr;
    const void* vtable=*reinterpret_cast<const void* const*>(service.Get());
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(vtable),&module))throw std::runtime_error("Cannot identify loaded text service module");
    DWORD length=GetModuleFileNameW(module,path,static_cast<DWORD>(_countof(path)));
    if(!length||length>=_countof(path)||_wcsicmp(path,expectedDll)!=0)throw std::runtime_error("COM loaded a different DLL than the installed version");
    wprintf(L"PASS: registered %u-bit text service loaded from %ls; required COM interfaces available.\n",
            static_cast<unsigned>(sizeof(void*)*8),path);
}
int wmain(int argc,wchar_t** argv){
    HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(co))return 1;
    int result=0;
    try{
        ComPtr<ITfInputProcessorProfiles> legacy;
        Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&legacy)),"profiles");
        ComPtr<ITfInputProcessorProfileMgr> profiles;Check(legacy.As(&profiles),"modern profiles");
        ComPtr<ITfCategoryMgr> categories;
        Check(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&categories)),"categories");
        std::wstring mode=argc>1?argv[1]:L"--status";
        if(mode==L"--verify-install"&&argc==3){
            TF_INPUTPROCESSORPROFILE native{};BOOL enabled=FALSE;
            Check(profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,Service,Profile,nullptr,&native),"installed profile");
            Check(legacy->IsEnabledLanguageProfile(Service,0x412,Profile,&enabled),"installed user profile");
            if(!enabled||CategoryCount(categories.Get())!=6)throw std::runtime_error("Installed profile is disabled or incomplete");
            VerifyInstalledLoad(argv[2]);
        }else if(mode==L"--register"&&argc==3){
            TF_INPUTPROCESSORPROFILE existing{};
            if(profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,Service,Profile,nullptr,&existing)==S_OK)
                throw std::runtime_error("Profile already exists; refusing replacement");
            std::wstring dll=argv[2];
            static const wchar_t name[]=L"CONTENTRIUM Keys";
            Check(profiles->RegisterProfile(Service,0x412,Profile,name,static_cast<ULONG>(wcslen(name)),dll.c_str(),static_cast<ULONG>(dll.size()),
                static_cast<ULONG>(-100),nullptr,0,TRUE,0),"register profile");
            for(auto& category:Categories)Check(categories->RegisterCategory(Service,category,Service),"register category");
            if(CategoryCount(categories.Get())!=6)throw std::runtime_error("Registered categories incomplete");
            Refresh();puts("Registered CONTENTRIUM Keys profile and six categories.");
        }else if(mode==L"--refresh"){
            Refresh();BSTR description=nullptr;
            Check(legacy->GetLanguageProfileDescription(Service,0x412,Profile,&description),"read input method name");
            bool matches=description&&wcscmp(description,L"CONTENTRIUM Keys")==0;
            printf("Profile description: %ls\n",description?description:L"");SysFreeString(description);
            if(!matches)throw std::runtime_error("Input method name readback mismatch");
        }else if(mode==L"--unregister"){
            HRESULT first=S_OK;
            for(auto& category:Categories){auto hr=categories->UnregisterCategory(Service,category,Service);if(FAILED(hr)&&SUCCEEDED(first))first=hr;}
            auto hr=legacy->Unregister(Service);if(FAILED(hr)&&SUCCEEDED(first))first=hr;
            Refresh();
            TF_INPUTPROCESSORPROFILE remaining{};
            if(profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,Service,Profile,nullptr,&remaining)==S_OK||CategoryCount(categories.Get())!=0)
                throw std::runtime_error("Profile/category cleanup incomplete");
            if(FAILED(first))fprintf(stderr,"Cleanup API reported 0x%08lX; absence verified.\n",static_cast<unsigned long>(first));
            puts("Profile and categories absent.");
        }else if(mode==L"--enable"){
            InputApi("InstallLayoutOrTip",Tip(0x412,Service,Profile),0);
            Check(legacy->EnableLanguageProfile(Service,0x412,Profile,TRUE),"enable user profile");Refresh();
            BOOL enabled=FALSE;Check(legacy->IsEnabledLanguageProfile(Service,0x412,Profile,&enabled),"read user profile");
            if(!enabled)throw std::runtime_error("User profile is disabled");puts("User profile enabled.");
        }else if(mode==L"--disable"){
            InputApi("InstallLayoutOrTip",Tip(0x412,Service,Profile),1);puts("User profile disabled.");
        }else if(mode==L"--select"){
            auto requested=argc==3?std::wstring(argv[2]):Tip(0x412,Service,Profile);
            CLSID cls;GUID profile;Parse(requested,cls,profile); // validate before mutation
            InputApi("SetDefaultLayoutOrTip",requested,0);
            Activate(profiles.Get(),requested);
            if(DefaultTip(legacy.Get())!=requested)throw std::runtime_error("Default input readback mismatch");
            puts("Default and current profile selected.");
        }else if(mode==L"--restore"&&argc==4){
            CLSID cls;GUID profile;Parse(argv[2],cls,profile);Parse(argv[3],cls,profile);
            InputApi("SetDefaultLayoutOrTip",argv[2],0); // restore default before the separately captured active profile
            Activate(profiles.Get(),argv[3]);
            if(DefaultTip(legacy.Get())!=argv[2])throw std::runtime_error("Restored default readback mismatch");
            puts("Previous default and active profiles restored and verified.");
        }else if(mode==L"--status"){
            TF_INPUTPROCESSORPROFILE native{},active{};CLSID defClass{};GUID defProfile{};
            bool registered=profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,Service,Profile,nullptr,&native)==S_OK;
            auto activeHr=profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);
            Check(activeHr,"capture active profile");
            if(active.dwProfileType!=TF_PROFILETYPE_INPUTPROCESSOR)throw std::runtime_error("Select a Korean input method before installation");
            auto defaultTip=DefaultTip(legacy.Get());
            Check(legacy->GetDefaultLanguageProfile(0x412,GUID_TFCAT_TIP_KEYBOARD,&defClass,&defProfile),"read Korean default");
            BOOL enabled=FALSE;legacy->IsEnabledLanguageProfile(Service,0x412,Profile,&enabled);
            wprintf(L"{\"registered\":%ls,\"enabled\":%ls,\"categories\":%d,\"active\":%ls,\"koreanDefault\":\"%ls\",\"defaultTip\":\"%ls\",\"activeTip\":\"%ls\"}\n",
                registered?L"true":L"false",enabled?L"true":L"false",CategoryCount(categories.Get()),
                activeHr==S_OK&&active.clsid==Service&&active.guidProfile==Profile?L"true":L"false",Tip(0x412,defClass,defProfile).c_str(),defaultTip.c_str(),Tip(active.langid,active.clsid,active.guidProfile).c_str());
        }else throw std::runtime_error("Unknown mode or invalid arguments");
    }catch(const std::exception& ex){fprintf(stderr,"CONTENTRIUM Keys: %s\n",ex.what());result=1;}
    CoUninitialize();return result;
}
