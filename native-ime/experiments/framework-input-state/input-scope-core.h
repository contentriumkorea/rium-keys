#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <msctf.h>
#include <inputscope.h>
#include <wrl/client.h>

// Diagnostic only. InputScope constrains input; it does not establish text intent.
// No text, phrase, regex, XML, selection offsets, or routing verdict is returned.
constexpr UINT kInputScopeCapacity = 16;
struct InputScopeReport {
    HRESULT guardBefore = E_PENDING, guardAfter = E_PENDING;
    HRESULT request = E_PENDING, session = E_PENDING, selection = E_PENDING;
    HRESULT appProperty = E_PENDING, value = E_PENDING, inputScopeQi = E_PENDING;
    HRESULT scopes = E_PENDING, variantClear = E_PENDING, lifetimePin = E_PENDING;
    ULONG fetched = 0, callbacks = 0;
    UINT count = 0, copied = 0;
    VARTYPE variantType = VT_EMPTY;
    BOOL known = FALSE, truncated = FALSE, retainedSession = FALSE;
    LONG numbers[kInputScopeCapacity]{};
};
struct InputScopeGuard {
    ITfThreadMgr* manager = nullptr;
    ITfDocumentMgr* document = nullptr;
    HWND focus = nullptr;
    DWORD pid = 0, tid = 0;
    ULONGLONG deadline = 0;
    // Only the OWN hidden-document fixture may omit native HWND focus validation.
    bool requireWindowFocus = true;
};
InputScopeReport ReadInputScope(ITfContext* context, TfClientId client,
                               const InputScopeGuard& guard);
void PrintInputScope(const InputScopeReport& report);

struct InputScopeActivation {
    HRESULT extended = E_PENDING, flagsBeforeHr = E_PENDING, activate = E_PENDING;
    HRESULT deactivate = E_PENDING, flagsAfterHr = E_PENDING;
    DWORD flagsBefore = 0, requestedFlags = 0, flagsAfter = 0;
};
// report.activate == E_PENDING means inactive/unknown: activation not attempted.
// Windows returns S_FALSE for an already active manager. Successful nested
// activation must always be balanced, including that non-S_OK HRESULT.
HRESULT AcquireInputScopeClient(ITfThreadMgr* manager, TfClientId* client, InputScopeActivation* report);
void ReleaseInputScopeClient(ITfThreadMgr* manager, InputScopeActivation* report);
