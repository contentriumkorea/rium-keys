// Owns all text and calls TSF inside this process. No synthetic desktop input.
#include "../input-probe/FixtureStore.h"
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <imm.h>
static void Check(HRESULT hr,const char* what){if(FAILED(hr)){printf("%s: 0x%08lX\n",what,static_cast<unsigned long>(hr));throw std::runtime_error(what);}}
static void Pump(){MSG message;for(int round=0;round<8;++round){while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(5);}}
int wmain(int argc,wchar_t** argv){
    int failures=0;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        wchar_t own[MAX_PATH];GetModuleFileNameW(nullptr,own,MAX_PATH);auto base=std::filesystem::path(own).parent_path();
        auto appdata=base/L"fixture-appdata";std::filesystem::create_directories(appdata);SetEnvironmentVariableW(L"APPDATA",appdata.c_str());
        SetEnvironmentVariableW(L"TEMP",appdata.c_str());SetEnvironmentVariableW(L"TMP",appdata.c_str());
        auto manifest=base/L"inline.manifest";
        ACTCTXW activation{};activation.cbSize=sizeof(activation);activation.lpSource=manifest.c_str();
        auto context=CreateActCtxW(&activation);if(context==INVALID_HANDLE_VALUE)throw std::runtime_error("activation context");
        ULONG_PTR cookie=0;if(!ActivateActCtx(context,&cookie))throw std::runtime_error("activate manifest");
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RiumInlineFixture";RegisterClassW(&cls);
        auto window=CreateWindowW(cls.lpszClassName,L"RIUM inline test",WS_OVERLAPPEDWINDOW,0,0,600,160,nullptr,nullptr,cls.hInstance,nullptr);
        bool native=argc>1&&wcscmp(argv[1],L"--native")==0;
        auto edit=CreateWindowW(native?L"EDIT":cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,10,10,560,30,window,nullptr,cls.hInstance,nullptr);
        ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(edit);Pump();
        auto focusDeadline=GetTickCount64()+45000;
        while(GetForegroundWindow()!=window && GetTickCount64()<focusDeadline){Pump();}
        if(GetForegroundWindow()!=window)throw std::runtime_error("fixture was not focused; no keys tested");
        ComPtr<ITfThreadMgrEx> thread;Check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&thread)),"thread");TfClientId client=0;Check(thread->ActivateEx(&client,0),"activate thread");
        ComPtr<ITfInputProcessorProfileMgr> profiles;Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles)),"profiles");
        Check(profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,ServiceId,ProfileId,nullptr,TF_IPPMF_FORPROCESS),"activate local profile");
        Pump();wchar_t loaded[MAX_PATH]{};GetModuleFileNameW(GetModuleHandleW(L"RiumKeysInput.dll"),loaded,MAX_PATH);wprintf(L"DLL: %ls\n",loaded);
        if(std::filesystem::path(loaded)!=base/L"RiumKeysInput.dll")throw std::runtime_error("fixture must load its own candidate DLL");
        ComPtr<ITfDocumentMgr> document;ComPtr<ITfContext> textContext;ComPtr<FixtureStore> store;
        if(!native){store.Attach(new FixtureStore(edit));Check(thread->CreateDocumentMgr(&document),"document");TfEditCookie editCookie;Check(document->CreateContext(client,0,static_cast<ITextStoreACP*>(store.Get()),&textContext,&editCookie),"context");Check(document->Push(textContext.Get()),"push");ComPtr<ITfDocumentMgr> previous;Check(thread->AssociateFocus(edit,document.Get(),&previous),"associate focus");SetFocus(edit);Check(thread->SetFocus(document.Get()),"focus");Pump();}
        else{SetFocus(edit);Pump();Check(thread->GetFocus(&document),"native document");if(!document)throw std::runtime_error("no native document");Check(document->GetTop(&textContext),"native context");}
        TF_STATUS status{};Check(textContext->GetStatus(&status),"status");printf("Context static flags: 0x%lx\n",status.dwStaticFlags);
        if(native){
            puts("READY: physical input only. Close this disposable window after testing.");fflush(stdout);
            std::wstring previous;auto deadline=GetTickCount64()+240000;
            while(IsWindow(window)&&GetTickCount64()<deadline){
                Pump();wchar_t committed[256]{},composing[256]{};GetWindowTextW(edit,committed,256);
                auto imc=ImmGetContext(edit);if(imc){ImmGetCompositionStringW(imc,GCS_COMPSTR,composing,sizeof(composing)-sizeof(wchar_t));ImmReleaseContext(edit,imc);}
                auto snapshot=std::wstring(committed)+L"|"+composing;
                if(snapshot!=previous){printf("DOCUMENT ");for(auto c:std::wstring(committed))printf("U+%04X ",c);printf(" COMPOSITION ");for(auto c:std::wstring(composing))printf("U+%04X ",c);puts("");fflush(stdout);previous=snapshot;}
            }
            thread->Deactivate();return 0;
        }
        ComPtr<ITfKeystrokeMgr> keys;Check(thread.As(&keys),"keys");printf("Foreground-own: %d\n",GetForegroundWindow()==window);
        auto press=[&](UINT key){BYTE state[256]{};SetKeyboardState(state);BOOL test=FALSE,eaten=FALSE;auto flags=1|(static_cast<LPARAM>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16);Check(keys->TestKeyDown(key,flags,&test),"test down");if(test)Check(keys->KeyDown(key,flags,&eaten),"key down");Pump();printf("key=%02X test=%d eaten=%d\n",key,test,eaten);return eaten!=FALSE;};
        auto current=[&](){if(store)return store->text;wchar_t text[256]{};GetWindowTextW(edit,text,256);return std::wstring(text);};
        auto expect=[&](const wchar_t* expected,const char* label){auto actual=current();bool passed=actual==expected;printf("%s %s: ",passed?"PASS":"FAIL",label);for(auto c:actual)printf("U+%04X ",c);puts("");if(!passed)++failures;};
        // Select Korean using the same preserved layout command, local to this thread.
        if(!press('G')){press(VK_HANGUL);press('G');}
        expect(L"\u314e","initial immediately in document");press('K');expect(L"\ud558","vowel replaces initial");press('S');expect(L"\ud55c","final replaces syllable");
        press(VK_BACK);expect(L"\ud558","backspace final");press('S');press('R');expect(L"\ud55c\u3131","committed prefix and new preedit");press('M');press('F');expect(L"\ud55c\uae00","last syllable visible before commit");press(VK_SPACE);expect(L"\ud55c\uae00 ","commit without duplicate");
        thread->SetFocus(nullptr);if(!native)document->Pop(TF_POPF_ALL);textContext.Reset();document.Reset();store.Reset();thread->Deactivate();keys.Reset();thread.Reset();profiles.Reset();DestroyWindow(window);DeactivateActCtx(0,cookie);ReleaseActCtx(context);
    }catch(const std::exception& error){printf("ERROR: %s\n",error.what());++failures;}
    CoUninitialize();printf("Inline failures: %d\n",failures);return failures?1:0;
}
