#pragma once
// Included by the existing registration controller only for RIUM_REUSED_ENGINE.
// Uses a real Win32 EDIT and its own IMM/TSF bridge, with no custom text store.
static bool ReusedKoreanMode(ITfInputProcessorProfileMgr* profiles,ITfCompartment* open){
    if(!profiles||!open)return false;
    TF_INPUTPROCESSORPROFILE current{};auto read=profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&current);
    VARIANT mode;VariantInit(&mode);auto modeRead=open->GetValue(&mode);
    bool matches=read==S_OK&&current.dwProfileType==TF_PROFILETYPE_INPUTPROCESSOR&&current.langid==0x412&&
        current.clsid==ServiceId&&current.guidProfile==ProfileId&&modeRead==S_OK&&mode.vt==VT_I4&&mode.lVal!=0;
    VariantClear(&mode);return matches;
}
struct ReusedButtonCounter{
    WNDPROC previous=nullptr;int down=0,up=0;bool firstWord=false,pendingDown=false;
    ComPtr<ITfInputProcessorProfileMgr> profiles;ComPtr<ITfCompartment> open;
};
static LRESULT CALLBACK ReusedButtonProc(HWND window,UINT message,WPARAM w,LPARAM l){
    auto counter=reinterpret_cast<ReusedButtonCounter*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(counter&&w=='V'&&(message==WM_KEYDOWN||message==WM_KEYUP)){
        bool eligible=counter->firstWord&&ReusedKoreanMode(counter->profiles.Get(),counter->open.Get());
        if(message==WM_KEYDOWN){counter->pendingDown=eligible;if(eligible)++counter->down;}
        else{if(eligible&&counter->pendingDown)++counter->up;counter->pendingDown=false;}
        Print("Button received original V after Korean word: eligible=%d down=%d up=%d\n",eligible,counter->down,counter->up);
        return 0;
    }
    return counter?CallWindowProcW(counter->previous,window,message,w,l):DefWindowProcW(window,message,w,l);
}
static bool SameReusedProfile(const TF_INPUTPROCESSORPROFILE& a,const TF_INPUTPROCESSORPROFILE& b){
    return a.dwProfileType==b.dwProfileType&&a.langid==b.langid&&a.clsid==b.clsid&&a.guidProfile==b.guidProfile&&a.hkl==b.hkl;
}
static bool RestoreReusedProfile(ITfInputProcessorProfileMgr* profiles,const TF_INPUTPROCESSORPROFILE& previous){
    auto restored=profiles->ActivateProfile(previous.dwProfileType,previous.langid,previous.clsid,previous.guidProfile,previous.hkl,TF_IPPMF_FORPROCESS);
    TF_INPUTPROCESSORPROFILE actual{};auto read=profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&actual);
    bool match=read==S_OK&&SameReusedProfile(previous,actual);
    Print("Restore HRESULT: 0x%08lx; previous profile readback matches: %d\n",static_cast<unsigned long>(restored),match);
    return restored==S_OK&&match;
}
static void ReusedEngineSession(){
    auto base=DllPath();base.resize(base.find_last_of(L'\\'));
    auto appdata=base+L"\\fixture-appdata";CreateDirectoryW(appdata.c_str(),nullptr);
    auto configDir=appdata+L"\\RIUM Keys";CreateDirectoryW(configDir.c_str(),nullptr);
    FILE* file=nullptr;_wfopen_s(&file,(configDir+L"\\config.ini").c_str(),L"w");Require(file!=nullptr,"create isolated fixture config");
    fputs("[Options]\nUseUiHelper=0\n",file);fclose(file);
    Require(SetEnvironmentVariableW(L"APPDATA",appdata.c_str())!=FALSE,"isolate fixture configuration");
    Require(UserTip(true),"enable temporary reused-engine profile");Check(RefreshTipCache(),"refresh temporary profile cache");
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RiumReusedEngineFixture";
    RegisterClassW(&cls);
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"CONTENTRIUM Keys - 입력 확인",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,680,290,nullptr,nullptr,cls.hInstance,nullptr);
    Require(window!=nullptr,"create isolated native fixture");
    HWND guide=CreateWindowExW(0,L"STATIC",
        L"1. 아래 입력칸에 '한글'을 쓰고 Space를 누르세요.\n"
        L"2. '단축키 확인' 버튼을 클릭하고 영문 V 키를 누르세요.\n"
        L"3. 입력칸 끝에 '한글'과 Space를 한 번 더 입력하세요.\n"
        L"한/영 키는 누르지 마세요. 통과하면 이 창은 자동으로 닫힙니다.",
        WS_CHILD|WS_VISIBLE,20,12,630,84,window,nullptr,cls.hInstance,nullptr);
    HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,20,104,620,32,window,nullptr,cls.hInstance,nullptr);
    HWND button=CreateWindowExW(0,L"BUTTON",L"단축키 확인 (V)",WS_CHILD|WS_VISIBLE|WS_TABSTOP,20,152,270,30,window,nullptr,cls.hInstance,nullptr);
    HWND progress=CreateWindowExW(0,L"STATIC",L"입력기 준비 중...",WS_CHILD|WS_VISIBLE,20,199,620,24,window,nullptr,cls.hInstance,nullptr);
    for(auto control:{guide,edit,button,progress})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    ReusedButtonCounter counter;SetWindowLongPtrW(button,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&counter));
    counter.previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(button,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(ReusedButtonProc)));
    ShowWindow(window,SW_SHOW);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(edit);
    ComPtr<ITfThreadMgr> thread;TfClientId client=0;Check(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&thread)),"native fixture thread manager");Check(ActivateThread(thread.Get(),&client),"activate native fixture thread");
    ComPtr<ITfInputProcessorProfileMgr> profiles;Check(CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles)),"native fixture profiles");
    TF_INPUTPROCESSORPROFILE previous{};Check(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&previous),"capture local profile");
    bool restored=false,passed=false;
    try {
        TF_INPUTPROCESSORPROFILE requested{};HRESULT found=E_PENDING;auto deadline=GetTickCount64()+3000;
        do {found=profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,ServiceId,ProfileId,nullptr,&requested);if(found==S_OK)break;MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(20);}while(GetTickCount64()<deadline);
        Check(found,"temporary profile becomes available");
        auto activated=profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x412,ServiceId,ProfileId,nullptr,TF_IPPMF_FORPROCESS);
        Require(activated==S_OK,"activate reused engine in this process only");
        TF_INPUTPROCESSORPROFILE active{};Check(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active),"read active keyboard profile");
        Require(active.clsid==ServiceId&&active.guidProfile==ProfileId,"reused engine is the active profile");
        SetFocus(button);SetFocus(edit);
        MSG initial;while(PeekMessageW(&initial,nullptr,0,0,PM_REMOVE)){TranslateMessage(&initial);DispatchMessageW(&initial);}
        ComPtr<ITfCompartmentMgr> compartments;Check(thread.As(&compartments),"mode compartment manager");ComPtr<ITfCompartment> open;Check(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,&open),"open mode compartment");
        VARIANT value;VariantInit(&value);value.vt=VT_I4;value.lVal=1;Check(open->SetValue(client,&value),"select Korean in fixture only");
        counter.profiles=profiles;counter.open=open;
        SetWindowTextW(progress,L"1 / 3  한글 입력 후 Space를 눌러주세요.");
        Line("READY: type gksrmf + Space; click Shortcut surface and press V; return and type gksrmf + Space.");
        auto end=GetTickCount64()+360000;bool first=false,inlineSeen=false,shortcutSeen=false;size_t lastLength=static_cast<size_t>(-1);
        while(GetTickCount64()<end&&IsWindow(window)){
            MSG message;
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                // Native EDIT owns the IMM/TSF compatibility dispatch. Unlike
                // our custom text-store fixture, do not run a second key sink.
                // Count V only inside the application's actual button WndProc.
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            wchar_t contents[256]{};GetWindowTextW(edit,contents,256);std::wstring actual=contents;
            auto imc=ImmGetContext(edit);
            if(imc){
                auto preeditBytes=ImmGetCompositionStringW(imc,GCS_COMPSTR,nullptr,0);
                if(preeditBytes>0&&!actual.empty())inlineSeen=true;
                ImmReleaseContext(edit,imc);
            }
            if(actual.size()!=lastLength){Print("Fixture text length: %zu\n",actual.size());lastLength=actual.size();}
            if(!first&&actual==L"\ud55c\uae00 "&&ReusedKoreanMode(profiles.Get(),open.Get())){
                first=true;counter.firstWord=true;SetWindowTextW(progress,L"2 / 3  단축키 확인 버튼을 클릭하고 V 키를 눌러주세요.");
            }
            if(first&&!shortcutSeen&&counter.down>0&&counter.up>0){shortcutSeen=true;SetWindowTextW(progress,L"3 / 3  입력칸 끝에 한글 + Space를 한 번 더 입력하세요.");}
            if(first&&counter.down>0&&counter.up>0&&actual==L"\ud55c\uae00 \ud55c\uae00 "){
                TF_INPUTPROCESSORPROFILE now{};auto read=profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&now);
                VARIANT mode;VariantInit(&mode);auto modeRead=open->GetValue(&mode);
                passed=inlineSeen&&read==S_OK&&SameReusedProfile(active,now)&&modeRead==S_OK&&mode.vt==VT_I4&&mode.lVal!=0;
                Print("Inline preedit visible in native document before commit: %d\n",inlineSeen);
                VariantClear(&mode);Print("Fork profile and Korean selection retained: %d\n",passed);break;
            }
            Sleep(5);
        }
        restored=RestoreReusedProfile(profiles.Get(),previous);
    }catch(...){if(!RestoreReusedProfile(profiles.Get(),previous))Line("CLEANUP FAILURE: previous local input profile restoration unconfirmed.");DestroyWindow(window);thread->Deactivate();throw;}
    DestroyWindow(window);thread->Deactivate();Require(restored,"restore previous local input profile");Require(passed,"native Korean/shortcut/Korean transition did not pass");
    Line("PASS: reused engine physical Korean/shortcut/Korean in native EDIT. Local profile restored.");
}
