using System.Diagnostics;
using System.IO;
using System.Text.Json;
namespace AdobeKoreanShortcuts;

// Isolate potentially blocked UIA providers and their native allocations from the keyboard hook.
internal sealed class FocusProbe : IDisposable
{
    readonly object gate=new();
    volatile bool stop;
    Process? worker;
    FocusSample sample=new(0,0,0,"",false,true,false,0,"포커스 확인 대기");
    internal FocusSample Current=>Volatile.Read(ref sample);
    internal FocusProbe()=>new Thread(Run){IsBackground=true,Name="Focus worker supervisor"}.Start();
    internal static bool NativeText(nint focus)
    {var c=Native.Class(focus);return c.Contains("Edit",StringComparison.OrdinalIgnoreCase)||c.Contains("Rich",StringComparison.OrdinalIgnoreCase)||c.Contains("Combo",StringComparison.OrdinalIgnoreCase);}
    void Reset()=>Volatile.Write(ref sample,new(0,0,0,"",false,true,false,0,"포커스 확인 대기"));
    void Run()
    {
        while(!stop)
        {
            Process? child=null;
            try
            {
                lock(gate)
                {
                    if(stop)return;
                    child=new Process {StartInfo=new ProcessStartInfo(Environment.ProcessPath!) {UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true}};
                    child.StartInfo.ArgumentList.Add("--probe");
                    child.StartInfo.ArgumentList.Add(Environment.ProcessId.ToString());
                    child.OutputDataReceived+=(_,e)=>
                    {
                        if(e.Data==null || stop)return;
                        try
                        {
                            var v=JsonSerializer.Deserialize<FocusWire>(e.Data);
                            lock(gate)
                                if(v!=null && !stop && ReferenceEquals(worker,child))Volatile.Write(ref sample,v.ToSample());
                        }
                        catch(JsonException) { }
                    };
                    worker=child;
                    child.Start();child.BeginOutputReadLine();
                }
                long started=Environment.TickCount64;
                while(!stop && !child.HasExited)
                {
                    child.Refresh();
                    long last=Math.Max(started,Current.At);
                    if(Environment.TickCount64-last>5000 || child.PrivateMemorySize64>256L*1024*1024)break;
                    Thread.Sleep(200);
                }
            }
            catch { }
            finally
            {
                lock(gate)
                {
                    if(child!=null){try{if(!child.HasExited)child.Kill();}catch{} child.Dispose();}
                    worker=null;Reset();
                }
            }
            for(int i=0;i<10 && !stop;i++)Thread.Sleep(100);
        }
    }
    public void Dispose()
    {
        stop=true;
        lock(gate){try{if(worker is {HasExited:false})worker.Kill();}catch{} Reset();}
    }
}
internal record FocusWire(long Window,long Focus,uint Pid,string App,bool Adobe,bool Text,bool Dialog,long At,string Reason,string Context)
{
    internal FocusSample ToSample()=>new((nint)Window,(nint)Focus,Pid,App,Adobe,Text,Dialog,At,Reason,Context);
    internal static FocusWire From(FocusSample s)=>new(s.Window.ToInt64(),s.Focus.ToInt64(),s.Pid,s.App,s.Adobe,s.Text,s.Dialog,s.At,s.Reason,s.Context);
}
internal static class ProbeWorker
{
    internal static void Run(int parentId)
    {
        using var parent=Process.GetProcessById(parentId);
        new Thread(()=>{try{parent.WaitForExit();}catch{} Process.GetCurrentProcess().Kill();}){IsBackground=true}.Start();
        using var probe=new UiaProbe();
        using var output=new StreamWriter(Console.OpenStandardOutput()){AutoFlush=true};
        try {while(true){output.WriteLine(JsonSerializer.Serialize(FocusWire.From(probe.Current)));Thread.Sleep(60);}}
        catch(IOException){Process.GetCurrentProcess().Kill();}
    }
}
