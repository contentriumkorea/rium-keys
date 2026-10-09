using AdobeKoreanShortcuts;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Application=System.Windows.Forms.Application;
internal static class Program
{
    [STAThread] static void Main(string[] args)
    {
        if(args.Length==2 && args[0]=="--probe"){ProbeWorker.Run(int.Parse(args[1]));return;}
        ApplicationConfiguration.Initialize();
        using var form=new Form{Text="RIUM Keys — isolated input detection test",Width=480,Height=430};
        Exception? failure=null;
        form.Shown+=async (_,_)=>{try{await Run(form);}catch(Exception ex){failure=ex;}finally{form.Close();}};
        Application.Run(form);
        if(failure!=null)throw failure;
    }
    static async Task Run(Form form)
    {
        var button=new TestButton{Text="Shortcut surface",Top=20,Left=20,Width=180};
        var edit=new TextBox{Top=70,Left=20,Width=300};
        var password=new TextBox{Top=110,Left=20,Width=300,UseSystemPasswordChar=true};
        var combo=new ComboBox{Top=150,Left=20,Width=300};combo.Items.Add("example");
        form.Controls.AddRange([button,edit,password,combo]);
        var wpfButton=new System.Windows.Controls.Button{Content="Shared-window shortcut",Height=30};
        var wpfEdit=new System.Windows.Controls.TextBox{Height=30};
        var panel=new System.Windows.Controls.StackPanel();panel.Children.Add(wpfButton);panel.Children.Add(wpfEdit);
        var group=new System.Windows.Controls.GroupBox{Header="Timeline",Content=panel};
        var host=new System.Windows.Forms.Integration.ElementHost{Top=200,Left=20,Width=300,Height=110,Child=group};form.Controls.Add(host);
        using var probe=new FocusProbe();
        form.Activate();
        async Task Expect(Control control,bool workspace,string label)
        {
            long changed=Environment.TickCount64;control.Focus();var expectedFocus=Native.Info(form.Handle).Focus;var clock=Stopwatch.StartNew();bool pass=false;FocusSample f=probe.Current;
            while(clock.ElapsedMilliseconds<4000)
            {
                await Task.Delay(1);f=probe.Current;
                if(f.Pid==Environment.ProcessId && f.Focus==expectedFocus && f.At>changed && Policy.Fresh(f.At,Environment.TickCount64,true,true,true) && (workspace?(f.Eligible&&!f.Text):f.Text)){pass=true;break;}
            }
            if(!pass)throw new Exception(label+$" expectedPid={Environment.ProcessId} expectedFocus={control.Handle} age={Environment.TickCount64-f.At} "+System.Text.Json.JsonSerializer.Serialize(FocusWire.From(f)));
            Console.WriteLine($"PASS {label} ({clock.ElapsedMilliseconds}ms)");
        }
        await Expect(button,true,"non-Adobe button recognized");
        await Expect(edit,false,"editable field bypassed");
        await Expect(password,false,"password field bypassed");
        await Expect(combo,false,"combo field bypassed");
        await Expect(button,true,"return to non-text surface");
        async Task<nint> ExpectWpf(System.Windows.UIElement control,bool workspace,string label)
        {
            long changed=Environment.TickCount64;control.Focus();System.Windows.Input.Keyboard.Focus(control);
            var expected=Native.Info(form.Handle).Focus;var clock=Stopwatch.StartNew();FocusSample f=probe.Current;
            while(clock.ElapsedMilliseconds<4000)
            {
                await Task.Delay(1);f=probe.Current;
                if(f.Pid==Environment.ProcessId && f.Focus==expected && f.At>changed && Policy.Fresh(f.At,Environment.TickCount64,true,true,true) && (workspace?(f.Eligible&&!f.Text):f.Text))
                {Console.WriteLine($"PASS {label} ({clock.ElapsedMilliseconds}ms)");return expected;}
            }
            throw new Exception(label+$" expected={expected} age={Environment.TickCount64-f.At} "+System.Text.Json.JsonSerializer.Serialize(FocusWire.From(f)));
        }
        var first=await ExpectWpf(wpfButton,true,"custom framework non-text detection");
        panel.Children.Remove(wpfButton);
        wpfButton=new System.Windows.Controls.Button{Content="Replacement shortcut control",Height=30};panel.Children.Insert(0,wpfButton);
        await ExpectWpf(wpfButton,true,"removed focused element recovers under same native HWND");
        var second=await ExpectWpf(wpfEdit,false,"custom framework editor bypass");
        if(first!=second)throw new Exception("Fixture expected shared native focus handle");
        Console.WriteLine("PASS text/non-text classification changes within same native HWND");
        // Exercise Adobe policy on our own UIA tree; never drive an Adobe/user document.
        using(var adobeProbe=new UiaProbe(name=>name=="IntegrationTests"?"Adobe Premiere Pro":name))
        {
            wpfButton.Focus();System.Windows.Input.Keyboard.Focus(wpfButton);
            var nativeFocus=Native.Info(form.Handle).Focus;
            async Task Panel(string name,bool protectedText)
            {
                long changed=Environment.TickCount64;group.Header=name;
                var clock=Stopwatch.StartNew();FocusSample f=adobeProbe.Current;
                while(clock.ElapsedMilliseconds<4000)
                {
                    await Task.Delay(1);f=adobeProbe.Current;
                    if(f.Pid==Environment.ProcessId && f.Focus==nativeFocus && f.At>changed && Policy.Fresh(f.At,Environment.TickCount64,true,true,true) && f.Context.Contains(name+"/") && f.Eligible && f.Text==protectedText)
                    {Console.WriteLine($"PASS same-HWND Adobe fixture {name}: protected={protectedText} ({clock.ElapsedMilliseconds}ms)");return;}
                }
                throw new Exception("Adobe panel fixture "+name+": "+System.Text.Json.JsonSerializer.Serialize(FocusWire.From(f)));
            }
            await Panel("Timeline",false);
            await Panel("Program Monitor",true); // No verified type-tool state: must protect.
            await Panel("Source Monitor",false);
            await Panel("Timeline",false);
        }
        await Expect(button,true,"native shortcut target restored");
        // Exercise the real bridge against only this private window. The IME fact is a fixture,
        // not evidence of compatibility with every real input method or application.
        FocusSample snapshot=probe.Current with{Korean=true};
        using var bridge=new Bridge(()=>snapshot);
        var callback=typeof(Bridge).GetMethod("OnKey",BindingFlags.NonPublic|BindingFlags.Instance)!;
        nint memory=Marshal.AllocHGlobal(Marshal.SizeOf<Native.KeyInfo>());
        nint Key(int key,bool down,uint flags=0)
        {
            Marshal.StructureToPtr(new Native.KeyInfo{Key=(uint)key,Scan=0x2f,Flags=flags},memory,false);
            return (nint)callback.Invoke(bridge,[0,(nint)(down?0x100:0x101),memory])!;
        }
        try
        {
            if(Key(0x56,true)!=1)throw new Exception("Bridge did not own key down");
            if(Key(0x56,true)!=1)throw new Exception("Bridge did not own repeat");
            Application.DoEvents();
            if(button.Downs!=1 || button.Ups!=0)throw new Exception("Pending repeat must retain ownership without replaying stale facts");
            Thread.Sleep(100);snapshot=snapshot with{At=Environment.TickCount64};
            if(Key(0x56,true)!=1)throw new Exception("Bridge lost repeat ownership");
            Application.DoEvents();
            if(button.Downs!=2 || button.Ups!=0)throw new Exception("Repeat did not resume after fresh observation");
            if(Key(0x56,false)!=1)throw new Exception("Bridge did not own key up");
            Application.DoEvents();
            if(button.Downs!=2 || button.Ups!=1)throw new Exception("Focused control did not receive balanced shortcut");
            Console.WriteLine("PASS generic shortcut delivered to focused control with paired key-up");
            Thread.Sleep(100);
            if(Key(0x56,true,0x10)!=1 || Key(0x56,false,0x10)!=1)throw new Exception("Injected/remote input did not use same routing");
            Application.DoEvents();
            Console.WriteLine("PASS injected input uses the same routing without a feedback loop");
            Thread.Sleep(100);snapshot=snapshot with{At=Environment.TickCount64};
            Key(0x56,true);Application.DoEvents();int oldDowns=button.Downs,oldUps=button.Ups;
            Key(0x09,true);Key(0x09,false);
            Key(0x56,true);Key(0x56,false);Application.DoEvents();
            if(button.Downs!=oldDowns || button.Ups!=oldUps+1)throw new Exception("Transition during held repeat failed to release original owner exactly once");
            Console.WriteLine("PASS transition cancels held repeat with one matching release");
            Thread.Sleep(100);snapshot=snapshot with{At=Environment.TickCount64};
            Key(0x56,true);Application.DoEvents();oldDowns=button.Downs;oldUps=button.Ups;
            snapshot=snapshot with{At=Environment.TickCount64,Text=true};
            Key(0x56,true);Key(0x56,false);Application.DoEvents();
            if(button.Downs!=oldDowns || button.Ups!=oldUps+1)throw new Exception("Text transition during held repeat not protected");
            snapshot=snapshot with{Text=false};
            Console.WriteLine("PASS same-window text transition cancels held repeat");
            Key(0x09,true);Key(0x09,false);
            if(Key(0x56,true)==1)throw new Exception("Stale text-transition sample captured first character");Key(0x56,false);
            Console.WriteLine("PASS transition immediately passes first character through");
            foreach(int transition in new[]{0x11,0x71,0x20,0x1B,0x15})
            {
                Key(transition,true);Key(transition,false);
                if(Key(0x56,true)==1)throw new Exception("Transition failed: "+transition);Key(0x56,false);
            }
            Console.WriteLine("PASS modifier/F2/Space/Escape/Hangul transitions invalidate cached eligibility");
            snapshot=snapshot with{Text=true};
            var timings=new List<double>();
            for(int i=0;i<1000;i++)
            {
                var start=Stopwatch.GetTimestamp();
                if(Key(0x56,true)==1)throw new Exception("Text input intercepted");Key(0x56,false);
                timings.Add(Stopwatch.GetElapsedTime(start).TotalMilliseconds);
            }
            timings.Sort();Console.WriteLine($"PASS 1000 text-input pairs bypassed; p99 {timings[989]:F3}ms per pair (includes reflection)");
        }
        finally{Marshal.FreeHGlobal(memory);}
        Console.WriteLine("Live detection and bridge integration checks passed");
    }
}





internal sealed class TestButton : Button
{
    internal int Downs,Ups;
    protected override void WndProc(ref Message m){if(m.Msg==0x100 && m.WParam==0x56)Downs++;if(m.Msg==0x101 && m.WParam==0x56)Ups++;base.WndProc(ref m);}
}
