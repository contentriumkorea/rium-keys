#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <imm.h>

// TEMPORARY INLINE-ONLY experiment. Offspot/IME-unaware editors are unsupported.
// Forwarding is not general proof of rejection; queued COMP coverage only.
enum InlinePhase { InlineArmed, InlineAwaitPreedit, InlineDisarmed };
enum InlineEvent { EvRawProbe = 1, EvFreshStart, EvCompDispatch, EvForwardObserved,
    EvCancel, EvAbort, EvSubclassFailure, EvCleanupFailure, EvDone };
enum InlineAbort { AbortNone, AbortContext, AbortEpoch, AbortReentry, AbortDeadline,
    AbortInterveningKey, AbortResult, AbortEnd, AbortNoFreshStart, AbortNoSubclass,
    AbortCleanup, AbortUnmatched };
struct InlineOps {
    void *context;
    bool (*enabled)(void *);
    ULONGLONG (*now)(void *);
    HWND (*focus)(void *);
    HIMC (*get_context)(void *, HWND);
    void (*release_context)(void *, HWND, HIMC);
    HWND (*default_ime)(void *, HWND);
    UINT (*original_vk)(void *, HWND);
    bool (*modifiers)(void *);
    BOOL (*notify)(void *, HIMC, DWORD, DWORD, DWORD);
    BOOL (*install)(void *, HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR);
    BOOL (*remove)(void *, HWND, SUBCLASSPROC, UINT_PTR);
    HMODULE (*hold_module)(void *, const void *);
    void (*release_module)(void *, HMODULE);
    void (*event)(void *, UINT, UINT, DWORD, UINT);
};
struct InlineConfig { DWORD pid, tid; HWND target; ULONGLONG until; bool explicit_opt_in; };
struct InlineStats {
    UINT raw_probes, app_dispatches, observed_delegations, cancel_attempts, cancel_successes;
    UINT cleanup_failures, retained_scopes, abort_reason, probe;
    UINT_PTR last_subclass_id;
};
struct InlineExperiment {
    InlineConfig config;
    InlineOps ops;
    InlinePhase phase;
    InlineStats stats;
    ULONGLONG epoch, probe_epoch, probe_tick;
    DWORD key_time;
    HIMC himc;
    bool fresh_start, processing;
    void *active_scope;
};
void InlineInit(InlineExperiment *, const InlineConfig *, const InlineOps *);
bool InlineProcessQueue(InlineExperiment *, MSG *, int hook_code, WPARAM removal);
void InlineObserveDispatch(InlineExperiment *, HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InlineDefaultSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
