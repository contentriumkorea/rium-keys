using System.Diagnostics;
using System.Windows.Automation;
namespace AdobeKoreanShortcuts;
internal record FocusSample(nint Window,nint Focus,uint Pid,string App,bool Eligible,bool Text,bool Dialog,long At,string Reason,string Context="",bool Korean=false);
internal sealed class UiaProbe : IDisposable
{
    volatile bool stop;
    readonly AdobeTextState textState=new();
    readonly Func<string,string> classifyApp;
    AutomationElement? focusCache;
    nint cachedWindow,cachedNativeFocus;
    FocusSample sample=new(0,0,0,"",false,true,false,0,"시작 중");
    internal FocusSample Current=>Volatile.Read(ref sample);
    internal UiaProbe(Func<string,string>? classifyApp=null)
    {
        this.classifyApp=classifyApp ?? (name=>name);
        new Thread(Run){IsBackground=true,Name="System focus probe"}.Start();
    }
    static CacheRequest Cache(bool names=false)
    {
        var cache=new CacheRequest{TreeScope=TreeScope.Element};
        foreach(var property in new[]{AutomationElement.ProcessIdProperty,AutomationElement.ControlTypeProperty,AutomationElement.IsKeyboardFocusableProperty,AutomationElement.HasKeyboardFocusProperty,AutomationElement.IsPasswordProperty,AutomationElement.NativeWindowHandleProperty,AutomationElement.IsValuePatternAvailableProperty,AutomationElement.IsTextPatternAvailableProperty})cache.Add(property);
        if(names){cache.Add(AutomationElement.ClassNameProperty);cache.Add(AutomationElement.NameProperty);}

        return cache;
    }
    void Run()
    {
        uint lastPid=0;string app="";
        while(!stop)
        {
            try
            {
                long observedAt=Environment.TickCount64;
                var window=Native.GetForegroundWindow();Native.GetWindowThreadProcessId(window,out uint pid);
                if(pid!=lastPid){using var process=Process.GetProcessById((int)pid);app=classifyApp(process.ProcessName);lastPid=pid;}
                var gui=Native.Info(window);
                bool dialog=Native.Class(window)=="#32770" || gui.MenuOwner!=0;
                bool text=true,eligible=false,korean=false;
                string context="",reason="입력 영역 확인 중";
                bool adobe=app is "AfterFX" or "Adobe Premiere Pro";
                bool excluded=app is "RiumKeys" or "WindowsTerminal" or "OpenConsole" or "conhost" or "cmd" or "powershell" or "pwsh" || Native.Class(window)=="ConsoleWindowClass";
                if(excluded)reason="원래 입력 사용";
                else if(dialog || gui.Caret!=0 || FocusProbe.NativeText(gui.Focus))reason="한글 입력 중";
                // Premiere returns main-window focus after a tab-header click. Its
                // subtree can contain thousands of unrelated controls; scanning it
                // cannot establish which internal panel owns the keyboard.
                else if(app=="Adobe Premiere Pro" && gui.Focus==window)reason="프리미어 패널 확인 불가 · 원래 입력 사용";
                else if(Native.Class(gui.Focus).Equals("Button",StringComparison.OrdinalIgnoreCase) || Native.Class(gui.Focus).StartsWith("WindowsForms10.BUTTON.",StringComparison.OrdinalIgnoreCase))
                {
                    eligible=true;text=false;reason="PC 단축키 대기";
                }
                else if(gui.Size!=0 && gui.Focus!=0)
                {
                    bool knownAdobe=false;
                    var cache=Cache(adobe);
                    using var cached=cache.Activate();
                    AutomationElement? focused=null;
                    if(cachedWindow==window && cachedNativeFocus==gui.Focus && focusCache!=null)
                    {
                        focused=focusCache.GetUpdatedCache(cache);
                        if(!focused.Cached.HasKeyboardFocus)focused=null;
                    }
                    if(focused==null)
                    {
                        var nativeFocus=AutomationElement.FromHandle(gui.Focus);
                        focused=nativeFocus?.FindFirst(TreeScope.Subtree,new PropertyCondition(AutomationElement.HasKeyboardFocusProperty,true));
                    }
                    focusCache=focused;cachedWindow=window;cachedNativeFocus=gui.Focus;
                    if(focused!=null && focused.Cached.ProcessId==pid)
                    {
                        text=false;
                        var facts=ReadFacts(focused);
                        int depth=0;
                        // Read the focused ancestry once for both panel identity and text safety.
                        // Re-read every inspection: Adobe can reuse native HWNDs for other tabs.
                        for(var el=focused;adobe && el!=null && depth++<12;el=TreeWalker.ControlViewWalker.GetParent(el,cache))
                        {
                            var c=el.Cached;if(c.ProcessId!=pid)break;
                            context+=c.ControlType.ProgrammaticName+":"+c.ClassName+"/";
                            if(c.Name is "AE Composition" or "AE Timeline" or "AE Project" or "Program Monitor" or "Source Monitor" or "Timeline")context+=c.Name+"/";
                            var f=depth==1?facts:ReadFacts(el);
                            if(f.Password || f.Writable || f.Role is "Edit" or "ComboBox" or "Spinner" || (f.HasText && f.ReadOnly!=true))text=true;
                            if(c.ControlType==ControlType.Window)break;
                        }
                        knownAdobe=Policy.KnownWorkspace(app,context);
                        eligible=knownAdobe || Policy.GenericWorkspace(facts.Role,facts.Focusable,facts.Password,facts.Writable,facts.HasText,facts.ReadOnly);
                        if(knownAdobe && textState.ProtectCanvas(pid,app,context))text=true;
                        if(!eligible || !(bool)focused.GetCurrentPropertyValue(AutomationElement.HasKeyboardFocusProperty))text=true;
                        reason=text?"한글 입력 / 원래 입력 사용":"PC 단축키 대기";
                    }
                }
                if(eligible && !text)
                {
                    // Cross-process IME reads stay out of the keyboard hook.
                    var ime=Native.ImmGetDefaultIMEWnd(gui.Focus);
                    korean=Native.Ime(ime,5,0,out int open) && open!=0 && Native.Ime(ime,1,0,out int mode) && (mode&1)!=0;
                }
                if(Native.GetForegroundWindow()==window && Native.Info(window).Focus==gui.Focus)
                    Volatile.Write(ref sample,new(window,gui.Focus,pid,app,eligible,text,dialog,observedAt,reason,context,korean));
            }
            catch{focusCache=null;cachedWindow=0;cachedNativeFocus=0;Volatile.Write(ref sample,new(0,0,0,"",false,true,false,Environment.TickCount64,"원래 입력 사용"));}
            Thread.Sleep(40);
        }
    }
    static (string Role,bool Focusable,bool Password,bool Writable,bool HasText,bool? ReadOnly) ReadFacts(AutomationElement el)
    {
        var c=el.Cached;
        bool writable=(bool)el.GetCachedPropertyValue(AutomationElement.IsValuePatternAvailableProperty) &&
            (!el.TryGetCurrentPattern(ValuePattern.Pattern,out var value) || !((ValuePattern)value).Current.IsReadOnly);
        bool hasText=(bool)el.GetCachedPropertyValue(AutomationElement.IsTextPatternAvailableProperty);
        bool? readOnly=null;
        if(hasText && el.TryGetCurrentPattern(TextPattern.Pattern,out var text) && ((TextPattern)text).DocumentRange.GetAttributeValue(TextPattern.IsReadOnlyAttribute) is bool flag)readOnly=flag;
        return(c.ControlType.ProgrammaticName.Replace("ControlType.",""),c.IsKeyboardFocusable,c.IsPassword,writable,hasText,readOnly);
    }
    public void Dispose()=>stop=true;
}
