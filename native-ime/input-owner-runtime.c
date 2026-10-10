#include "input-owner-runtime.h"
#include <wchar.h>
#include <string.h>

#ifdef _WIN64
#include "providers/ccl/ccl-owner-reader.h"
#include "providers/dva/owner-runtime.h"
#include "providers/ae/ae-owner-runtime.h"
extern LONG g_DllRefCount;
static SRWLOCK runtimeLock=SRWLOCK_INIT;
static LONG runtimeStatus; /* 0 not started, 1 verifying, 2 ready/unsupported */
static unsigned runtimeProvider;
static CclOwnerContext cclContext;

static void Capture(void *context,RiumOwnerStamp *stamp) {
    RiumOwnerState *state=context;memset(stamp,0,sizeof(*stamp));
    if (!state || !state->runtimeProvider) return;
    if (!TryAcquireSRWLockShared(&runtimeLock)) return;
    if (runtimeProvider!=state->runtimeProvider) { ReleaseSRWLockShared(&runtimeLock);return; }
    if (runtimeProvider==1) RiumDvaOwner_Capture(NULL,stamp);
    else if (runtimeProvider==2) CclOwnerCapture(&cclContext,stamp);
    else if (runtimeProvider==3) RiumAeOwner_Capture(NULL,stamp);
    ReleaseSRWLockShared(&runtimeLock);
}

RiumOwnerKind RiumOwnerRuntime_AtBoundary(RiumOwnerState *state,BOOL clean) {
    if (!state) return RIUM_OWNER_UNKNOWN;
    if (state->reader==Capture && clean && TryAcquireSRWLockShared(&runtimeLock)) {
        state->runtimeProvider=runtimeProvider;
        ReleaseSRWLockShared(&runtimeLock);
    }
    RiumOwnerKind kind=RiumOwner_Observe(state);
    if (state->reader==Capture && clean && kind==RIUM_OWNER_UNKNOWN && state->current.provider) {
        // No composition, pending work or callback is active at this boundary.
        // Keep unsupported input on ordinary TSF until it finishes, rather than
        // create an UNKNOWN-bound composition that a later TEXT sample strands.
        // Old bindings remain invalid; this is never a busy-input fallback.
        state->runtimeProvider=0;
        memset(&state->current,0,sizeof(state->current));
        ++state->epoch;
        state->initialized=TRUE;
    }
    return kind;
}

typedef struct { HMODULE module;unsigned provider; } RuntimeWorker;
static DWORD WINAPI Verify(void *arg) {
    RuntimeWorker work=*(RuntimeWorker*)arg;
    HeapFree(GetProcessHeap(),0,arg);
    unsigned verified=0;
    if (work.provider==1 && RiumDvaOwner_Initialize()) verified=1;
    if (work.provider==2 && CclOwnerInitialize(&cclContext)==CCL_REASON_OK) verified=2;
    if (work.provider==3 && RiumAeOwner_Initialize()) verified=3;
    AcquireSRWLockExclusive(&runtimeLock);
    runtimeProvider=verified;
    InterlockedExchange(&runtimeStatus,2);
    ReleaseSRWLockExclusive(&runtimeLock);
    InterlockedDecrement(&g_DllRefCount);
    FreeLibraryAndExitThread(work.module,0);
    return 0;
}

static unsigned CandidateProvider(void) {
    wchar_t path[MAX_PATH];DWORD length=GetModuleFileNameW(NULL,path,MAX_PATH);
    if (!length || length>=MAX_PATH) return 0;
    const wchar_t *name=wcsrchr(path,L'\\');name=name?name+1:path;
    if (!_wcsicmp(name,L"Adobe Premiere Pro.exe")) return 1;
    if (!_wcsicmp(name,L"Studio One.exe") && GetModuleHandleW(L"cclgui.dll")) return 2;
    if (!_wcsicmp(name,L"AfterFX.exe")) return 3;
    return 0;
}

void RiumOwnerRuntime_Attach(RiumOwnerState *state) {
    state->reader=Capture;state->readerContext=state;
    unsigned candidate=CandidateProvider();
    if (!candidate || !TryAcquireSRWLockExclusive(&runtimeLock)) return;
    BOOL admitted=InterlockedCompareExchange(&runtimeStatus,1,0)==0;
    ReleaseSRWLockExclusive(&runtimeLock);
    if (!admitted) return;
    RuntimeWorker *work=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*work));
    HMODULE module=NULL;
    if (!work || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(uintptr_t)&RiumOwnerRuntime_Attach,&module)) {
        if(work)HeapFree(GetProcessHeap(),0,work);
        InterlockedExchange(&runtimeStatus,0);return;
    }
    work->module=module;work->provider=candidate;
    InterlockedIncrement(&g_DllRefCount);
    HANDLE thread=CreateThread(NULL,0,Verify,work,0,NULL);
    if (thread) CloseHandle(thread);
    else {
        HeapFree(GetProcessHeap(),0,work);
        InterlockedExchange(&runtimeStatus,0);
        InterlockedDecrement(&g_DllRefCount);
        FreeLibrary(module);
    }
}

BOOL RiumOwnerRuntime_ReleaseUnused(void) {
    if (InterlockedCompareExchange(&runtimeStatus,0,0)==1 ||
        !TryAcquireSRWLockExclusive(&runtimeLock)) return FALSE;
    if (runtimeStatus==1) { ReleaseSRWLockExclusive(&runtimeLock);return FALSE; }
    runtimeProvider=0;
    RiumDvaOwner_Shutdown();CclOwnerShutdown(&cclContext);RiumAeOwner_Shutdown();
    InterlockedExchange(&runtimeStatus,0);
    ReleaseSRWLockExclusive(&runtimeLock);
    return TRUE;
}
#else
void RiumOwnerRuntime_Attach(RiumOwnerState *state) { (void)state; }
RiumOwnerKind RiumOwnerRuntime_AtBoundary(RiumOwnerState *state,BOOL clean) {
    (void)clean;return state?RiumOwner_Observe(state):RIUM_OWNER_UNKNOWN;
}
BOOL RiumOwnerRuntime_ReleaseUnused(void) { return TRUE; }
#endif
