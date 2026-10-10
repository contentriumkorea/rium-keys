#include "inline-preedit-core.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <limits.h>
#include <errno.h>

// Disposable, explicit, ONE-SHOT inline-only cancellation viability probe.
// Not installed, not a universal input policy, no text/window-title logging.
// WndProc dispatch bypasses accelerator/pretranslation message-loop handling.
// Only one focused HWND, process, UI thread and at most 120 seconds are allowed.
static const DWORD MAGIC = 0x52495034, VERSION = 4;
static const LONG CAPACITY = 1024;
struct Row {
    volatile LONG ready;
    ULONGLONG tick;
    UINT kind, message, probe, detail;
    DWORD flags, conversion;
    LONG length;
    UINT open;
    ULONG_PTR window, focus, context;
};
struct Trace {
    DWORD magic, version, pid, tid;
    ULONG_PTR target;
    ULONGLONG token, until;
    volatile LONG stop, count, dropped, writers;
    Row rows[CAPACITY];
};
static void mapping_name(WCHAR *out, DWORD pid, DWORD tid) {
    _snwprintf(out, 128, L"Local\\RIUM-InlinePreedit-4C107A32-%lu-%lu", pid, tid);
}
static bool valid(Trace *t, DWORD pid, DWORD tid) {
    return t && t->magic == MAGIC && t->version == VERSION && t->pid == pid &&
        t->tid == tid && t->token && t->target;
}

#ifdef EXPERIMENT_DLL
struct Session { InlineExperiment engine; Trace *trace; ULONGLONG token; bool initialized; };
static thread_local Session session;
static bool enabled(void *p) {
    Session *s = (Session *)p;
    return s->trace && valid(s->trace, GetCurrentProcessId(), GetCurrentThreadId()) &&
        s->trace->token == s->token && !InterlockedCompareExchange(&s->trace->stop, 0, 0);
}
static ULONGLONG now(void *) { return GetTickCount64(); }
static HWND focus(void *) { return GetFocus(); }
static HIMC get_context(void *, HWND w) { return ImmGetContext(w); }
static void release_context(void *, HWND w, HIMC h) { ImmReleaseContext(w, h); }
static HWND default_ime(void *, HWND w) { return ImmGetDefaultIMEWnd(w); }
static bool modifiers(void *) {
    return (GetKeyState(VK_SHIFT) & 0x8000) || (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
        (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);
}
static void record(Session *s, UINT kind, UINT message, UINT probe, DWORD flags,
                   UINT detail = 0, LONG length = LONG_MIN, HIMC context = nullptr,
                   UINT open = 2, DWORD conversion = 0xffffffff) {
    Trace *t = s->trace;
    if (!t) return;
    LONG index = InterlockedIncrement(&t->count) - 1;
    if (index < 0 || index >= CAPACITY) { InterlockedIncrement(&t->dropped); return; }
    Row *r = &t->rows[index];
    r->tick = GetTickCount64(); r->kind = kind; r->message = message;
    r->probe = probe; r->flags = flags; r->detail = detail; r->length = length;
    r->window = t->target; r->focus = (ULONG_PTR)GetFocus(); r->context = (ULONG_PTR)context;
    r->open = open; r->conversion = conversion;
    InterlockedExchange(&r->ready, 1);
}
static UINT original_vk(void *p, HWND w) {
    Session *s = (Session *)p;
    UINT key = ImmGetVirtualKey(w);
    if (key == 'C' || key == 'V') {
        HIMC h = ImmGetContext(w);
        DWORD conversion = 0xffffffff, sentence = 0;
        UINT open = 2;
        if (h) {
            open = ImmGetOpenStatus(h) != FALSE;
            if (!ImmGetConversionStatus(h, &conversion, &sentence)) conversion = 0xffffffff;
        }
        record(s, 4, WM_KEYDOWN, key == 'C' ? 1 : 2, 0, 0, LONG_MIN, h, open, conversion);
        if (h) ImmReleaseContext(w, h);
    }
    return key;
}
static BOOL notify(void *p, HIMC h, DWORD action, DWORD index, DWORD value) {
    Session *s = (Session *)p;
    BOOL result = ImmNotifyIME(h, action, index, value);
    LONG length = LONG_MIN;
    HWND w = s->engine.config.target;
    if (IsWindow(w) && GetFocus() == w) {
        HIMC current = ImmGetContext(w);
        if (current && current == h)
            length = ImmGetCompositionStringW(current, GCS_COMPSTR, nullptr, 0);
        if (current) ImmReleaseContext(w, current);
    }
    record(s, 5, WM_IME_COMPOSITION, s->engine.stats.probe, 0, result != FALSE, length);
    return result;
}
static BOOL install(void *, HWND w, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data) {
    return SetWindowSubclass(w, proc, id, data);
}
static BOOL remove_subclass(void *, HWND w, SUBCLASSPROC proc, UINT_PTR id) {
    return RemoveWindowSubclass(w, proc, id);
}
static HMODULE hold_module(void *, const void *address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)address, &module);
    return module;
}
static void release_module(void *, HMODULE module) { if (module) FreeLibrary(module); }
static void event(void *p, UINT code, UINT probe, DWORD flags, UINT detail) {
    record((Session *)p, 1, code, probe, flags, detail);
}
// Outermost callback owns the mapping. Nested Win32 callbacks borrow it while
// the writer is active; no per-run mapping pointer remains after callback exit.
struct MappingScope {
    HANDLE mapping = nullptr;
    Trace *trace = nullptr;
    bool own = false;
    explicit MappingScope(Session *s) {
        if (s->trace) { trace = s->trace; return; }
        WCHAR name[128]; mapping_name(name, GetCurrentProcessId(), GetCurrentThreadId());
        mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!mapping) return;
        trace = (Trace *)MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Trace));
        if (!valid(trace, GetCurrentProcessId(), GetCurrentThreadId())) {
            if (trace) UnmapViewOfFile(trace);
            trace = nullptr; CloseHandle(mapping); mapping = nullptr; return;
        }
        own = true; s->trace = trace;
        InterlockedIncrement(&trace->writers);
        if ((!s->initialized || s->token != trace->token) && !s->engine.processing && !s->engine.active_scope) {
            s->token = trace->token;
            InlineConfig config = { trace->pid, trace->tid, (HWND)trace->target, trace->until, true };
            InlineOps ops = { s, enabled, now, focus, get_context, release_context, default_ime,
                original_vk, modifiers, notify, install, remove_subclass, hold_module, release_module, event };
            InlineInit(&s->engine, &config, &ops); s->initialized = true;
            record(s, 6, 0, 0, 0); // Only metadata: proves an actual hook callback initialized this run.
        }
    }
    ~MappingScope() {
        if (own) {
            InterlockedDecrement(&trace->writers);
            session.trace = nullptr;
            UnmapViewOfFile(trace); CloseHandle(mapping);
        }
    }
};
static void observe_message(Session *s, UINT stage, HWND w, UINT m, WPARAM p, LPARAM l) {
    if (!enabled(s) || GetTickCount64() >= s->engine.config.until) return;
    if (w != s->engine.config.target) return;
    if (m == WM_IME_STARTCOMPOSITION || m == WM_IME_ENDCOMPOSITION || m == WM_IME_COMPOSITION)
        record(s, stage, m, 0, m == WM_IME_COMPOSITION ? (DWORD)l : 0);
    else if (m == WM_KEYDOWN || m == WM_KEYUP || m == WM_CHAR || m == WM_IME_CHAR) {
        UINT probe = (p == 'C' || p == 'c') ? 1 : (p == 'V' || p == 'v') ? 2 : p == VK_PROCESSKEY ? 3 : 0;
        record(s, stage, m, probe, 0);
    }
}
extern "C" __declspec(dllexport) LRESULT CALLBACK ProbeQueue(int code, WPARAM removal, LPARAM data) {
    LRESULT next = CallNextHookEx(nullptr, code, removal, data);
    if (code != HC_ACTION || removal != PM_REMOVE || !data) return next;
    MappingScope mapped(&session);
    if (!mapped.trace || !session.initialized || !enabled(&session)) return next;
    MSG *msg = (MSG *)data;
    observe_message(&session, 2, msg->hwnd, msg->message, msg->wParam, msg->lParam);
    InlineProcessQueue(&session.engine, msg, code, removal);
    return next;
}
extern "C" __declspec(dllexport) LRESULT CALLBACK ProbeDispatch(int code, WPARAM sent, LPARAM data) {
    if (code == HC_ACTION && data) {
        MappingScope mapped(&session);
        if (mapped.trace && session.initialized && enabled(&session)) {
            const CWPSTRUCT *msg = (const CWPSTRUCT *)data;
            observe_message(&session, 3, msg->hwnd, msg->message, msg->wParam, msg->lParam);
            InlineObserveDispatch(&session.engine, msg->hwnd, msg->message, msg->wParam, msg->lParam);
        }
    }
    return CallNextHookEx(nullptr, code, sent, data);
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

#else
static bool number(const WCHAR *s, ULONGLONG *out) {
    if (!s || !*s || *s == '-') return false;
    wchar_t *end = nullptr; errno = 0;
    *out = wcstoull(s, &end, 0);
    return errno == 0 && end && !*end;
}
static void flush(Trace *t, LONG *delivered) {
    while (*delivered < CAPACITY && InterlockedCompareExchange(&t->rows[*delivered].ready, 0, 0)) {
        Row *r = &t->rows[(*delivered)++];
        printf("ROW n=%ld tick=%llu kind=%u message=0x%x probe=%u flags=0x%lx detail=%u length=%ld open=%u conversion=0x%lx target=%p focus=%p himc=%p\n",
            *delivered, (unsigned long long)r->tick, r->kind, r->message, r->probe, r->flags,
            r->detail, r->length, r->open, r->conversion, (void *)r->window, (void *)r->focus, (void *)r->context);
    }
    fflush(stdout);
}
int wmain(int argc, WCHAR **argv) {
    ULONGLONG pid64 = 0, target64 = 0, seconds = 0;
    WCHAR name[128];
    if (argc == 4 && !wcscmp(argv[1], L"--stop")) {
        if (!number(argv[2], &pid64) || !number(argv[3], &target64) || !pid64 || pid64 > MAXDWORD || !target64 || target64 > MAXDWORD) return 2;
        mapping_name(name, (DWORD)pid64, (DWORD)target64);
        HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!h) { printf("STOP_NOT_RUNNING error=%lu\n", GetLastError()); return 3; }
        Trace *t = (Trace *)MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Trace));
        bool okay = valid(t, (DWORD)pid64, (DWORD)target64);
        if (okay) InterlockedExchange(&t->stop, 1);
        if (t) UnmapViewOfFile(t); CloseHandle(h);
        puts(okay ? "STOP_REQUESTED" : "STOP_INVALID"); return okay ? 0 : 4;
    }
    if (argc != 6 || wcscmp(argv[5], L"--inline-only-experiment") ||
        !number(argv[1], &pid64) || !number(argv[2], &target64) || !number(argv[4], &seconds) ||
        !pid64 || pid64 > MAXDWORD || !target64 || seconds < 1 || seconds > 120) {
        puts("usage: inline-preedit-experiment <pid> <focused-hwnd-dec-or-hex> <absolute-dll> <seconds-1..120> --inline-only-experiment\n       inline-preedit-experiment --stop <pid> <tid>"); return 2;
    }
    DWORD pid = (DWORD)pid64, owner = 0;
    HWND target = (HWND)(ULONG_PTR)target64;
    DWORD tid = GetWindowThreadProcessId(target, &owner);
    GUITHREADINFO gui = {}; gui.cbSize = sizeof(gui);
    if (!IsWindow(target) || !tid || owner != pid || pid == GetCurrentProcessId() ||
        !GetGUIThreadInfo(tid, &gui) || gui.hwndFocus != target) {
        puts("REFUSED target must be the currently focused HWND in the selected external process/thread"); return 5;
    }
    WCHAR full[32768];
    DWORD chars = GetFullPathNameW(argv[3], 32768, full, nullptr);
    if (!chars || chars >= 32768 || _wcsicmp(full, argv[3]) || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        puts("REFUSED DLL path must be an existing absolute path"); return 6;
    }
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    BOOL wow = FALSE;
    if (!process || !IsWow64Process(process, &wow) || wow) {
        if (process) CloseHandle(process); puts("REFUSED this build requires a native x64 target"); return 7;
    }
    mapping_name(name, pid, tid);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Trace), name);
    DWORD mapping_error = GetLastError();
    if (!mapping || mapping_error == ERROR_ALREADY_EXISTS) {
        if (mapping) CloseHandle(mapping); CloseHandle(process); puts("REFUSED another run still owns this thread"); return 8;
    }
    Trace *t = (Trace *)MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Trace));
    if (!t) { CloseHandle(mapping); CloseHandle(process); return 9; }
    ZeroMemory(t, sizeof(*t));
    LARGE_INTEGER counter; QueryPerformanceCounter(&counter);
    t->magic = MAGIC; t->version = VERSION; t->pid = pid; t->tid = tid; t->target = (ULONG_PTR)target;
    t->token = ((ULONGLONG)GetCurrentProcessId() << 32) ^ (ULONGLONG)counter.QuadPart;
    if (!t->token) t->token = 1;
    t->until = GetTickCount64() + seconds * 1000;
    HMODULE module = LoadLibraryExW(full, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    HOOKPROC queue = module ? (HOOKPROC)(void *)GetProcAddress(module, "ProbeQueue") : nullptr;
    HOOKPROC dispatch = module ? (HOOKPROC)(void *)GetProcAddress(module, "ProbeDispatch") : nullptr;
    HHOOK hd = dispatch ? SetWindowsHookExW(WH_CALLWNDPROC, dispatch, module, tid) : nullptr;
    HHOOK hq = queue && hd ? SetWindowsHookExW(WH_GETMESSAGE, queue, module, tid) : nullptr;
    int result = 10;
    if (!hd || !hq) {
        printf("INSTALL_FAILED error=%lu\n", GetLastError()); InterlockedExchange(&t->stop, 1);
        if (hq) UnhookWindowsHookEx(hq); if (hd) UnhookWindowsHookEx(hd);
    } else {
        printf("READY version=%lu pid=%lu tid=%lu target=%p token=%llu until=%llu seconds=%llu inlineOnly=1 oneShot=1 kind=1:event,2:queue,3:send,4:mode,5:cancelLength,6:init unknownLength=%ld\n",
            VERSION, pid, tid, target, (unsigned long long)t->token, (unsigned long long)t->until,
            (unsigned long long)seconds, LONG_MIN); fflush(stdout);
        LONG delivered = 0;
        while (!InterlockedCompareExchange(&t->stop, 0, 0) && GetTickCount64() < t->until && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            flush(t, &delivered); WaitForSingleObject(process, 25);
        }
        InterlockedExchange(&t->stop, 1);
        BOOL q = UnhookWindowsHookEx(hq); DWORD qe = q ? 0 : GetLastError();
        BOOL d = UnhookWindowsHookEx(hd); DWORD de = d ? 0 : GetLastError();
        ULONGLONG drain_until = GetTickCount64() + 1000;
        while (InterlockedCompareExchange(&t->writers, 0, 0) && GetTickCount64() < drain_until) Sleep(5);
        flush(t, &delivered);
        LONG writers = InterlockedCompareExchange(&t->writers, 0, 0);
        printf("STOPPED queueUnhook=%u queueError=%lu dispatchUnhook=%u dispatchError=%lu rows=%ld dropped=%ld writers=%ld\n",
            q != FALSE, qe, d != FALSE, de, delivered, t->dropped, writers); fflush(stdout);
        result = q && d && !writers ? 0 : 11;
    }
    if (module) FreeLibrary(module);
    UnmapViewOfFile(t); CloseHandle(mapping); CloseHandle(process); return result;
}
#endif
