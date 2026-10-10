#include "inline-preedit-core.h"
// WndProc-dispatched shortcut compatibility only: SendMessage bypasses the
// application's TranslateAccelerator/PreTranslateMessage stages. This is NOT a
// universal routing policy, and is unsupported for offspot/IME-unaware editors.
// Only queued, fresh-start, first preedit is covered. HIMC equality is a guard,
// not proof of text ownership. Later END/RESULT traffic is never filtered.
// The default IME receives every message unchanged. The temporary subclass only
// observes delegation; a canceled/failed attempt must never hide live preedit.
struct Scope {
    InlineExperiment *engine;
    HWND target, ime;
    HIMC himc;
    ULONGLONG epoch;
    HMODULE module;
    UINT_PTR id;
    WPARAM expected_character; // transient equality only; cleared before cleanup
    LPARAM expected_flags;
    bool active, first_app_entry, invalid, forwarded;
};
static void emit(InlineExperiment *e, UINT event, DWORD flags = 0, UINT detail = 0) {
    if (e->ops.event) e->ops.event(e->ops.context, event, e->stats.probe, flags, detail);
}
static bool live(InlineExperiment *e) {
    return e->config.explicit_opt_in && e->config.pid == GetCurrentProcessId() &&
        e->config.tid == GetCurrentThreadId() && e->ops.enabled(e->ops.context) &&
        e->ops.now(e->ops.context) < e->config.until;
}
static bool belongs(InlineExperiment *e, HWND window) {
    DWORD pid = 0;
    return window && IsWindow(window) && GetWindowThreadProcessId(window, &pid) == e->config.tid &&
        pid == e->config.pid;
}
static bool same_context(InlineExperiment *e, HIMC expected) {
    if (!belongs(e, e->config.target) || e->ops.focus(e->ops.context) != e->config.target) return false;
    HIMC current = e->ops.get_context(e->ops.context, e->config.target);
    bool same = current && current == expected;
    if (current) e->ops.release_context(e->ops.context, e->config.target, current);
    return same;
}
static void abort_once(InlineExperiment *e, UINT reason) {
    e->phase = InlineDisarmed;
    e->stats.abort_reason = reason;
    emit(e, EvAbort, 0, reason);
}
static bool same_message(const MSG *a, const MSG *b) {
    return a->hwnd == b->hwnd && a->message == b->message && a->wParam == b->wParam &&
        a->lParam == b->lParam && a->time == b->time &&
        a->pt.x == b->pt.x && a->pt.y == b->pt.y;
}
// A failed removal is an emergency leak, never a dangling callback. Retained
// scopes are permanently inert and keep their module reference until exit.
static bool retire_scope(InlineExperiment *e, Scope *scope, DWORD flags, HMODULE *held) {
    scope->active = false;
    e->active_scope = nullptr;
    scope->expected_character = 0;
    scope->engine = nullptr;
    BOOL removed = e->ops.remove(e->ops.context, scope->ime, InlineDefaultSubclass, scope->id);
    DWORD_PTR remaining = 0;
    bool clean = removed && !GetWindowSubclass(scope->ime, InlineDefaultSubclass, scope->id, &remaining);
    if (!clean) {
        ++e->stats.cleanup_failures; ++e->stats.retained_scopes;
        emit(e, EvCleanupFailure, flags); abort_once(e, AbortCleanup); return false;
    }
    *held = scope->module;
    HeapFree(GetProcessHeap(), 0, scope);
    return true;
}
void InlineInit(InlineExperiment *e, const InlineConfig *c, const InlineOps *o) {
    ZeroMemory(e, sizeof(*e)); e->config = *c; e->ops = *o; e->phase = InlineArmed;
}
void InlineObserveDispatch(InlineExperiment *e, HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (!e || !e->config.explicit_opt_in) return;
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_IME_SETCONTEXT ||
        (window == e->config.target && (message == WM_DESTROY || message == WM_NCDESTROY))) ++e->epoch;
    Scope *scope = (Scope *)e->active_scope;
    if (scope && scope->active) {
        bool exact = message == WM_IME_COMPOSITION && wp == scope->expected_character &&
                     lp == scope->expected_flags;
        if (window == scope->target) {
            if (scope->first_app_entry && exact) scope->first_app_entry = false;
            else { scope->invalid = true; ++e->epoch; }
        } else if (window == scope->ime &&
                   (message == WM_IME_COMPOSITION || message == WM_IME_STARTCOMPOSITION ||
                    message == WM_IME_ENDCOMPOSITION) && !exact) {
            scope->invalid = true; ++e->epoch;
        }
    } else if (e->phase == InlineAwaitPreedit && window == e->config.target) {
        if (message == WM_IME_STARTCOMPOSITION && !e->processing &&
            e->epoch == e->probe_epoch && same_context(e, e->himc)) {
            e->fresh_start = true; emit(e, EvFreshStart);
        }
        if (message == WM_IME_ENDCOMPOSITION) abort_once(e, AbortEnd);
    }
}
LRESULT CALLBACK InlineDefaultSubclass(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                      UINT_PTR, DWORD_PTR reference) {
    Scope *scope = (Scope *)reference;
    // Emergency retained scopes are permanently inert. No map, Engine, or text
    // pointer is dereferenced after normal dispatch ends, even if removal fails.
    if (!scope || !scope->active) return DefSubclassProc(window, message, wp, lp);
    InlineExperiment *e = scope->engine;
    if (window == scope->ime && message == WM_IME_COMPOSITION &&
        wp == scope->expected_character && lp == scope->expected_flags) {
        if (scope->forwarded) { scope->invalid = true; ++e->epoch; }
        if (!scope->invalid && !scope->forwarded && e->epoch == scope->epoch &&
            live(e) && same_context(e, scope->himc)) {
            scope->forwarded = true;
            ++e->stats.observed_delegations;
            emit(e, EvForwardObserved, (DWORD)lp);
        }
    }
    return DefSubclassProc(window, message, wp, lp);
}

bool InlineProcessQueue(InlineExperiment *e, MSG *message, int hook_code, WPARAM removal) {
    if (!e || !message || hook_code != HC_ACTION || removal != PM_REMOVE) return false;
    if (e->processing) {
        ++e->epoch;
        if (e->active_scope) ((Scope *)e->active_scope)->invalid = true;
        return false;
    }
    if (e->phase == InlineDisarmed) return false;
    if (!live(e)) { abort_once(e, AbortDeadline); return false; }
    if (!belongs(e, e->config.target) || e->ops.focus(e->ops.context) != e->config.target) {
        abort_once(e, AbortContext); return false;
    }
    struct Guard { bool &flag; explicit Guard(bool &value) : flag(value) { flag = true; }
                   ~Guard() { flag = false; } } guard(e->processing);

    if (e->phase == InlineArmed) {
        if (message->hwnd != e->config.target || message->message != WM_KEYDOWN ||
            message->wParam != VK_PROCESSKEY || (message->lParam & (1LL << 30)) ||
            e->ops.modifiers(e->ops.context)) return false;
        UINT original = e->ops.original_vk(e->ops.context, message->hwnd);
        if (original != 'C' && original != 'V') return false;
        HIMC context = e->ops.get_context(e->ops.context, message->hwnd);
        if (!context) { abort_once(e, AbortContext); return false; }
        e->himc = context;
        e->ops.release_context(e->ops.context, message->hwnd, context);
        if (!same_context(e, context)) { abort_once(e, AbortContext); return false; }
        e->stats.probe = original == 'C' ? 1 : 2;
        e->probe_epoch = e->epoch;
        e->probe_tick = e->ops.now(e->ops.context);
        e->key_time = message->time;
        e->phase = InlineAwaitPreedit;
        ++e->stats.raw_probes;
        emit(e, EvRawProbe);
        // Preserve the original queue PROCESSKEY in every field. Snapshot the
        // dispatch arguments before a handler can reuse its caller's MSG storage.
        const HWND target = message->hwnd;
        const LPARAM key_flags = message->lParam;
        SendMessageW(target, WM_KEYDOWN, original, key_flags);
        if (!live(e) || !same_context(e, e->himc)) abort_once(e, AbortContext);
        else if (e->epoch != e->probe_epoch) abort_once(e, AbortEpoch);
        return false;
    }
    if (e->epoch != e->probe_epoch) { abort_once(e, AbortEpoch); return false; }
    if (e->ops.now(e->ops.context) > e->probe_tick + 1000) {
        abort_once(e, AbortDeadline); return false;
    }
    if (!same_context(e, e->himc)) { abort_once(e, AbortContext); return false; }
    if (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN) {
        abort_once(e, AbortInterveningKey); return false;
    }
    if (message->hwnd != e->config.target) return false;
    if ((LONG)(message->time - e->key_time) < 0) { abort_once(e, AbortContext); return false; }
    if (message->message == WM_IME_STARTCOMPOSITION) {
        e->fresh_start = true; emit(e, EvFreshStart); return false;
    }
    if (message->message == WM_IME_ENDCOMPOSITION) { abort_once(e, AbortEnd); return false; }
    if (message->message != WM_IME_COMPOSITION) return false;
    DWORD flags = (DWORD)message->lParam;
    const DWORD result_flags = GCS_RESULTSTR | GCS_RESULTREADSTR | GCS_RESULTCLAUSE | GCS_RESULTREADCLAUSE;
    if (flags & result_flags) { abort_once(e, AbortResult); return false; }
    if (!flags) { abort_once(e, AbortEnd); return false; }
    if (!(flags & GCS_COMPSTR)) return false;
    if (!e->fresh_start) { abort_once(e, AbortNoFreshStart); return false; }
    const MSG queued = *message; // Transient genuine payload, never recorded.
    // Disarm before any callback. One preedit attempt, whether it succeeds or not.
    e->phase = InlineDisarmed;
    HWND ime = e->ops.default_ime(e->ops.context, e->config.target);
    if (!belongs(e, ime) || ime == e->config.target) { abort_once(e, AbortContext); return false; }
    Scope *scope = (Scope *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Scope));
    if (!scope) { abort_once(e, AbortNoSubclass); return false; }
    scope->engine = e;
    scope->target = e->config.target; scope->ime = ime; scope->himc = e->himc;
    scope->epoch = e->epoch; scope->expected_character = queued.wParam;
    scope->expected_flags = queued.lParam; scope->id = (UINT_PTR)scope;
    scope->module = e->ops.hold_module(e->ops.context, (const void *)InlineDefaultSubclass);
    e->stats.last_subclass_id = scope->id;
    if (!scope->module || !e->ops.install(e->ops.context, ime, InlineDefaultSubclass,
                                          scope->id, (DWORD_PTR)scope)) {
        if (scope->module) e->ops.release_module(e->ops.context, scope->module);
        HeapFree(GetProcessHeap(), 0, scope);
        emit(e, EvSubclassFailure, flags); abort_once(e, AbortNoSubclass); return false;
    }
    // Setup dependencies may reenter the app. Do not consume its queued message
    // until HWND, focus, HIMC, epoch, default IME and MSG storage all still match.
    if (!live(e) || !same_context(e, e->himc) ||
        e->ops.default_ime(e->ops.context, e->config.target) != ime ||
        e->epoch != e->probe_epoch || !same_message(message, &queued)) {
        HMODULE held = nullptr;
        if (retire_scope(e, scope, flags, &held)) {
            abort_once(e, AbortContext);
            e->ops.release_module(e->ops.context, held);
        }
        return false;
    }
    scope->active = true; scope->first_app_entry = true;
    e->active_scope = scope;
    ++e->stats.app_dispatches;
    emit(e, EvCompDispatch, flags);
    // Consume BEFORE calling the app. Do not write this caller-owned MSG again
    // after SendMessage; a nested message loop may reuse the same storage.
    message->message = WM_NULL;
    // The genuine CS_INSERTCHAR wParam and all flags are passed UNCHANGED.
    SendMessageW(queued.hwnd, queued.message, queued.wParam, queued.lParam);
    scope->active = false;
    e->active_scope = nullptr;
    bool forwarded = scope->forwarded;
    bool invalid = scope->invalid || scope->first_app_entry;
    HMODULE held_module = nullptr;
    if (!retire_scope(e, scope, flags, &held_module)) return true;
    if (forwarded && !invalid && e->epoch == e->probe_epoch && live(e) &&
        same_context(e, e->himc) && e->ops.default_ime(e->ops.context, e->config.target) == ime) {
        HIMC current = e->ops.get_context(e->ops.context, e->config.target);
        if (current && current == e->himc && e->ops.focus(e->ops.context) == e->config.target &&
            e->epoch == e->probe_epoch && live(e)) {
            ++e->stats.cancel_attempts;
            BOOL canceled = e->ops.notify(e->ops.context, current, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            if (canceled) ++e->stats.cancel_successes;
            emit(e, EvCancel, flags, canceled != 0);
        } else abort_once(e, AbortContext);
        if (current) e->ops.release_context(e->ops.context, e->config.target, current);
    } else if (invalid || e->epoch != e->probe_epoch) abort_once(e, AbortReentry);
    else if (forwarded) abort_once(e, AbortContext);
    emit(e, EvDone, flags, forwarded);
    e->ops.release_module(e->ops.context, held_module);
    return true;
}
