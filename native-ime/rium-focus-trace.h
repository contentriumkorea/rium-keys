#pragma once
#include <windows.h>
#include <imm.h>
#include <stdio.h>

// Explicit, short-lived diagnosis inside the host process. Never records keys,
// composition strings, document text, titles, or window names. Disabled unless
// the ordinary-user installer opts one executable into a time-bounded session.
static inline void Rium_TraceFocus(ITfContext *context, const TF_STATUS *status, BOOL blocked) {
    static _Thread_local BOOL initialized;
    static _Thread_local ULONGLONG until;
    static _Thread_local wchar_t path[MAX_PATH];
    static _Thread_local ULONGLONG lastSample;
    static LONG totalRows;
    if (!initialized) {
        initialized = TRUE;
        wchar_t target[MAX_PATH] = {0}, own[MAX_PATH] = {0}, temp[MAX_PATH] = {0};
        DWORD cb = sizeof(target);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Contentrium\\RiumKeysInput", L"FocusTraceImage",
            RRF_RT_REG_SZ, NULL, target, &cb) != ERROR_SUCCESS || !target[0] ||
            !GetModuleFileNameW(NULL, own, MAX_PATH)) return;
        const wchar_t *name = wcsrchr(own, L'\\'); name = name ? name + 1 : own;
        if (_wcsicmp(name, target)) return;
        cb = sizeof(until);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Contentrium\\RiumKeysInput", L"FocusTraceUntil",
            RRF_RT_REG_QWORD, NULL, &until, &cb) != ERROR_SUCCESS) { until = 0; return; }
        DWORD tempLength = GetTempPathW(MAX_PATH, temp);
        if (!tempLength || tempLength >= MAX_PATH ||
            _snwprintf(path, MAX_PATH, L"%lsRiumKeys-focus-%lu.log", temp, GetCurrentProcessId()) < 0) { until = 0; return; }
        path[MAX_PATH - 1] = 0;
    }
    if (!until) return;
    ULONGLONG tick = GetTickCount64();
    if ((lastSample && tick - lastSample < 100) || InterlockedCompareExchange(&totalRows, 0, 0) >= 512) return;
    lastSample = tick;
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER now = { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
    if (now.QuadPart >= until || until - now.QuadPart > 30ull * 60 * 10000000) return;
    GUITHREADINFO info = { .cbSize = sizeof(info) };
    if (!GetGUIThreadInfo(GetCurrentThreadId(), &info)) return;
    wchar_t cls[96] = {0};
    if (info.hwndFocus) GetClassNameW(info.hwndFocus, cls, 96);
    COMPOSITIONFORM composition = {0}; CANDIDATEFORM candidate = {0};
    BOOL gotComposition = FALSE, gotCandidate = FALSE;
    HIMC imc = info.hwndFocus ? ImmGetContext(info.hwndFocus) : NULL;
    if (imc) {
        gotComposition = ImmGetCompositionWindow(imc, &composition);
        gotCandidate = ImmGetCandidateWindow(imc, 0, &candidate);
        ImmReleaseContext(info.hwndFocus, imc);
    }
    RECT client = {0}; if (info.hwndFocus) GetClientRect(info.hwndFocus, &client);
    wchar_t row[1024];
    _snwprintf(row, 1024,
        L"tid=%lu context=%p static=%lu dynamic=%lu blocked=%d focus=%p ownerTid=%lu class=%ls caret=%p caretRect=%ld,%ld,%ld,%ld gui=%lu imc=%p compOk=%d compStyle=%lu compPoint=%ld,%ld candOk=%d candStyle=%lu candPoint=%ld,%ld client=%ld,%ld,%ld,%ld\r\n",
        GetCurrentThreadId(), (void*)context, status->dwStaticFlags, status->dwDynamicFlags, blocked,
        (void*)info.hwndFocus, GetWindowThreadProcessId(info.hwndFocus, NULL), cls,
        (void*)info.hwndCaret, info.rcCaret.left, info.rcCaret.top, info.rcCaret.right, info.rcCaret.bottom,
        info.flags, (void*)imc, gotComposition, composition.dwStyle, composition.ptCurrentPos.x, composition.ptCurrentPos.y,
        gotCandidate, candidate.dwStyle, candidate.ptCurrentPos.x, candidate.ptCurrentPos.y,
        client.left, client.top, client.right, client.bottom);
    row[1023] = 0;
    static _Thread_local wchar_t previous[1024];
    if (!wcscmp(previous, row)) return;
    if (InterlockedIncrement(&totalRows) > 512) return;
    wcscpy(previous, row);
    char utf8[4096]; int length = WideCharToMultiByte(CP_UTF8, 0, row, -1, utf8, sizeof(utf8), NULL, NULL);
    if (length <= 1) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) { DWORD written; WriteFile(file, utf8, (DWORD)length - 1, &written, NULL); CloseHandle(file); }
}
