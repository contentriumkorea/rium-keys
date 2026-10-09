namespace AdobeKoreanShortcuts;
public record FocusFacts(bool Adobe, bool Text, bool Caret, bool Dialog, bool Fresh);
public static class Policy
{
    public static bool KnownWorkspace(string app,string context) => app switch
    {
        "AfterFX" => context.Contains("AE Timeline/") || context.Contains("AE Composition/") || context.Contains("AE Project/"),
        "Adobe Premiere Pro" => context.Contains("Timeline/") || context.Contains("Program Monitor/"),
        _ => false
    };
    public static bool Fresh(long observed, long now, bool sameWindow, bool sameFocus, bool valid) =>
        valid && sameWindow && sameFocus && now >= observed && now-observed < 350;
    public static bool ShouldBridge(FocusFacts f, int key, bool ctrl, bool alt, bool win) =>
        f.Adobe && f.Fresh && !f.Text && !f.Caret && !f.Dialog && !ctrl && !alt && !win &&
        (key is >= 0x41 and <= 0x5A || key is >= 0xBA and <= 0xC0 || key is >= 0xDB and <= 0xDE);
}
public sealed class PressTracker
{
    readonly HashSet<int> pressed=[];
    public bool Down(int key)=>pressed.Add(key);
    public void Up(int key)=>pressed.Remove(key);
}
