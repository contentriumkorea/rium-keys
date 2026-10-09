using System.Runtime.InteropServices;
using System.Text;

namespace AdobeKoreanShortcuts;
internal static class Native
{
    internal delegate nint HookProc(int code, nint message, nint data);
    [StructLayout(LayoutKind.Sequential)] internal struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] internal struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] internal struct GuiInfo
    { public uint Size, Flags; public nint Active, Focus, Capture, MenuOwner, MoveSize, Caret; public Rect CaretRect; }
    [StructLayout(LayoutKind.Sequential)] internal struct KeyInfo
    { public uint Key, Scan, Flags, Time; public nuint Extra; }
    [DllImport("user32.dll")] internal static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] internal static extern uint GetWindowThreadProcessId(nint window, out uint pid);
    [DllImport("user32.dll")] internal static extern bool GetGUIThreadInfo(uint thread, ref GuiInfo info);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(nint window, StringBuilder name, int length);
    [DllImport("user32.dll")] internal static extern short GetAsyncKeyState(int key);
    [DllImport("imm32.dll")] internal static extern nint ImmGetDefaultIMEWnd(nint window);
    [DllImport("user32.dll", SetLastError=true)] static extern nint SendMessageTimeout(nint window, uint msg, nuint wparam, nint lparam, uint flags, uint timeout, out nuint result);
    [DllImport("user32.dll", SetLastError=true)] internal static extern nint SetWindowsHookEx(int type, HookProc callback, nint module, uint thread);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] internal static extern nint GetModuleHandle(string? module);
    [DllImport("user32.dll")] internal static extern bool UnhookWindowsHookEx(nint hook);
    [DllImport("user32.dll")] internal static extern nint CallNextHookEx(nint hook, int code, nint message, nint data);
    [DllImport("user32.dll")] internal static extern bool RegisterHotKey(nint window, int id, uint modifiers, uint key);
    [DllImport("user32.dll")] internal static extern bool UnregisterHotKey(nint window, int id);
    [DllImport("user32.dll",SetLastError=true)] internal static extern bool PostMessage(nint window,uint message,nuint key,nint data);
    internal static bool Down(int key) => (GetAsyncKeyState(key) & 0x8000) != 0;
    internal static GuiInfo Info(nint window)
    {
        var info = new GuiInfo { Size = (uint)Marshal.SizeOf<GuiInfo>() };
        if (!GetGUIThreadInfo(GetWindowThreadProcessId(window, out _), ref info)) return default;
        return info;
    }
    internal static string Class(nint window)
    { var value = new StringBuilder(256); GetClassName(window, value, value.Capacity); return value.ToString(); }
    internal static bool Ime(nint window, nuint command, int value, out int result)
    {
        if(command is not (1 or 5) || value != 0) throw new ArgumentException("IME access is read-only");
        result = 0;
        if (window == 0) return false;
        var success = SendMessageTimeout(window, 0x283, command, value, 0x2 | 0x20, 15, out var returned) != 0;
        result = unchecked((int)returned); return success;
    }
}
