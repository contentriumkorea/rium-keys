namespace AdobeKoreanShortcuts;
public record FocusFacts(bool Eligible, bool Text, bool Caret, bool Dialog, bool Fresh);
public static class Policy
{
    public static bool GenericWorkspace(string role,bool focusable,bool password,bool writable,bool hasText,bool? readOnly) =>
        focusable && !password && !writable && role is not ("Edit" or "ComboBox" or "Spinner") &&
        (!hasText || readOnly==true) &&
        (role is "Button" or "CheckBox" or "RadioButton" or "MenuItem" or "TabItem" or "Slider" ||
         (role=="Document" && hasText && readOnly==true));
    public static bool AfterTransition(long observed,long changed) => observed>=changed && observed-changed>=75;
    public static bool KnownWorkspace(string app,string context) => app switch
    {
        "AfterFX" => context.Contains("AE Timeline/") || context.Contains("AE Composition/") || context.Contains("AE Project/"),
        "Adobe Premiere Pro" => context.Contains("Timeline/") || context.Contains("Program Monitor/"),
        _ => false
    };
    public static bool Fresh(long observed, long now, bool sameWindow, bool sameFocus, bool valid) =>
        valid && sameWindow && sameFocus && now >= observed && now-observed < 350;
    public static bool ShouldBridge(FocusFacts f, int key, bool ctrl, bool alt, bool win) =>
        f.Eligible && f.Fresh && !f.Text && !f.Caret && !f.Dialog && !ctrl && !alt && !win &&
        (key is >= 0x41 and <= 0x5A || key is >= 0xBA and <= 0xC0 || key is >= 0xDB and <= 0xDE);
}
public sealed class PressTracker
{
    readonly HashSet<int> pressed=[];
    public bool Down(int key)=>pressed.Add(key);
    public void Up(int key)=>pressed.Remove(key);
}
