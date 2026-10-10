#include "inline-preedit-core.h"
#include <stdio.h>
#include <string.h>

// All windows are message-only, never shown/activated. IMM state is a controlled
// dependency because real IME cancellation would require external input state.
// SendMessage, Set/Remove/GetWindowSubclass and WH_CALLWNDPROC are native Win32.
enum Behavior { Handle, Delegate, InsertThenDelegate, Reenter, FocusChange,
    ContextChange, FocusRoundTrip, UnmatchedForward, ResultReenter,
    RawFocusChange, RawContextChange, RawDestroy, RawFocusRoundTrip,
    DelegateThenFocusChange, DelegateThenReenter, ReuseQueueStorage,
    OffspotEditor, RawCommandTextEditor };
struct Fixture {
    InlineExperiment engine;
    HWND app, ime, other, focus;
    HIMC himc;
    UINT original_vk;
    Behavior behavior;
    bool delegate_start, fail_install, fail_remove, enabled, modifiers, recursive;
    bool fail_notify, install_focus_roundtrip, hold_focus_roundtrip, saw_null_queue;
    MSG *queue_storage;
    UINT keys, app_updates, app_results, app_cancels, app_ends, app_imechars;
    UINT default_starts, default_updates, default_results, default_ends;
    UINT cancel_calls, bad_cancel_phase, wrong_notify, payload_mismatch, committed;
    UINT app_depth;
    bool interim;
    bool offspot_preedit;
    UINT raw_commands;
    WPARAM expected_character;
    ULONGLONG clock_offset;
};
static Fixture *current;
static UINT failures;
static const LPARAM PREEDIT = 0x6018; // Korean COMPSTR|COMPATTR|INSERTCHAR|NOMOVECARET

static void change_focus(Fixture *f, bool roundtrip) {
    f->focus = f->other;
    SendMessageW(f->other, WM_SETFOCUS, (WPARAM)f->app, 0);
    if (roundtrip) {
        f->focus = f->app;
        SendMessageW(f->app, WM_SETFOCUS, (WPARAM)f->other, 0);
    }
}

static LRESULT CALLBACK AppProc(HWND w, UINT m, WPARAM p, LPARAM l) {
    Fixture *f = current;
    if (!f || w != f->app) return DefWindowProcW(w, m, p, l);
    if (m == WM_KEYDOWN) {
        if (p == 'C' || p == 'V') {
            ++f->keys;
            if (f->behavior == RawCommandTextEditor) ++f->raw_commands;
            if (f->behavior == RawFocusChange || f->behavior == RawFocusRoundTrip)
                change_focus(f, f->behavior == RawFocusRoundTrip);
            if (f->behavior == RawContextChange) f->himc = (HIMC)(ULONG_PTR)2;
            if (f->behavior == RawDestroy) DestroyWindow(w);
        }
        return 0;
    }
    if (m == WM_IME_STARTCOMPOSITION) {
        if (f->delegate_start) SendMessageW(f->ime, m, p, l);
        return 0;
    }
    if (m == WM_IME_ENDCOMPOSITION) {
        ++f->app_ends; f->interim = false;
        if (f->behavior == OffspotEditor) SendMessageW(f->ime, m, p, l);
        return 0;
    }
    if (m == WM_IME_CHAR) {
        ++f->app_imechars;
        if (f->behavior == OffspotEditor || f->behavior == RawCommandTextEditor) ++f->committed;
        return 0;
    }
    if (m != WM_IME_COMPOSITION) return DefWindowProcW(w, m, p, l);
    ++f->app_depth;
    if (l & GCS_RESULTSTR) {
        ++f->app_results; f->interim = false;
        if (f->behavior == OffspotEditor || f->behavior == RawCommandTextEditor) {
            // Controlled IME model: result processing yields one IME_CHAR, where
            // this editor actually inserts text. Nothing is inserted twice.
            SendMessageW(f->app, WM_IME_CHAR, p, 1);
            if (f->behavior == OffspotEditor) SendMessageW(f->ime, m, p, l);
        } else ++f->committed;
    }
    if (l == 0) {
        ++f->app_cancels; f->interim = false;
        if (f->behavior == OffspotEditor) SendMessageW(f->ime, m, p, l);
    }
    if (l & GCS_COMPSTR) {
        ++f->app_updates;
        if (p != f->expected_character || l != PREEDIT) ++f->payload_mismatch;
        if (f->behavior == Handle || f->behavior == InsertThenDelegate ||
            f->behavior == RawCommandTextEditor) f->interim = true;
        if (!f->recursive) {
            if (f->behavior == Reenter) SendMessageW(f->app, WM_APP + 37, 0, 0);
            if (f->behavior == FocusChange || f->behavior == FocusRoundTrip) {
                change_focus(f, f->behavior == FocusRoundTrip);
            }
            if (f->behavior == ContextChange) f->himc = (HIMC)(ULONG_PTR)2;
            if (f->behavior == ResultReenter) {
                f->recursive = true;
                SendMessageW(f->app, WM_IME_COMPOSITION, p, GCS_RESULTSTR);
                f->recursive = false;
            }
        }
        if (f->behavior == UnmatchedForward) SendMessageW(f->ime, m, p + 1, l);
        else if (f->behavior != Handle && f->behavior != RawCommandTextEditor)
            SendMessageW(f->ime, m, p, l);
        if (f->behavior == DelegateThenFocusChange) change_focus(f, false);
        if (f->behavior == DelegateThenReenter) SendMessageW(f->app, WM_APP + 37, 0, 0);
        if (f->behavior == ReuseQueueStorage && f->queue_storage) {
            f->saw_null_queue = f->queue_storage->message == WM_NULL;
            f->queue_storage->message = WM_APP + 99;
            f->queue_storage->wParam = 17;
            f->queue_storage->lParam = 23;
        }
    }
    --f->app_depth;
    return 0;
}
static LRESULT CALLBACK ImeProc(HWND w, UINT m, WPARAM p, LPARAM l) {
    if (current && w == current->ime) {
        if (m == WM_IME_STARTCOMPOSITION) ++current->default_starts;
        if (m == WM_IME_ENDCOMPOSITION) {
            ++current->default_ends; current->offspot_preedit = false;
        }
        if (m == WM_IME_COMPOSITION && (l & GCS_COMPSTR)) {
            ++current->default_updates;
            if (current->behavior == OffspotEditor) current->offspot_preedit = true;
        }
        if (m == WM_IME_COMPOSITION && !l) current->offspot_preedit = false;
        if (m == WM_IME_COMPOSITION && (l & GCS_RESULTSTR)) ++current->default_results;
        if (m >= WM_IME_STARTCOMPOSITION && m <= WM_IME_COMPOSITION) return 0;
    }
    return DefWindowProcW(w, m, p, l);
}
static LRESULT CALLBACK DispatchHook(int code, WPARAM p, LPARAM l) {
    if (code == HC_ACTION && current && l) {
        const CWPSTRUCT *m = (const CWPSTRUCT *)l;
        InlineObserveDispatch(&current->engine, m->hwnd, m->message, m->wParam, m->lParam);
    }
    return CallNextHookEx(nullptr, code, p, l);
}
static bool enabled(void *p) { return ((Fixture *)p)->enabled; }
static ULONGLONG now(void *p) { return GetTickCount64() + ((Fixture *)p)->clock_offset; }
static HWND focus(void *p) { return ((Fixture *)p)->focus; }
static HIMC context(void *p, HWND) { return ((Fixture *)p)->himc; }
static void release_context(void *, HWND, HIMC) {}
static HWND default_ime(void *p, HWND) { return ((Fixture *)p)->ime; }
static UINT original(void *p, HWND) { return ((Fixture *)p)->original_vk; }
static bool modifiers(void *p) { return ((Fixture *)p)->modifiers; }
static BOOL notify(void *p, HIMC h, DWORD action, DWORD index, DWORD value) {
    Fixture *f = (Fixture *)p;
    ++f->cancel_calls;
    DWORD_PTR data = 0;
    if (f->app_depth || GetWindowSubclass(f->ime, InlineDefaultSubclass,
        f->engine.stats.last_subclass_id, &data)) ++f->bad_cancel_phase;
    if (h != f->himc || action != NI_COMPOSITIONSTR || index != CPS_CANCEL || value != 0)
        ++f->wrong_notify;
    if (f->fail_notify) return FALSE;
    SendMessageW(f->app, WM_IME_COMPOSITION, 0, 0);
    SendMessageW(f->app, WM_IME_ENDCOMPOSITION, 0, 0);
    return TRUE;
}
static BOOL install(void *p, HWND w, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR data) {
    Fixture *f = (Fixture *)p;
    BOOL result = f->fail_install ? FALSE : SetWindowSubclass(w, proc, id, data);
    if (f->install_focus_roundtrip) change_focus(f, true);
    return result;
}
static BOOL remove_subclass(void *p, HWND w, SUBCLASSPROC proc, UINT_PTR id) {
    return ((Fixture *)p)->fail_remove ? FALSE : RemoveWindowSubclass(w, proc, id);
}
static HMODULE hold(void *p, const void *address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)address, &module);
    if (((Fixture *)p)->hold_focus_roundtrip) change_focus((Fixture *)p, true);
    return module;
}
static void release(void *, HMODULE module) { if (module) FreeLibrary(module); }
static void event(void *, UINT, UINT, DWORD, UINT) {}
static bool absent(Fixture *f) {
    DWORD_PTR data = 0;
    return !GetWindowSubclass(f->ime, InlineDefaultSubclass, f->engine.stats.last_subclass_id, &data);
}
static void setup(Fixture *f, Behavior behavior = Handle) {
    ZeroMemory(f, sizeof(*f));
    f->app = CreateWindowExW(0, L"RIUMFixtureApp", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    f->ime = CreateWindowExW(0, L"RIUMFixtureIME", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    f->other = CreateWindowExW(0, L"RIUMFixtureApp", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    f->focus = f->app; f->himc = (HIMC)(ULONG_PTR)1; f->original_vk = 'C';
    f->behavior = behavior; f->enabled = true; f->expected_character = 0x314a; f->committed = 1;
    InlineConfig config = { GetCurrentProcessId(), GetCurrentThreadId(), f->app, now(f) + 10000, true };
    InlineOps ops = { f, enabled, now, focus, context, release_context, default_ime, original,
        modifiers, notify, install, remove_subclass, hold, release, event };
    InlineInit(&f->engine, &config, &ops);
    current = f;
}
static void teardown(Fixture *f) {
    current = nullptr;
    DestroyWindow(f->app); DestroyWindow(f->ime); DestroyWindow(f->other);
}
static MSG message(Fixture *f, UINT m, WPARAM p, LPARAM l) {
    MSG result = {};
    result.hwnd = f->app; result.message = m; result.wParam = p; result.lParam = l;
    result.time = (DWORD)now(f);
    return result;
}
static MSG pump(Fixture *f, UINT m, WPARAM p, LPARAM l, bool dispatch = true) {
    MSG msg = message(f, m, p, l);
    f->queue_storage = &msg;
    InlineProcessQueue(&f->engine, &msg, HC_ACTION, PM_REMOVE);
    f->queue_storage = nullptr;
    if (dispatch && msg.message != WM_NULL) SendMessageW(msg.hwnd, msg.message, msg.wParam, msg.lParam);
    return msg;
}
static bool begin(Fixture *f) {
    MSG key = pump(f, WM_KEYDOWN, VK_PROCESSKEY, 0x002e0001, false);
    pump(f, WM_IME_STARTCOMPOSITION, 0, 0);
    return key.message == WM_KEYDOWN && key.wParam == VK_PROCESSKEY && f->keys == 1;
}
static void check(bool pass, const char *name) {
    printf("%s %s\n", pass ? "PASS" : "FAIL", name);
    if (!pass) ++failures;
}

// Release-gate-only models. A real IME may supply different ordering; these
// tests establish a counterexample to a universal policy, not app compatibility.
// Cancellation removes the modeled live preedit, so the IME must not fabricate
// a later result to conceal lost text. Existing later-message tests stay intact.
static bool complete_editor_text(Fixture *f) {
    bool has_preedit = f->behavior == OffspotEditor ? f->offspot_preedit : f->interim;
    if (!has_preedit) return false;
    pump(f, WM_IME_ENDCOMPOSITION, 0, 0);
    pump(f, WM_IME_COMPOSITION, f->expected_character, GCS_RESULTSTR);
    return true;
}
static bool begin_release_gate(Fixture *f) {
    MSG key = message(f, WM_KEYDOWN, VK_PROCESSKEY, 0x002e0001);
    MSG original_key;
    memcpy(&original_key, &key, sizeof(key));
    f->queue_storage = &key;
    InlineProcessQueue(&f->engine, &key, HC_ACTION, PM_REMOVE);
    f->queue_storage = nullptr;
    bool unchanged = !memcmp(&original_key, &key, sizeof(key));
    pump(f, WM_IME_STARTCOMPOSITION, 0, 0);
    return unchanged;
}
static bool release_gate_preserved(Fixture *f, bool original_key_unchanged, bool produced) {
    // Outcome criteria only. A corrected policy may decline raw dispatch, and
    // cancellation counts alone do not establish whether text was preserved.
    return original_key_unchanged && produced && f->committed == 2 &&
        f->app_results == 1 && f->app_imechars == 1 && !f->payload_mismatch &&
        !f->raw_commands && absent(f) && !f->engine.stats.cleanup_failures &&
        !f->engine.stats.retained_scopes && !f->bad_cancel_phase && !f->wrong_notify;
}
static int release_gate(HHOOK hook) {
    puts("RELEASE_GATE enabled=1 scope=global-policy textPreservationRequired=1 expectedCurrentFailure=1");
    const Behavior editors[] = { OffspotEditor, RawCommandTextEditor };
    for (Behavior behavior : editors) {
        Fixture f;
        setup(&f, behavior); f.delegate_start = behavior == OffspotEditor;
        f.engine.config.explicit_opt_in = false;
        bool original_key_unchanged = begin_release_gate(&f);
        pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
        bool produced = complete_editor_text(&f);
        printf("RELEASE_GATE_CONTROL case=%s rawDispatch=%u originalKeyUnchanged=%u\n",
            behavior == OffspotEditor ? "offspot-editor" : "raw-command-text-editor",
            f.keys, original_key_unchanged);
        // The no-dispatch control must satisfy the identical gate predicate.
        check(release_gate_preserved(&f, original_key_unchanged, produced),
            behavior == OffspotEditor ?
            "CONTROL disabled policy satisfies the offspot text-preservation gate" :
            "CONTROL disabled policy satisfies the raw-command text-entry gate");
        teardown(&f);

        setup(&f, behavior); f.delegate_start = behavior == OffspotEditor;
        original_key_unchanged = begin_release_gate(&f);
        pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
        produced = complete_editor_text(&f);
        printf("RELEASE_GATE_OBSERVED case=%s rawDispatch=%u delegated=%u cancellations=%u "
            "producedResult=%u acceptedResults=%u acceptedImeChars=%u committedDelta=%u "
            "rawCommands=%u subclassRemoved=%u originalKeyUnchanged=%u\n",
            behavior == OffspotEditor ? "offspot-editor" : "raw-command-text-editor",
            f.keys, f.default_updates, f.cancel_calls, produced,
            f.app_results, f.app_imechars, f.committed - 1, f.raw_commands, absent(&f),
            original_key_unchanged);
        // This is deliberately a real failure for today's experiment. Do not
        // invert the assertion or turn the counterexample into an allowlist.
        check(release_gate_preserved(&f, original_key_unchanged, produced),
            behavior == OffspotEditor ?
            "RELEASE_GATE legitimate offspot text must survive delegated preedit" :
            "RELEASE_GATE text entry must not also execute a restored raw-key command");
        teardown(&f);
    }
    BOOL unhooked = UnhookWindowsHookEx(hook);
    printf("RELEASE_GATE_RESULT failures=%u ownHookRemoved=%u hiddenMessageOnlyWindows=1\n",
        failures, unhooked != 0);
    return failures || !unhooked ? 1 : 0;
}

int main(int argc, char **argv) {
    bool run_gate = argc == 2 && !strcmp(argv[1], "--release-gate");
    if (argc != 1 && !run_gate) {
        fputs("usage: inline-preedit-fixture [--release-gate]\n", stderr); return 2;
    }
    WNDCLASSW app = {}; app.lpfnWndProc = AppProc; app.lpszClassName = L"RIUMFixtureApp";
    WNDCLASSW ime = {}; ime.lpfnWndProc = ImeProc; ime.lpszClassName = L"RIUMFixtureIME";
    if (!RegisterClassW(&app) || !RegisterClassW(&ime)) return 90;
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, DispatchHook, nullptr, GetCurrentThreadId());
    if (!hook) return 91;
    if (run_gate) return release_gate(hook);
    Fixture f;
    setup(&f); bool key_ok = begin(&f);
    MSG comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && comp.message == WM_NULL && f.app_updates == 1 && f.interim &&
          !f.cancel_calls && !f.payload_mismatch && absent(&f), "inline app receives genuine preedit exactly once");
    teardown(&f);

    setup(&f, Delegate); key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && f.app_updates == 1 && f.default_updates == 1 && f.cancel_calls == 1 &&
          f.app_cancels == 1 && !f.bad_cancel_phase && !f.wrong_notify && absent(&f),
          "delegated preedit cancels only after app return and subclass removal");
    teardown(&f);

    setup(&f); f.delegate_start = true; key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && f.default_starts == 1 && f.app_updates == 1 && !f.cancel_calls,
          "delegated START does not classify handled composition as rejected"); teardown(&f);

    setup(&f, InsertThenDelegate); key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && f.cancel_calls == 1 && !f.interim && f.committed == 1 && !f.payload_mismatch,
          "cancel removes temporary INSERTCHAR while preserving committed text");
    MSG end = pump(&f, WM_IME_ENDCOMPOSITION, 0, 0);
    MSG result = pump(&f, WM_IME_COMPOSITION, f.expected_character, GCS_RESULTSTR);
    MSG imechar = pump(&f, WM_IME_CHAR, f.expected_character, 1);
    check(end.message == WM_IME_ENDCOMPOSITION && result.message == WM_IME_COMPOSITION &&
          imechar.message == WM_IME_CHAR && f.app_results == 1 && f.app_imechars == 1 &&
          f.committed == 2 && f.cancel_calls == 1, "END before RESULT passes; this is NOT stale-result safety proof"); teardown(&f);

    setup(&f); f.original_vk = 'A';
    MSG nonprobe = pump(&f, WM_KEYDOWN, VK_PROCESSKEY, 1, false);
    check(nonprobe.message == WM_KEYDOWN && nonprobe.wParam == VK_PROCESSKEY && !f.keys,
          "non-probe PROCESSKEY remains untouched");
    f.original_vk = 'V'; f.expected_character = 0x314d; key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && f.engine.stats.probe == 2 && !f.payload_mismatch,
          "V equality is supported without logging character payload"); teardown(&f);

    setup(&f, Delegate); key_ok = begin(&f);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT | GCS_RESULTSTR);
    check(key_ok && comp.message == WM_IME_COMPOSITION && f.app_updates == 1 &&
          f.app_results == 1 && !f.cancel_calls && absent(&f), "mixed result preedit is never intercepted"); teardown(&f);

    setup(&f, Delegate); key_ok = begin(&f);
    MSG canceled = pump(&f, WM_IME_COMPOSITION, 0, 0);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && canceled.message == WM_IME_COMPOSITION && comp.message == WM_IME_COMPOSITION &&
          f.app_cancels == 1 && !f.cancel_calls, "existing cancellation disarms without suppressing later messages"); teardown(&f);

    setup(&f, Delegate); key_ok = begin(&f);
    pump(&f, WM_KEYDOWN, 'A', 1, false);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && comp.message == WM_IME_COMPOSITION && !f.cancel_calls,
          "intervening key aborts association"); teardown(&f);

    const Behavior abort_modes[] = { Reenter, FocusChange, ContextChange, FocusRoundTrip, ResultReenter };
    const char *abort_names[] = { "reentrant app dispatch aborts cancellation", "focus change aborts cancellation",
        "HIMC replacement aborts cancellation", "focus round trip changes epoch and aborts cancellation",
        "nested result is preserved and aborts cancellation" };
    for (UINT i = 0; i < 5; ++i) {
        setup(&f, abort_modes[i]); key_ok = begin(&f);
        pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
        check(key_ok && !f.cancel_calls && f.default_updates == 1 && absent(&f) &&
              (abort_modes[i] != ResultReenter || f.app_results == 1), abort_names[i]); teardown(&f);
    }

    setup(&f, UnmatchedForward); key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && f.default_updates == 1 && !f.cancel_calls && absent(&f),
          "unmatched forwarded parameters remain untouched"); teardown(&f);

    setup(&f, Delegate); f.fail_remove = true; key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    bool retained = !absent(&f);
    SendMessageW(f.ime, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && retained && f.engine.stats.retained_scopes == 1 &&
          f.engine.stats.cleanup_failures == 1 && !f.cancel_calls && f.default_updates == 2,
          "cleanup failure retains valid inert callback and fails open"); teardown(&f);

    setup(&f, Delegate); f.fail_install = true; key_ok = begin(&f);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && comp.message == WM_IME_COMPOSITION && f.app_updates == 1 &&
          f.default_updates == 1 && !f.cancel_calls && absent(&f), "subclass install failure leaves original queue message intact"); teardown(&f);

    setup(&f); MSG peek = message(&f, WM_KEYDOWN, VK_PROCESSKEY, 1);
    InlineProcessQueue(&f.engine, &peek, HC_ACTION, PM_NOREMOVE);
    InlineProcessQueue(&f.engine, &peek, HC_NOREMOVE, PM_REMOVE);
    check(!f.keys && peek.message == WM_KEYDOWN && peek.wParam == VK_PROCESSKEY,
          "only HC_ACTION and PM_REMOVE can mutate or dispatch");
    key_ok = begin(&f); f.clock_offset = 11000;
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && comp.message == WM_IME_COMPOSITION && !f.cancel_calls,
          "deadline expiration leaves messages unchanged"); teardown(&f);

    setup(&f, Delegate);
    pump(&f, WM_KEYDOWN, VK_PROCESSKEY, 1, false);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(f.keys == 1 && comp.message == WM_IME_COMPOSITION && !f.cancel_calls,
          "no fresh START means association unknown and fail open"); teardown(&f);

    setup(&f); key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && comp.message == WM_IME_COMPOSITION && f.app_updates == 2 &&
          f.engine.stats.app_dispatches == 1, "experiment disarms after one intercepted preedit"); teardown(&f);

    const Behavior raw_modes[] = { RawFocusChange, RawContextChange, RawDestroy, RawFocusRoundTrip };
    const char *raw_names[] = { "raw key focus change disarms without changing PROCESSKEY",
        "raw key HIMC change disarms without changing PROCESSKEY",
        "raw key window destruction disarms without accessing stale HWND",
        "raw key focus round trip disarms through epoch guard" };
    for (UINT i = 0; i < 4; ++i) {
        setup(&f, raw_modes[i]);
        MSG raw = message(&f, WM_KEYDOWN, VK_PROCESSKEY, 0x002e0001);
        MSG original_raw = raw;
        InlineProcessQueue(&f.engine, &raw, HC_ACTION, PM_REMOVE);
        comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT, false);
        check(!memcmp(&raw, &original_raw, sizeof(MSG)) && f.keys == 1 &&
            f.engine.phase == InlineDisarmed && comp.message == WM_IME_COMPOSITION &&
            !f.engine.stats.app_dispatches && !f.cancel_calls, raw_names[i]);
        teardown(&f);
    }
    const Behavior after_forward[] = { DelegateThenFocusChange, DelegateThenReenter };
    for (Behavior mode : after_forward) {
        setup(&f, mode); key_ok = begin(&f);
        comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
        check(key_ok && comp.message == WM_NULL && f.default_updates == 1 && !f.cancel_calls && absent(&f),
            mode == DelegateThenFocusChange ? "delegation then focus change preserves default preedit" :
            "delegation then reentry preserves default preedit"); teardown(&f);
    }
    setup(&f, InsertThenDelegate); f.fail_notify = true; key_ok = begin(&f);
    pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && f.cancel_calls == 1 && !f.engine.stats.cancel_successes &&
        f.default_updates == 1 && f.interim && f.committed == 1 && absent(&f),
        "failed cancellation preserves ordinary preedit and remains disarmed"); teardown(&f);

    setup(&f, Delegate); f.install_focus_roundtrip = true; key_ok = begin(&f);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && comp.message == WM_IME_COMPOSITION && f.app_updates == 1 &&
        !f.engine.stats.app_dispatches && !f.cancel_calls && absent(&f),
        "subclass setup reentry revalidates before mutating queue message"); teardown(&f);
    setup(&f, Delegate); f.hold_focus_roundtrip = true; key_ok = begin(&f);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT);
    check(key_ok && comp.message == WM_IME_COMPOSITION && f.app_updates == 1 &&
        !f.engine.stats.app_dispatches && !f.cancel_calls && absent(&f),
        "module hold reentry revalidates before mutating queue message"); teardown(&f);

    setup(&f, ReuseQueueStorage); key_ok = begin(&f);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT, false);
    check(key_ok && f.saw_null_queue && f.app_updates == 1 && comp.message == WM_APP + 99 &&
        comp.wParam == 17 && comp.lParam == 23 && !f.payload_mismatch && absent(&f),
        "queue message is null before dispatch and reused storage is not overwritten afterward"); teardown(&f);

    setup(&f); f.engine.config.explicit_opt_in = false;
    nonprobe = pump(&f, WM_KEYDOWN, VK_PROCESSKEY, 1, false);
    check(!f.keys && nonprobe.wParam == VK_PROCESSKEY && !f.engine.stats.app_dispatches,
        "missing explicit opt-in leaves all input unchanged"); teardown(&f);
    setup(&f, Delegate); key_ok = begin(&f);
    comp = pump(&f, WM_IME_COMPOSITION, f.expected_character, PREEDIT | GCS_RESULTREADSTR);
    check(key_ok && comp.message == WM_IME_COMPOSITION && !f.cancel_calls && !f.engine.stats.app_dispatches,
        "result-reading string flags are excluded from interception"); teardown(&f);
    BOOL unhooked = UnhookWindowsHookEx(hook);
    printf("FIXTURE_RESULT failures=%u ownHookRemoved=%u hiddenMessageOnlyWindows=1\n", failures, unhooked != 0);
    return failures || !unhooked ? 1 : 0;
}
