using System.Runtime.InteropServices;
namespace AdobeKoreanShortcuts;

// Direct key messages bypass IME translation. Never change the IME conversion mode.
internal sealed class Bridge : IDisposable
{
    readonly Func<FocusSample> current;
    readonly Native.HookProc callback;
    readonly Native.HookProc mouseCallback;
    long changedAt,transition;
    readonly Dictionary<int,(nint Window,nint Foreground,nint Focus,uint Scan,bool Extended,long Transition)> held=[];
    readonly HashSet<int> cancelled=[];
    readonly PressTracker presses=new();
    nint hook,mouseHook;
    internal bool Paused {get;private set;}
    internal int Count {get;private set;}
    internal string LastResult {get;private set;}="단축키 입력 대기";
    internal Bridge(FocusProbe probe):this(()=>probe.Current){}
    internal Bridge(Func<FocusSample> current)
    {
        this.current=current;callback=OnKey;mouseCallback=OnMouse;
        hook=Native.SetWindowsHookEx(13,callback,Native.GetModuleHandle(null),0);
        if(hook==0)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        mouseHook=Native.SetWindowsHookEx(14,mouseCallback,Native.GetModuleHandle(null),0);
        if(mouseHook==0){Native.UnhookWindowsHookEx(hook);hook=0;throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}
    }
    internal void TogglePause(){ReleaseAll();Paused=!Paused;}
    void Changed(){changedAt=Environment.TickCount64;transition++;}
    nint OnMouse(int code,nint message,nint pointer)
    {
        if(code>=0 && message is 0x201 or 0x202 or 0x204 or 0x205 or 0x207 or 0x208 or 0x20A or 0x20B or 0x20C)Changed();
        return Native.CallNextHookEx(mouseHook,code,message,pointer);
    }
    nint OnKey(int code,nint message,nint pointer)
    {
        if(code>=0)
        {
            try
            {
                var k=Marshal.PtrToStructure<Native.KeyInfo>(pointer);
                int key=(int)k.Key;bool down=message==0x100||message==0x104;
                if(key is 0x09 or 0x0D or 0x1B or 0x15 or 0x19 or 0xF2 or 0x20 or 0x11 or 0x12 or 0x5B or 0x5C || (key is >=0x21 and <=0x28) || (key is >=0x70 and <=0x87) || Native.Down(0x11) || Native.Down(0x12) || Native.Down(0x5B) || Native.Down(0x5C))Changed();
                bool firstDown=down && presses.Down(key);
                if(!down)presses.Up(key);
                if(cancelled.Contains(key)){if(!down)cancelled.Remove(key);return 1;}
                if(!down && held.Remove(key,out var previous))
                {
                    Native.PostMessage(previous.Window,0x101,(nuint)key,KeyMessage.Pack(previous.Scan,previous.Extended,true,true));
                    return 1;
                }
                if(down && !Paused)
                {
                    var f=current();var window=Native.GetForegroundWindow();var gui=Native.Info(window);
                    bool fresh=Policy.Fresh(f.At,Environment.TickCount64,f.Window==window,f.Focus==gui.Focus,gui.Size!=0);
                    var facts=new FocusFacts(f.Eligible,f.Text||FocusProbe.NativeText(gui.Focus),gui.Caret!=0,f.Dialog||gui.MenuOwner!=0,fresh && Policy.AfterTransition(f.At,changedAt));
                    if(held.TryGetValue(key,out var owner))
                    {
                        // Wait for a post-dispatch observation without losing key ownership.
                        // Any later transition or text evidence cancels the original owner.
                        if(owner.Foreground==window && owner.Focus==gui.Focus && owner.Transition==transition && Policy.ShouldBridge(facts with{Fresh=true},key,Native.Down(0x11),Native.Down(0x12),Native.Down(0x5B)||Native.Down(0x5C)))
                        {
                            if(facts.Fresh)Native.PostMessage(owner.Window,0x100,(nuint)key,KeyMessage.Pack(owner.Scan,owner.Extended,false,true));
                        }
                        else {Native.PostMessage(owner.Window,0x101,(nuint)key,KeyMessage.Pack(owner.Scan,owner.Extended,true,true));held.Remove(key);cancelled.Add(key);}
                        return 1;
                    }
                    if(firstDown && Policy.ShouldBridge(facts,key,Native.Down(0x11),Native.Down(0x12),Native.Down(0x5B)||Native.Down(0x5C)))
                    {
                        var target=f.App is "AfterFX" or "Adobe Premiere Pro"?window:gui.Focus;
                        if(f.Korean && target!=0)
                        {
                            bool repeat=held.ContainsKey(key);bool extended=(k.Flags&1)!=0;
                            if(Native.PostMessage(target,0x100,(nuint)key,KeyMessage.Pack(k.Scan,extended,false,repeat)))
                            {
                                Changed();held[key]=(target,window,gui.Focus,k.Scan,extended,transition);Count++;
                                LastResult="한글 상태 유지 · 단축키 메시지 전달";
                                return 1;
                            }
                            LastResult="단축키 전달 실패 · 원래 입력 통과";
                        }
                    }
                }
            }
            catch {LastResult="진단 오류 · 원래 입력 통과";}
        }
        return Native.CallNextHookEx(hook,code,message,pointer);
    }
    void ReleaseAll()
    {
        foreach(var pair in held)
        {
            Native.PostMessage(pair.Value.Window,0x101,(nuint)pair.Key,KeyMessage.Pack(pair.Value.Scan,pair.Value.Extended,true,true));
            cancelled.Add(pair.Key);
        }
        held.Clear();
    }
    public void Dispose(){ReleaseAll();if(hook!=0)Native.UnhookWindowsHookEx(hook);hook=0;if(mouseHook!=0)Native.UnhookWindowsHookEx(mouseHook);mouseHook=0;GC.KeepAlive(callback);GC.KeepAlive(mouseCallback);}
}
