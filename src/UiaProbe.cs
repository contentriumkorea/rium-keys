using System.Diagnostics;
using System.Windows.Automation;

namespace AdobeKoreanShortcuts;
internal record FocusSample(nint Window, nint Focus, uint Pid, string App, bool Adobe, bool Text, bool Dialog, long At, string Reason, string Context="");
internal sealed class UiaProbe : IDisposable
{
    internal static readonly HashSet<string> Supported = new(StringComparer.OrdinalIgnoreCase)
    { "AfterFX", "Adobe Premiere Pro", "Photoshop", "Illustrator", "InDesign", "Animate", "Adobe Audition", "Lightroom", "Bridge", "Adobe Media Encoder", "Acrobat" };
    private volatile bool stop;
    readonly AdobeTextState textState=new();
    private FocusSample sample = new(0,0,0,"",false,true,false,0,"시작 중");
    internal FocusSample Current => Volatile.Read(ref sample);
    internal UiaProbe() { new Thread(Run) { IsBackground = true, Name = "Adobe focus probe" }.Start(); }
    void Run()
    {
        uint lastPid = 0; string app = ""; bool adobe = false;
        while (!stop)
        {
            try
            {
                long observedAt=Environment.TickCount64;
                var window = Native.GetForegroundWindow();
                Native.GetWindowThreadProcessId(window, out uint pid);
                if (pid != lastPid)
                {
                    lastPid = pid; adobe = false; app = "";
                    using var process = Process.GetProcessById((int)pid);
                    app = process.ProcessName;
                    if (Supported.Contains(app))
                    {
                        var path = process.MainModule?.FileName ?? "";
                        adobe = path.Contains("\\Adobe\\", StringComparison.OrdinalIgnoreCase) &&
                            (FileVersionInfo.GetVersionInfo(path).CompanyName ?? "").Contains("Adobe", StringComparison.OrdinalIgnoreCase);
                    }
                }
                var gui = Native.Info(window);
                bool text = true;
                bool dialog = Native.Class(window) == "#32770" || gui.MenuOwner != 0;
                string reason = "Adobe 외 프로그램";
                string context="";
                if (adobe)
                {
                    var nativeElement=gui.Focus!=0?AutomationElement.FromHandle(gui.Focus):null;
                    for(var el=nativeElement;el!=null;el=TreeWalker.ControlViewWalker.GetParent(el))
                    {
                        var curr=el.Current;
                        if(curr.ProcessId!=pid)break;
                        context+=curr.ControlType.ProgrammaticName+":"+curr.ClassName+"/";
                        if(curr.Name is "AE Composition" or "AE Timeline" or "AE Project" or "Program Monitor" or "Timeline")context+=curr.Name+"/";
                        if(context.Length>500)break;
                    }
                    text = !Policy.KnownWorkspace(app,context) || FocusProbe.NativeText(gui.Focus) || gui.Caret != 0 || textState.ProtectCanvas(pid,app,context);
                    reason = text ? "한글 입력 영역 보호" : "단축키 대기";
                    if(!text)
                    {
                    var focused = AutomationElement.FocusedElement;
                    if (focused == null || focused.Current.ProcessId != pid)
                    { text = true; reason = "포커스 확인 대기"; }
                    else
                    {
                        int depth=0;
                        for (var el = focused; el != null && depth++ < 12; el = TreeWalker.ControlViewWalker.GetParent(el))
                        {
                            var current = el.Current;
                            if (current.ProcessId != pid) break;
                            if (current.ControlType == ControlType.Edit || current.ControlType == ControlType.ComboBox || current.IsPassword)
                                text = true;
                            if (el.TryGetCurrentPattern(ValuePattern.Pattern, out var value) && !((ValuePattern)value).Current.IsReadOnly)
                                text = true;
                            if (el.TryGetCurrentPattern(TextPattern.Pattern, out var pattern))
                            {
                                var range = ((TextPattern)pattern).DocumentRange;
                                if (range.GetAttributeValue(TextPattern.IsReadOnlyAttribute) is bool readOnly && !readOnly) text = true;
                            }
                            if (current.ControlType == ControlType.Window) break;
                        }
                        if (text) reason = "한글 입력 영역 보호";
                    }
                    }
                }
                if (Native.GetForegroundWindow() == window && Native.Info(window).Focus == gui.Focus)
                    Volatile.Write(ref sample, new(window,gui.Focus,pid,app,adobe,text,dialog,observedAt,reason,context));
            }
            catch { Volatile.Write(ref sample, new(0,0,0,"",false,true,false,Environment.TickCount64,"접근할 수 없는 창 보호")); }
            Thread.Sleep(60);
        }
    }
    public void Dispose() { stop = true; }
}
