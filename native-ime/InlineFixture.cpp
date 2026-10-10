// Actual Windows TSF against a document owned by this process, never a host adapter.
#include "../input-probe/FixtureStore.h"
#include <bcrypt.h>
#include <imm.h>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

static constexpr GUID ParentMarker={0x8be347f8,0xc7a0,0x11d7,{0xb4,0x08,0x00,0x06,0x5b,0x84,0x43,0x5c}};
static void Require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
static void Check(HRESULT hr,const char* what){if(FAILED(hr)){printf("%s: 0x%08lX\n",what,static_cast<unsigned long>(hr));throw std::runtime_error(what);}}
struct Options { bool selfTest=false,native=false;DWORD staticFlags=TS_SS_NOHIDDENTEXT;const char* mode="baseline"; };
static Options ParseOptions(int argc,wchar_t** argv){
    Options out;bool chosen=false;
    for(int i=1;i<argc;++i){
        if(!wcscmp(argv[i],L"--self-test")){Require(argc==2,"--self-test must be used alone");out.selfTest=true;}
        else if(!wcscmp(argv[i],L"--native")){Require(!chosen,"choose exactly one fixture mode");chosen=true;out.native=true;out.mode="native-observation";}
        else if(!wcscmp(argv[i],L"--chromium")){Require(!chosen,"choose exactly one fixture mode");chosen=true;out.staticFlags=TS_SS_TRANSITORY|TS_SS_NOHIDDENTEXT;out.mode="chromium-flags";}
        else if(!wcscmp(argv[i],L"--baseline")){Require(!chosen,"choose exactly one fixture mode");chosen=true;}
        else if(!wcscmp(argv[i],L"--static-flags")){
            Require(!chosen&&i+1<argc,"--static-flags requires one DWORD");chosen=true;
            auto raw=argv[++i];wchar_t* end=nullptr;errno=0;
            Require(*raw&&*raw!=L'-',"invalid static flags");auto flags=wcstoull(raw,&end,0);
            Require(errno!=ERANGE&&end&&!*end&&flags<=MAXDWORD,"invalid static flags");
            out.staticFlags=static_cast<DWORD>(flags);out.mode="custom-flags";
        }else throw std::runtime_error("usage: [--baseline|--chromium|--static-flags DWORD|--native|--self-test]");
    }
    return out;
}
struct Hash {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<BYTE> object;
    Hash(){
        Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"open SHA256");
        try{DWORD size=0,returned=0;Require(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<BYTE*>(&size),sizeof(size),&returned,0)>=0,"SHA256 object size");object.resize(size);Require(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0,"create SHA256");}
        catch(...){BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;throw;}
    }
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    void Add(const BYTE* data,ULONG size){Require(BCryptHashData(hash,const_cast<BYTE*>(data),size,0)>=0,"hash candidate bytes");}
    std::string Finish(){BYTE digest[32];Require(BCryptFinishHash(hash,digest,sizeof(digest),0)>=0,"finish SHA256");std::string out;constexpr char hex[]="0123456789ABCDEF";for(auto byte:digest){out+=hex[byte>>4];out+=hex[byte&15];}return out;}
};
struct CandidateFile {
    HANDLE file=INVALID_HANDLE_VALUE;std::filesystem::path path;std::string hash;
    explicit CandidateFile(const std::filesystem::path& base):path(base/L"RiumKeysInput.dll"){
        // Keep staged bytes immutable until our TSF resources are cleaned up.
        file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(file!=INVALID_HANDLE_VALUE,"open staged candidate without write/delete sharing");
        try{Hash digest;BYTE buffer[16384];DWORD read=0;do{Require(ReadFile(file,buffer,sizeof(buffer),&read,nullptr)!=FALSE,"read candidate");if(read)digest.Add(buffer,read);}while(read);hash=digest.Finish();std::ifstream input(base/L"candidate.sha256");std::string expected;input>>expected;Require(expected==hash,"staged candidate SHA256 differs from build manifest");}
        catch(...){CloseHandle(file);file=INVALID_HANDLE_VALUE;throw;}
    }
    ~CandidateFile(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    static HMODULE LoadedModule(DWORD& error){SetLastError(ERROR_SUCCESS);auto module=GetModuleHandleW(L"RiumKeysInput.dll");error=module?ERROR_SUCCESS:GetLastError();return module;}
    void VerifyLoaded()const{
        DWORD error=0;auto module=LoadedModule(error);if(!module)printf("MODULE_LOOKUP lastError=%lu\n",error);
        wchar_t loaded[32768]{};auto count=module?GetModuleFileNameW(module,loaded,static_cast<DWORD>(std::size(loaded))):0;
        Require(count&&count<std::size(loaded),"candidate module was not loaded");
        if(!std::filesystem::equivalent(path,std::filesystem::path(loaded))){printf("REJECTED_DLL path=%ls\n",loaded);throw std::runtime_error("TSF loaded a different candidate DLL");}
        printf("LOADED_DLL path=%ls sha256=%s\n",loaded,hash.c_str());fflush(stdout);
    }
};
struct Manifest {
    HANDLE context=INVALID_HANDLE_VALUE;ULONG_PTR cookie=0;
    explicit Manifest(const std::filesystem::path& base){auto path=base/L"inline.manifest";ACTCTXW value{};value.cbSize=sizeof(value);value.lpSource=path.c_str();context=CreateActCtxW(&value);Require(context!=INVALID_HANDLE_VALUE,"create local manifest context");if(!ActivateActCtx(context,&cookie)){ReleaseActCtx(context);context=INVALID_HANDLE_VALUE;throw std::runtime_error("activate local manifest");}}
    ~Manifest(){if(cookie)DeactivateActCtx(0,cookie);if(context!=INVALID_HANDLE_VALUE)ReleaseActCtx(context);}
};
static void Pump(){
    MSG message{};
    // Bound our queue work; a synchronous Windows callback has no hard timeout.
    for(unsigned count=0;count<128&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++count){TranslateMessage(&message);DispatchMessageW(&message);}
    Sleep(5);
}
static LRESULT CALLBACK SurfaceProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_LBUTTONDOWN){SetFocus(hwnd);return 0;} // User click only; never an automatic focus request.
    if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);RECT area{};GetClientRect(hwnd,&area);FillRect(dc,&area,GetSysColorBrush(COLOR_WINDOW));SetBkMode(dc,TRANSPARENT);auto store=reinterpret_cast<FixtureStore*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));const wchar_t* text=store&&!store->text.empty()?store->text.c_str():L"Click here to focus this disposable TSF document";DrawTextW(dc,text,-1,&area,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);EndPaint(hwnd,&paint);return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
static bool SameProfile(const TF_INPUTPROCESSORPROFILE& a,const TF_INPUTPROCESSORPROFILE& b){return a.dwProfileType==b.dwProfileType&&a.langid==b.langid&&a.clsid==b.clsid&&a.guidProfile==b.guidProfile&&a.hkl==b.hkl;}
static LONG CaptureOpenMode(ITfCompartment* open){
    VARIANT value;VariantInit(&value);auto hr=open->GetValue(&value);bool valid=hr==S_OK&&value.vt==VT_I4;LONG mode=valid?value.lVal:0;VariantClear(&value);
    Require(valid,"capture original own-thread mode before profile mutation (requires VT_I4)");return mode;
}
struct Session {
    int& failures;HWND window=nullptr,edit=nullptr;bool activated=false,pushed=false,associated=false,profileTouched=false;
    TfClientId client=0;TF_INPUTPROCESSORPROFILE previous{};LONG previousOpen=0;
    ComPtr<ITfThreadMgrEx> thread;ComPtr<ITfInputProcessorProfileMgr> profiles;ComPtr<ITfDocumentMgr> document,previousAssociation;
    ComPtr<ITfContext> context;ComPtr<FixtureStore> store;ComPtr<ITfKeystrokeMgr> keys;ComPtr<ITfCompartment> open;
    explicit Session(int& value):failures(value){}
    ~Session(){
        // Restore on every C++ failure path, while the thread manager still lives.
        if(activated){auto hr=thread->SetFocus(nullptr);if(FAILED(hr)){++failures;printf("CLEANUP focus=0x%08lX\n",static_cast<unsigned long>(hr));}}
        if(associated&&IsWindow(edit)){ComPtr<ITfDocumentMgr> ignored;auto hr=thread->AssociateFocus(edit,previousAssociation.Get(),&ignored);if(FAILED(hr)){++failures;printf("CLEANUP association=0x%08lX\n",static_cast<unsigned long>(hr));}}
        if(pushed){auto hr=document->Pop(TF_POPF_ALL);if(FAILED(hr)){++failures;printf("CLEANUP pop=0x%08lX\n",static_cast<unsigned long>(hr));}}
        if(profileTouched){
            // Activation can change the mode, including when activation reports failure.
            // Restore the profile first, then the pre-activation mode; verify both last.
            auto profileHr=profiles->ActivateProfile(previous.dwProfileType,previous.langid,previous.clsid,previous.guidProfile,previous.hkl,TF_IPPMF_FORPROCESS);
            VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=previousOpen;auto modeHr=open->SetValue(client,&value);
            TF_INPUTPROCESSORPROFILE actualProfile{};auto profileRead=profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&actualProfile);bool profileMatch=profileRead==S_OK&&SameProfile(previous,actualProfile);
            VARIANT actualMode;VariantInit(&actualMode);auto modeRead=open->GetValue(&actualMode);bool modeMatch=modeRead==S_OK&&actualMode.vt==VT_I4&&actualMode.lVal==previousOpen;VariantClear(&actualMode);
            printf("RESTORE_PROFILE scope=FORPROCESS hr=0x%08lX read=0x%08lX match=%d\n",static_cast<unsigned long>(profileHr),static_cast<unsigned long>(profileRead),profileMatch);
            printf("RESTORE_MODE hr=0x%08lX read=0x%08lX match=%d\n",static_cast<unsigned long>(modeHr),static_cast<unsigned long>(modeRead),modeMatch);
            if(profileHr!=S_OK||!profileMatch)++failures;if(modeHr!=S_OK||!modeMatch)++failures;
        }
        if(IsWindow(edit))SetWindowLongPtrW(edit,GWLP_USERDATA,0);
        keys.Reset();context.Reset();document.Reset();previousAssociation.Reset();store.Reset();open.Reset();profiles.Reset();
        if(activated){auto hr=thread->Deactivate();printf("DEACTIVATE hr=0x%08lX\n",static_cast<unsigned long>(hr));if(FAILED(hr))++failures;}
        thread.Reset();if(IsWindow(window))DestroyWindow(window);fflush(stdout);
    }
    bool Focused()const{return IsWindow(window)&&IsWindow(edit)&&GetForegroundWindow()==window&&GetFocus()==edit;}
    bool DocumentFocused()const{
        if(!thread||!document)return false;ComPtr<ITfDocumentMgr> current;if(thread->GetFocus(&current)!=S_OK||!current)return false;
        ComPtr<IUnknown> expectedIdentity,currentIdentity;return document.As(&expectedIdentity)==S_OK&&current.As(&currentIdentity)==S_OK&&expectedIdentity.Get()==currentIdentity.Get();
    }
};
static void TraceActivation(Session& session,const char* phase){
    CLSID foreground{};auto foregroundHr=session.keys->GetForeground(&foreground);wchar_t id[40]{};StringFromGUID2(foreground,id,40);
    DWORD flags=0;auto flagsHr=session.thread->GetActiveFlags(&flags);DWORD moduleError=0;auto module=CandidateFile::LoadedModule(moduleError);
    printf("TSF_ACTIVATION phase=%s foregroundHr=0x%08lX foregroundClsid=%ls candidateForeground=%d hkl=%p threadFlagsHr=0x%08lX threadFlags=0x%08lX candidateModule=%d moduleError=%lu\n",phase,static_cast<unsigned long>(foregroundHr),id,foregroundHr==S_OK&&foreground==ServiceId,GetKeyboardLayout(0),static_cast<unsigned long>(flagsHr),flags,module!=nullptr,moduleError);fflush(stdout);
}
static void WaitForCandidate(Session& session,const CandidateFile& candidate,bool native){
    auto start=GetTickCount64();DWORD moduleError=ERROR_SUCCESS;DWORD flags=0;auto flagsHr=session.thread->GetActiveFlags(&flags);
    TraceActivation(session,"before-module-wait");
    printf("WAIT_MODULE timeoutMs=2000 documentAttached=%d documentFocused=%d threadFlagsHr=0x%08lX threadFlags=0x%08lX\n",session.pushed,session.DocumentFocused(),static_cast<unsigned long>(flagsHr),flags);fflush(stdout);
    for(;;){
        Require(session.Focused(),"focus changed while waiting for candidate activation");
        Require(native||session.DocumentFocused(),"own TSF document lost focus before first key");
        if(CandidateFile::LoadedModule(moduleError)){candidate.VerifyLoaded();printf("MODULE_READY elapsedMs=%llu\n",GetTickCount64()-start);return;}
        if(GetTickCount64()-start>=2000){TraceActivation(session,"module-timeout");printf("MODULE_TIMEOUT lastError=%lu elapsedMs=%llu\n",moduleError,GetTickCount64()-start);candidate.VerifyLoaded();return;}
        Pump();
    }
}
struct Snapshot { bool ran=false;ULONG fetched=0,compositions=0;LONG start=-1,length=-1;TF_SELECTIONSTYLE style{}; };
class ReadSelection final:public ITfEditSession {
    LONG refs=1;ComPtr<ITfContext> context;
public:
    Snapshot result;
    explicit ReadSelection(ITfContext* value):context(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_ITfEditSession)return E_NOINTERFACE;*out=static_cast<ITfEditSession*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie)override{
        result.ran=true;TF_SELECTION selection{};auto hr=context->GetSelection(cookie,TF_DEFAULT_SELECTION,1,&selection,&result.fetched);
        ComPtr<ITfRange> range;range.Attach(selection.range);if(hr!=S_OK||result.fetched!=1||!range)return E_FAIL;
        result.style=selection.style;ComPtr<ITfRangeACP> acp;if(FAILED(hr=range.As(&acp)))return hr;if(FAILED(hr=acp->GetExtent(&result.start,&result.length)))return hr;
        ComPtr<ITfContextComposition> compositions;if(FAILED(hr=context.As(&compositions)))return hr;
        ComPtr<IEnumITfCompositionView> items;if(FAILED(hr=compositions->EnumCompositions(&items))||!items)return E_FAIL;
        for(unsigned i=0;i<2;++i){ComPtr<ITfCompositionView> item;ULONG fetched=0;hr=items->Next(1,&item,&fetched);if(hr==S_FALSE&&fetched==0)return S_OK;if(hr!=S_OK||fetched!=1||!item)return E_FAIL;++result.compositions;}
        return E_UNEXPECTED;
    }
};
static bool SelectionMatches(const Snapshot& value,LONG end,bool composing){return value.ran&&value.fetched==1&&value.start==end&&value.length==0&&!value.style.fInterimChar&&value.style.ase==TF_AE_END&&value.compositions==(composing?1UL:0UL);}
static void AssertState(Session& session,const wchar_t* expected,bool composing,const char* label){
    Require(session.Focused()&&session.DocumentFocused(),"fixture lost window or TSF document focus; no further key calls");
    ComPtr<ReadSelection> read;read.Attach(new ReadSelection(session.context.Get()));HRESULT inside=E_PENDING;
    auto requested=session.context->RequestEditSession(session.client,read.Get(),TF_ES_SYNC|TF_ES_READ,&inside);
    Require(requested==S_OK&&inside==S_OK,"actual TSF selection read session did not complete");Require(session.Focused()&&session.DocumentFocused(),"fixture lost window or TSF document focus during read session");
    const auto& value=read->result;auto acp=session.store->SelectionSnapshotForFixture();auto end=static_cast<LONG>(wcslen(expected));
    bool passed=session.store->text==expected&&SelectionMatches(value,end,composing)&&acp.acpStart==end&&acp.acpEnd==end&&!acp.style.fInterimChar&&acp.style.ase==TS_AE_END;
    printf("%s %s textLength=%zu TSF=[%ld,%ld] interim=%d anchor=%d compositions=%lu ACP=[%ld,%ld] interim=%d\n",passed?"PASS":"FAIL",label,session.store->text.size(),value.start,value.start+value.length,value.style.fInterimChar,value.style.ase,value.compositions,acp.acpStart,acp.acpEnd,acp.style.fInterimChar);fflush(stdout);
    Require(passed,"actual Windows TSF composition/text/selection assertion failed");
}
static bool HasUsableParentValue(HRESULT read,const VARIANT& value){return read==S_OK&&value.vt==VT_UNKNOWN&&value.punkVal!=nullptr;}
enum class ParentReading { Unknown,NoUsableParent,UsableParent };
static ParentReading ClassifyParentValue(HRESULT read,const VARIANT& value){
    if(read==S_FALSE&&value.vt==VT_EMPTY)return ParentReading::NoUsableParent;
    if(read!=S_OK)return ParentReading::Unknown;
    return HasUsableParentValue(read,value)?ParentReading::UsableParent:ParentReading::NoUsableParent;
}
static void AssertNoParent(ITfDocumentMgr* document){
    ComPtr<ITfCompartmentMgr> manager;Check(document->QueryInterface(IID_PPV_ARGS(&manager)),"document compartments");ComPtr<IEnumGUID> items;Check(manager->EnumCompartments(&items),"enumerate document compartments");Require(items!=nullptr,"missing compartment enumerator");
    for(unsigned i=0;i<256;++i){
        GUID id{};ULONG fetched=0;auto hr=items->Next(1,&id,&fetched);if(hr==S_FALSE&&fetched==0){puts("PARENT_MARKER enumerated=0 usable=0 (never created)");return;}Require(hr==S_OK&&fetched==1,"compartment enumeration failed");
        if(id==ParentMarker){
            // GetCompartment is read only here because this GUID was just enumerated.
            // Windows may precreate an empty compartment; presence alone is not a parent.
            ComPtr<ITfCompartment> compartment;auto getHr=manager->GetCompartment(ParentMarker,&compartment);
            VARIANT value;VariantInit(&value);auto readHr=getHr==S_OK&&compartment?compartment->GetValue(&value):E_PENDING;
            auto reading=ClassifyParentValue(readHr,value);bool usable=reading==ParentReading::UsableParent,unknownNonNull=value.vt==VT_UNKNOWN&&value.punkVal!=nullptr;
            printf("PARENT_MARKER enumerated=1 getHr=0x%08lX readHr=0x%08lX variant=0x%04X unknownNonNull=%d usable=%d conclusive=%d\n",static_cast<unsigned long>(getHr),static_cast<unsigned long>(readHr),value.vt,unknownNonNull,usable,reading!=ParentReading::Unknown);fflush(stdout);
            auto clearHr=VariantClear(&value);Require(clearHr==S_OK,"clear parent marker variant failed");
            Require(getHr==S_OK&&compartment!=nullptr,"could not inspect existing parent compartment");
            Require(reading!=ParentReading::Unknown,"parent compartment read failed or returned an unexpected empty-value contract");
            Require(!usable,"custom store has a usable transitory-extension parent; no-parent contract unavailable");
            return;
        }
    }
    throw std::runtime_error("compartment enumeration exceeded its bound");
}
struct KeyboardState {
    int& failures;BYTE previous[256]{};bool changed=false;
    explicit KeyboardState(int& value):failures(value){Require(GetKeyboardState(previous)!=FALSE,"capture thread keyboard state");BYTE neutral[256]{};Require(SetKeyboardState(neutral)!=FALSE,"neutral fixture thread keyboard state");changed=true;}
    ~KeyboardState(){if(changed&&!SetKeyboardState(previous)){++failures;puts("CLEANUP FAILURE: thread keyboard state restoration failed");}}
};
static void Press(Session& session,UINT key){
    Require(session.Focused()&&session.DocumentFocused(),"fixture lost window or TSF document focus before key");KeyboardState keyboard(session.failures);
    LPARAM down=1|(static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16);BOOL tested=FALSE,eaten=FALSE;
    auto testHr=session.keys->TestKeyDown(key,down,&tested);auto downHr=testHr==S_OK&&tested?session.keys->KeyDown(key,down,&eaten):E_PENDING;
    printf("KEY vk=0x%02X testHr=0x%08lX tested=%d downHr=0x%08lX eaten=%d\n",key,static_cast<unsigned long>(testHr),tested,static_cast<unsigned long>(downHr),eaten);fflush(stdout);
    Require(testHr==S_OK&&tested&&downHr==S_OK&&eaten,"expected fixture key was not consumed by the TSF key sink");
    BOOL testUp=FALSE,up=FALSE;LPARAM released=down|static_cast<LPARAM>(0xC0000000u);Check(session.keys->TestKeyUp(key,released,&testUp),"test key up");if(testUp)Check(session.keys->KeyUp(key,released,&up),"key up");
    for(int i=0;i<4;++i)Pump();Require(session.Focused()&&session.DocumentFocused(),"fixture lost window or TSF document focus after key");
}
// In-memory interfaces exercise the actual Session cleanup without activating TSF.
// Profile activation deliberately overwrites mode, as a real TIP may do.
struct RestoreScenario {TF_INPUTPROCESSORPROFILE active{};LONG mode=1;VARTYPE modeType=VT_I4;HRESULT modeRead=S_OK;bool modeWritten=false,earlyRead=false;};
class RestoreMode final:public ITfCompartment {
    LONG refs=1;RestoreScenario& value;
public:
    explicit RestoreMode(RestoreScenario& state):value(state){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_ITfCompartment)return E_NOINTERFACE;*out=static_cast<ITfCompartment*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto count=InterlockedDecrement(&refs);if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE SetValue(TfClientId,const VARIANT* mode)override{if(!mode||mode->vt!=VT_I4)return E_INVALIDARG;value.mode=mode->lVal;value.modeWritten=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetValue(VARIANT* mode)override{if(!mode)return E_POINTER;value.earlyRead|=!value.modeWritten;VariantInit(mode);mode->vt=value.modeType;mode->lVal=value.mode;return value.modeRead;}
};
class RestoreProfiles final:public ITfInputProcessorProfileMgr {
    LONG refs=1;RestoreScenario& value;
public:
    explicit RestoreProfiles(RestoreScenario& state):value(state){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_ITfInputProcessorProfileMgr)return E_NOINTERFACE;*out=static_cast<ITfInputProcessorProfileMgr*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto count=InterlockedDecrement(&refs);if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE ActivateProfile(DWORD type,LANGID lang,REFCLSID cls,REFGUID profile,HKL layout,DWORD flags)override{if(flags!=TF_IPPMF_FORPROCESS)return E_INVALIDARG;value.active.dwProfileType=type;value.active.langid=lang;value.active.clsid=cls;value.active.guidProfile=profile;value.active.hkl=layout;value.mode=1;value.modeWritten=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE DeactivateProfile(DWORD,LANGID,REFCLSID,REFGUID,HKL,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetProfile(DWORD,LANGID,REFCLSID,REFGUID,HKL,TF_INPUTPROCESSORPROFILE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumProfiles(LANGID,IEnumTfInputProcessorProfiles**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReleaseInputProcessor(REFCLSID,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RegisterProfile(REFCLSID,LANGID,REFGUID,const WCHAR*,ULONG,const WCHAR*,ULONG,ULONG,HKL,DWORD,BOOL,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnregisterProfile(REFCLSID,LANGID,REFGUID,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetActiveProfile(REFGUID,TF_INPUTPROCESSORPROFILE* out)override{if(!out)return E_POINTER;value.earlyRead|=!value.modeWritten;*out=value.active;return S_OK;}
};
static int SelfTest(){
    int checks=0,failures=0;auto test=[&](bool passed,const char* name){++checks;if(!passed){++failures;printf("FAIL %s\n",name);}};
    ComPtr<FixtureStore> baseline,chromium,custom;baseline.Attach(new FixtureStore(nullptr));chromium.Attach(new FixtureStore(nullptr,TS_SS_TRANSITORY|TS_SS_NOHIDDENTEXT));custom.Attach(new FixtureStore(nullptr,0));
    TS_STATUS a{},b{},c{};baseline->GetStatus(&a);chromium->GetStatus(&b);custom->GetStatus(&c);
    test(a.dwStaticFlags==TS_SS_NOHIDDENTEXT,"default store semantics preserved");test(b.dwStaticFlags==(TS_SS_TRANSITORY|TS_SS_NOHIDDENTEXT),"Chromium flags are advertised");test(c.dwStaticFlags==0,"custom flags accepted");
    chromium->readOnly=true;chromium->GetStatus(&b);test(b.dwDynamicFlags==TS_SD_READONLY&&b.dwStaticFlags==(TS_SS_TRANSITORY|TS_SS_NOHIDDENTEXT),"dynamic readonly does not alter static flags");
    auto selection=chromium->SelectionSnapshotForFixture();selection.acpEnd=99;auto unchanged=chromium->SelectionSnapshotForFixture();test(selection.acpEnd==99&&unchanged.acpStart==0&&unchanged.acpEnd==0&&!unchanged.style.fInterimChar,"selection snapshot is an independent read-only copy");
    auto parse=[](std::initializer_list<const wchar_t*> words){std::vector<std::wstring> storage;for(auto word:words)storage.emplace_back(word);std::vector<wchar_t*> args;for(auto& word:storage)args.push_back(word.data());return ParseOptions(static_cast<int>(args.size()),args.data());};
    test(parse({L"fixture"}).staticFlags==TS_SS_NOHIDDENTEXT,"default CLI baseline");test(parse({L"fixture",L"--chromium"}).staticFlags==(TS_SS_TRANSITORY|TS_SS_NOHIDDENTEXT),"Chromium CLI");test(parse({L"fixture",L"--static-flags",L"0x4"}).staticFlags==4,"custom CLI");
    auto rejects=[&](std::initializer_list<const wchar_t*> words){try{parse(words);return false;}catch(const std::exception&){return true;}};
    test(rejects({L"fixture",L"--chromium",L"--native"}),"conflicting modes rejected");test(rejects({L"fixture",L"--static-flags"}),"missing flags rejected");test(rejects({L"fixture",L"--static-flags",L"-1"}),"negative flags rejected");test(rejects({L"fixture",L"--static-flags",L"0x100000000"}),"overflow flags rejected");test(rejects({L"fixture",L"--static-flags",L"4x"}),"trailing flags rejected");
    Snapshot good;good.ran=true;good.fetched=1;good.start=1;good.length=0;good.style={TF_AE_END,FALSE};good.compositions=1;
    test(SelectionMatches(good,1,true),"collapsed live composition accepted");auto bad=good;bad.style.fInterimChar=TRUE;test(!SelectionMatches(bad,1,true),"blue interim selection rejected");bad=good;bad.length=1;test(!SelectionMatches(bad,1,true),"noncollapsed selection rejected");bad=good;bad.compositions=0;test(!SelectionMatches(bad,1,true),"silent composition loss rejected");bad=good;bad.ran=false;test(!SelectionMatches(bad,1,true),"unexecuted read callback rejected");good.compositions=0;test(SelectionMatches(good,1,false),"completed composition accepted");
    Hash hash;const BYTE abc[]={'a','b','c'};hash.Add(abc,sizeof(abc));test(hash.Finish()=="BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD","SHA256 known vector");
    puts("SELF_TEST: the following restore lines use in-memory interfaces, not Windows TSF.");
    for(bool explicitModeChange:{false,true}){
        RestoreScenario restore;int cleanupFailures=0;TF_INPUTPROCESSORPROFILE previous{};previous.dwProfileType=TF_PROFILETYPE_INPUTPROCESSOR;previous.langid=0x409;previous.clsid=ServiceId;
        {Session session(cleanupFailures);session.profiles.Attach(new RestoreProfiles(restore));session.open.Attach(new RestoreMode(restore));session.previous=previous;session.previousOpen=0;session.profileTouched=true;if(explicitModeChange){VARIANT changed;VariantInit(&changed);changed.vt=VT_I4;changed.lVal=1;session.open->SetValue(0,&changed);}}
        test(cleanupFailures==0&&SameProfile(restore.active,previous)&&restore.mode==0,explicitModeChange?"restore original mode after previous profile activation":"restore activation side effects before explicit mode change");
        test(!restore.earlyRead,"verify profile and mode only after both restorations");
    }
    RestoreScenario prior;prior.mode=0;ComPtr<ITfCompartment> priorMode;priorMode.Attach(new RestoreMode(prior));test(CaptureOpenMode(priorMode.Get())==0,"snapshot preserves original closed mode");prior.mode=1;test(CaptureOpenMode(priorMode.Get())==1,"snapshot preserves original open mode");
    auto rejectMode=[&](){try{CaptureOpenMode(priorMode.Get());return false;}catch(const std::exception&){return true;}};
    prior.modeType=VT_EMPTY;test(rejectMode(),"absent mode blocks profile mutation");prior.modeType=VT_I4;prior.modeRead=S_FALSE;test(rejectMode(),"non-success mode read blocks profile mutation");prior.modeRead=E_FAIL;test(rejectMode(),"failed mode read blocks profile mutation");
    VARIANT parent;VariantInit(&parent);test(!HasUsableParentValue(S_OK,parent),"precreated empty marker is not a usable parent");parent.vt=VT_I4;parent.lVal=1;test(!HasUsableParentValue(S_OK,parent),"integer marker is not an IUnknown parent");parent.vt=VT_UNKNOWN;parent.punkVal=nullptr;test(!HasUsableParentValue(S_OK,parent),"null IUnknown marker is not a parent");
    baseline->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&parent.punkVal));test(HasUsableParentValue(S_OK,parent),"nonnull opaque IUnknown parent must reject no-parent contract");test(!HasUsableParentValue(S_FALSE,parent),"non-success parent read is not usable");test(!HasUsableParentValue(E_FAIL,parent),"failed parent read is not usable");
    test(ClassifyParentValue(S_OK,parent)==ParentReading::UsableParent,"successful opaque parent is conclusive");test(ClassifyParentValue(S_FALSE,parent)==ParentReading::Unknown,"S_FALSE with nonempty parent is inconclusive");test(ClassifyParentValue(E_FAIL,parent)==ParentReading::Unknown,"failed parent read cannot establish absence");VariantClear(&parent);
    test(ClassifyParentValue(S_FALSE,parent)==ParentReading::NoUsableParent,"S_FALSE plus VT_EMPTY is explicit absence");test(ClassifyParentValue(E_FAIL,parent)==ParentReading::Unknown,"failed empty parent read cannot establish absence");parent.vt=VT_I4;parent.lVal=0;test(ClassifyParentValue(S_FALSE,parent)==ParentReading::Unknown,"S_FALSE plus unexpected integer is inconclusive");VariantClear(&parent);
    printf("No-window self-test: %d checks, %d failures; no COM activation, windows, native profile changes, or key calls.\n",checks,failures);return failures?1:0;
}
static void Run(const Options& options,int& failures){
    wchar_t own[32768]{};auto count=GetModuleFileNameW(nullptr,own,static_cast<DWORD>(std::size(own)));Require(count&&count<std::size(own),"executable path");auto base=std::filesystem::path(own).parent_path();
    // Process environment only; the native engine may persist settings only here.
    auto appdata=base/L"fixture-appdata";auto settings=appdata/L"RIUM Keys";std::filesystem::create_directories(settings);
    {std::ofstream config(settings/L"config.ini",std::ios::trunc);Require(config.good(),"write isolated fixture configuration");config<<"[Options]\nUseUiHelper=0\nInlineComposition=1\n";Require(config.good(),"save isolated fixture configuration");}
    Require(SetEnvironmentVariableW(L"APPDATA",appdata.c_str())&&SetEnvironmentVariableW(L"TEMP",appdata.c_str())&&SetEnvironmentVariableW(L"TMP",appdata.c_str()),"isolate fixture process environment");
    CandidateFile candidate(base);Manifest manifest(base);Session session(failures);
    // Match Microsoft's TSFpad lifecycle: activate TSF before creating/showing
    // the document window, so the operator's first focus event reaches TSF.
    Check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&session.thread)),"thread manager");Check(session.thread->ActivateEx(&session.client,0),"activate own TSF thread");session.activated=true;
    Check(session.thread.As(&session.keys),"actual TSF keystroke manager");
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RiumInlineFixture";Require(RegisterClassW(&cls)!=0,"register fixture window");
    cls.lpfnWndProc=SurfaceProc;cls.lpszClassName=L"RiumInlineFixtureSurface";cls.hCursor=LoadCursorW(nullptr,IDC_IBEAM);Require(RegisterClassW(&cls)!=0,"register fixture surface");
    session.window=CreateWindowW(L"RiumInlineFixture",L"RIUM own-document TSF fixture",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,660,180,nullptr,nullptr,cls.hInstance,nullptr);Require(session.window!=nullptr,"create fixture window");
    session.edit=CreateWindowExW(WS_EX_CLIENTEDGE,options.native?L"EDIT":cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(options.native?ES_AUTOHSCROLL:0),20,25,600,45,session.window,nullptr,cls.hInstance,nullptr);Require(session.edit!=nullptr,"create fixture document");
    if(!options.native){
        session.store.Attach(new FixtureStore(session.edit,options.staticFlags));SetWindowLongPtrW(session.edit,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(session.store.Get()));
        Check(session.thread->CreateDocumentMgr(&session.document),"create own document");TfEditCookie cookie=0;Check(session.document->CreateContext(session.client,0,static_cast<ITextStoreACP*>(session.store.Get()),&session.context,&cookie),"create own context");
        Check(session.document->Push(session.context.Get()),"push own context");session.pushed=true;
        Check(session.thread->AssociateFocus(session.edit,session.document.Get(),&session.previousAssociation),"associate own focus before window is shown");session.associated=true;
    }
    TraceActivation(session,"prepared-before-show");
    ShowWindow(session.window,SW_SHOWNOACTIVATE);
    if(!IsWindowVisible(session.window))ShowWindow(session.window,SW_SHOWNOACTIVATE);
    printf("WAIT_FOR_FOCUS pid=%lu main=%p edit=%p mode=%s timeoutMs=45000; click the document area.\n",GetCurrentProcessId(),session.window,session.edit,options.mode);fflush(stdout);
    auto deadline=GetTickCount64()+45000;while(IsWindow(session.window)&&!session.Focused()&&GetTickCount64()<deadline)Pump();
    Require(session.Focused(),"fixture was not manually focused within 45 seconds; no explicit profile activation or key calls performed");
    TraceActivation(session,"operator-focused");
    if(!options.native)Check(session.thread->SetFocus(session.document.Get()),"confirm manually focused own TSF document");
    Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&session.profiles)),"profiles");Require(session.profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&session.previous)==S_OK,"capture previous process keyboard profile before activation");
    ComPtr<ITfCompartmentMgr> compartments;Check(session.thread.As(&compartments),"own thread compartments");Check(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,&session.open),"own thread open compartment");
    session.previousOpen=CaptureOpenMode(session.open.Get());
    // Arm both restorations before activation, which may mutate mode even on failure.
    session.profileTouched=true;auto activationHr=session.profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,ServiceId,ProfileId,nullptr,TF_IPPMF_FORPROCESS);
    printf("ACTIVATE_PROFILE scope=FORPROCESS hr=0x%08lX priorOpen=%ld\n",static_cast<unsigned long>(activationHr),session.previousOpen);Require(activationHr==S_OK,"activate local candidate FORPROCESS");
    TF_INPUTPROCESSORPROFILE active{};auto profileHr=session.profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);bool profileMatch=profileHr==S_OK&&active.dwProfileType==TF_PROFILETYPE_INPUTPROCESSOR&&active.langid==0x412&&active.clsid==ServiceId&&active.guidProfile==ProfileId;
    wchar_t activeClsid[40]{},activeGuid[40]{},activeCategory[40]{};StringFromGUID2(active.clsid,activeClsid,40);StringFromGUID2(active.guidProfile,activeGuid,40);StringFromGUID2(active.catid,activeCategory,40);
    printf("PROFILE_READBACK hr=0x%08lX match=%d type=%lu lang=0x%04X clsid=%ls profile=%ls hkl=%p substitute=%p caps=0x%08lX flags=0x%08lX category=%ls\n",static_cast<unsigned long>(profileHr),profileMatch,active.dwProfileType,active.langid,activeClsid,activeGuid,active.hkl,active.hklSubstitute,active.dwCaps,active.dwFlags,activeCategory);fflush(stdout);Require(profileMatch,"candidate profile readback mismatch");
    TraceActivation(session,"profile-selected");
    Require(session.Focused(),"focus changed during profile activation");
    if(!options.native){
        AssertNoParent(session.document.Get());TF_STATUS status{};Check(session.context->GetStatus(&status),"actual TSF status");printf("CONTEXT static=0x%08lX requested=0x%08lX\n",status.dwStaticFlags,options.staticFlags);Require(status.dwStaticFlags==options.staticFlags,"TSF did not expose requested fixture flags");
    }
    VARIANT korean;VariantInit(&korean);korean.vt=VT_I4;korean.lVal=1;Require(session.open->SetValue(session.client,&korean)==S_OK,"select Korean in fixture thread only");VARIANT actual;VariantInit(&actual);auto modeHr=session.open->GetValue(&actual);bool isKorean=modeHr==S_OK&&actual.vt==VT_I4&&actual.lVal!=0;VariantClear(&actual);Require(isKorean,"Korean compartment readback failed");
    // TSF may defer loading a selected TIP until a focused text document exists.
    // Never force-load the DLL: require Windows to load the exact candidate before keys.
    WaitForCandidate(session,candidate,options.native);
    TF_INPUTPROCESSORPROFILE readyProfile{};Require(session.profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&readyProfile)==S_OK&&SameProfile(active,readyProfile),"candidate profile changed during readiness wait");
    if(options.native){
        puts("NATIVE_OBSERVATION: physical input only; close this disposable window within 120 seconds. No automated Chromium verdict.");fflush(stdout);auto end=GetTickCount64()+120000;std::wstring previous;
        while(IsWindow(session.window)&&GetTickCount64()<end){Pump();wchar_t ownText[256]{};GetWindowTextW(session.edit,ownText,static_cast<int>(std::size(ownText)));auto text=std::wstring(ownText);if(text!=previous){printf("OWN_DOCUMENT length=%zu\n",text.size());fflush(stdout);previous=text;}}
        return;
    }
    struct Step{UINT key;const wchar_t* text;bool composing;const char* label;};
    const Step steps[]={{'G',L"\u314e",true,"initial"},{'K',L"\ud558",true,"vowel"},{'S',L"\ud55c",true,"final"},{VK_BACK,L"\ud558",true,"backspace-final"},{'S',L"\ud55c",true,"restore-final"},{'R',L"\ud55c\u3131",true,"commit-prefix"},{'M',L"\ud55c\uadf8",true,"next-vowel"},{'F',L"\ud55c\uae00",true,"next-final"},{VK_SPACE,L"\ud55c\uae00 ",false,"space-exactly-once"}};
    AssertState(session,L"",false,"empty-start");
    for(const auto& step:steps){Press(session,step.key);AssertState(session,step.text,step.composing,step.label);InvalidateRect(session.edit,nullptr,TRUE);}
    TF_INPUTPROCESSORPROFILE finalProfile{};Require(session.profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&finalProfile)==S_OK&&SameProfile(active,finalProfile),"candidate profile changed during fixture");candidate.VerifyLoaded();
    printf("PASS actual Windows TSF own-document mode=%s; 9 consumed keys, collapsed selections, live compositions and final commit. This is not a Chromium application test.\n",options.mode);
}
int wmain(int argc,wchar_t** argv){
    int failures=0;bool initialized=false;
    try{auto options=ParseOptions(argc,argv);if(options.selfTest)return SelfTest();auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Check(hr,"initialize fixture COM");initialized=true;Run(options,failures);}
    catch(const std::exception& error){printf("ERROR: %s\n",error.what());++failures;}
    if(initialized)CoUninitialize();printf("Inline failures: %d\n",failures);return failures?1:0;
}
