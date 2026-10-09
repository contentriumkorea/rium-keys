using AdobeKoreanShortcuts;
using System.Diagnostics;
using System.Windows.Forms;
internal static class Program
{
 [STAThread] static int Main()
 {
  using var target=new TestWindow();int failed=0,count=0;
  void Check(string name,bool ok){count++;Console.WriteLine($"{(ok?"PASS":"FAIL")} {name}");if(!ok)failed++;}
  Check("null IME rejected",!Native.Ime(0,1,0,out _));
  Check("Korean mode read without modification",Native.Ime(target.Handle,1,0,out int mode)&&mode==1&&target.Mode==1);
  bool denied=false;try{Native.Ime(target.Handle,2,0,out _);}catch(ArgumentException){denied=true;}
  Check("IME writes prohibited",denied&&target.Mode==1);
  Check("key down queued",Native.PostMessage(target.Handle,0x100,0x56,KeyMessage.Pack(0x2f,false,false,false)));
  Check("key up queued",Native.PostMessage(target.Handle,0x101,0x56,KeyMessage.Pack(0x2f,false,true,true)));
  Application.DoEvents();
  Check("exact key down/up delivered",target.Keys.SequenceEqual(new uint[]{0x100,0x101}));
  Check("delivery never changes Korean mode",target.Mode==1&&target.Characters==0);
  target.Slow=true;
  var task=Task.Run(()=>{var watch=Stopwatch.StartNew();bool ok=Native.Ime(target.Handle,1,0,out _);return (!ok,watch.ElapsedMilliseconds);});
  while(!task.IsCompleted){Application.DoEvents();Thread.Sleep(1);}
  Check("IME getter timeout bounded",task.Result.Item1&&task.Result.Item2<250);
  Native.HookProc callback=(code,msg,data)=>Native.CallNextHookEx(0,code,msg,data);
  var hook=Native.SetWindowsHookEx(13,callback,Native.GetModuleHandle(null),0);
  Check("hook installs",hook!=0);Check("hook uninstalls",hook!=0&&Native.UnhookWindowsHookEx(hook));GC.KeepAlive(callback);
  Console.WriteLine($"Native checks: {count-failed}/{count} passed; private test window.");return failed==0?0:1;
 }
}
internal sealed class TestWindow:NativeWindow,IDisposable
{
 internal int Mode=1,Characters;internal bool Slow;internal List<uint> Keys=[];
 internal TestWindow(){CreateHandle(new CreateParams{Caption="Private key test",Parent=new nint(-3)});}
 protected override void WndProc(ref Message m)
 {
  if(m.Msg==0x283){if(Slow)Thread.Sleep(100);if(m.WParam==2)Mode=(int)m.LParam;m.Result=Mode;return;}
  if(m.Msg is 0x100 or 0x101){Keys.Add((uint)m.Msg);return;}
  if(m.Msg==0x102)Characters++;
  base.WndProc(ref m);
 }
 public void Dispose(){DestroyHandle();}
}