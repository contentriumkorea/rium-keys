using Microsoft.Win32;
using System.IO;
namespace AdobeKoreanShortcuts;
internal static class Preferences
{
    const string Key=@"Software\Contentrium\RiumKeys";
    const string RunKey=@"Software\Microsoft\Windows\CurrentVersion\Run";
    internal static string DataDir=>Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RiumKeys");
    internal static bool Read(string name,bool fallback=true)
    {try{using var key=Registry.CurrentUser.OpenSubKey(Key);return key?.GetValue(name) is int v?v!=0:fallback;}catch{return fallback;}}
    internal static bool Write(string name,bool value)
    {try{using var key=Registry.CurrentUser.CreateSubKey(Key);key.SetValue(name,value?1:0,RegistryValueKind.DWord);return true;}catch{return false;}}
    internal static bool Startup
    {
        get{try{using var key=Registry.CurrentUser.OpenSubKey(RunKey);return key?.GetValue("RiumKeys") is string;}catch{return false;}}
        set{using var key=Registry.CurrentUser.CreateSubKey(RunKey);if(value)key.SetValue("RiumKeys","\""+Environment.ProcessPath+"\"");else key.DeleteValue("RiumKeys",false);}
    }
    internal static bool Installed
    {
        get{try{using var key=Registry.CurrentUser.OpenSubKey(Key);return key?.GetValue("InstallDir") is string dir && string.Equals(Path.GetFullPath(Path.Combine(dir,"RiumKeys.exe")),Environment.ProcessPath,StringComparison.OrdinalIgnoreCase);}catch{return false;}}
    }
    internal static void Log(Exception error)
    {try{Directory.CreateDirectory(DataDir);File.WriteAllText(Path.Combine(DataDir,"last-error.log"),DateTimeOffset.Now+" "+error.GetType().Name+": "+error.Message);}catch{}}
}
