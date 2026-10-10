using System.Windows.Forms;
using Application = System.Windows.Forms.Application;
namespace AdobeKoreanShortcuts;
internal static class Program
{
    [STAThread] static void Main(string[] args)
    {

        if(args.Length==2 && args[0]=="--probe") {ProbeWorker.Run(int.Parse(args[1]));return;}
        if(args.Contains("--exit"))
        {try{using var e=EventWaitHandle.OpenExisting("Local\\AdobeKoreanShortcuts.Exit");e.Set();}catch(WaitHandleCannotBeOpenedException){}return;}
        if(args.Contains("--health-check"))
        {
            try
            {
                ApplicationConfiguration.Initialize();
                Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
                using var key=typeof(Program).Assembly.GetManifestResourceStream("RiumKeys.UpdatePublicKey")??throw new InvalidOperationException("Missing publisher key");
                using var app=new TrayApp(true);
                using var deadline=new System.Windows.Forms.Timer{Interval=1000};
                deadline.Tick+=(_,_)=>app.ExitThread();deadline.Start();Application.Run(app);
            }
            catch(Exception ex){Preferences.Log(ex);Environment.ExitCode=1;}
            return;
        }
        using var mutex=new Mutex(true,"Local\\AdobeKoreanShortcuts",out bool created);
        if(!created)return;
        ApplicationConfiguration.Initialize();
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
        try{using var app=new TrayApp(args.Contains("--paused"));Application.Run(app);}
        catch(Exception ex){Preferences.Log(ex);}
    }
}
internal sealed class TrayApp : ApplicationContext
{
    readonly FocusProbe probe=new();
    readonly Bridge bridge;
    readonly NotifyIcon tray;
    readonly AutoUpdater updater=new();
    readonly System.Windows.Forms.Timer timer=new(){Interval=250};
    readonly EventWaitHandle exitSignal=new(false,EventResetMode.AutoReset,"Local\\AdobeKoreanShortcuts.Exit");
    readonly ToolStripMenuItem enabled=new("단축키 기능 켜기");
    readonly ToolStripMenuItem startup=new("Windows 시작 시 실행");
    readonly ToolStripMenuItem automatic=new("자동 업데이트");
    readonly ToolStripMenuItem status=new(){Enabled=false};
    readonly ToolStripMenuItem updateStatus=new(){Enabled=false};
    readonly ToolStripMenuItem check=new("업데이트 확인");
    long nextUpdate=Environment.TickCount64+10000;
    bool cleaned;
    internal TrayApp(bool paused)
    {
        bridge=new(probe);
        if(paused||!Preferences.Read("Enabled"))bridge.TogglePause();
        var menu=new ContextMenuStrip();
        menu.Items.Add(new ToolStripMenuItem("CONTENTRIUM Keys  "+AutoUpdater.Version){Enabled=false});
        menu.Items.Add(status);menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(enabled);menu.Items.Add(startup);menu.Items.Add(automatic);
        menu.Items.Add(check);menu.Items.Add(updateStatus);menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("종료",null,(_,_)=>ExitThread());
        enabled.Click+=(_,_)=>{if(Preferences.Write("Enabled",bridge.Paused))bridge.TogglePause();RefreshMenu();};
        startup.Click+=(_,_)=>{try{Preferences.Startup=!Preferences.Startup;}catch(Exception ex){Preferences.Log(ex);}RefreshMenu();};
        automatic.Click+=(_,_)=>{bool value=!Preferences.Read("AutoUpdate");if(Preferences.Write("AutoUpdate",value)){if(!value)updater.Cancel();else nextUpdate=Environment.TickCount64;}RefreshMenu();};
        check.Click+=async(_,_)=>await updater.CheckAsync();
        menu.Opening+=(_,_)=>RefreshMenu();
        tray=new NotifyIcon{Text="CONTENTRIUM Keys",Icon=System.Drawing.Icon.ExtractAssociatedIcon(Environment.ProcessPath!)??System.Drawing.SystemIcons.Application,ContextMenuStrip=menu,Visible=true};
        timer.Tick+=async(_,_)=>{
            if(exitSignal.WaitOne(0)){ExitThread();return;}
            RefreshMenu();
            if(Environment.TickCount64>=nextUpdate){nextUpdate=Environment.TickCount64+6*60*60*1000;if(Preferences.Read("AutoUpdate"))await updater.CheckAsync();}
        };
        RefreshMenu();timer.Start();
    }
    void RefreshMenu()
    {
        enabled.Checked=!bridge.Paused;startup.Checked=Preferences.Startup;automatic.Checked=Preferences.Read("AutoUpdate");
        status.Text=bridge.Paused?"기능 꺼짐":"기능 켜짐 · "+probe.Current.Reason;
        updateStatus.Text=updater.Status;check.Enabled=!updater.Busy;
        tray.Text=bridge.Paused?"CONTENTRIUM Keys · OFF":"CONTENTRIUM Keys · ON";
    }
    protected override void ExitThreadCore(){Cleanup();base.ExitThreadCore();}
    protected override void Dispose(bool disposing){if(disposing)Cleanup();base.Dispose(disposing);}
    void Cleanup(){if(cleaned)return;cleaned=true;timer.Stop();updater.Dispose();bridge.Dispose();probe.Dispose();tray.Visible=false;tray.Icon?.Dispose();tray.Dispose();timer.Dispose();exitSignal.Dispose();}
}



