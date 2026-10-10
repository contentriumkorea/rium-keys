// Actual runtime aggregator with deterministic OS/provider scheduling. No threads or DLL operations occur.
#include "input-owner-runtime.h"
#include "providers/ccl/ccl-owner-reader.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static HWND FixtureFocus(void){return (HWND)(uintptr_t)0x100;}
#define GetFocus FixtureFocus
#include "input-owner.c"
#ifdef _WIN64
BOOL RiumDvaOwner_Initialize(void);
void RiumDvaOwner_Capture(void*,RiumOwnerStamp*);
void RiumDvaOwner_Shutdown(void);
BOOL RiumAeOwner_Initialize(void);
void RiumAeOwner_Capture(void*,RiumOwnerStamp*);
void RiumAeOwner_Shutdown(void);
LONG g_DllRefCount;
static int lockHeld,starts,unloads,closes,captures,initializes,shutdowns,initOk=1,startOk=1,raceAttach;
static int aeInitializes,aeCaptures,aeShutdowns;
static const WCHAR *candidatePath=L"C:\\Adobe Premiere Pro.exe";
static RiumOwnerKind captureKind=RIUM_OWNER_TEXT;
static LPTHREAD_START_ROUTINE worker;
static void *workerArg;
static RiumOwnerState state,second;
static BOOLEAN WINAPI FakeTryShared(PSRWLOCK l){(void)l;if(lockHeld)return FALSE;lockHeld=1;return TRUE;}
static BOOLEAN WINAPI FakeTryExclusive(PSRWLOCK l){return FakeTryShared(l);}
static void WINAPI FakeAcquire(PSRWLOCK l){(void)l;if(lockHeld){puts("FAIL: blocking lock acquired under contention");}lockHeld=1;}
static void WINAPI FakeRelease(PSRWLOCK l){(void)l;lockHeld=0;}
static DWORD WINAPI FakeFilename(HMODULE m,LPWSTR p,DWORD n){(void)m;const WCHAR*t=candidatePath;if(n<=wcslen(t))return 0;wcscpy(p,t);return (DWORD)wcslen(t);}
static BOOL WINAPI FakeModule(DWORD f,LPCWSTR p,HMODULE*out){(void)f;(void)p;*out=(HMODULE)1;return TRUE;}
static HANDLE WINAPI FakeThread(LPSECURITY_ATTRIBUTES a,SIZE_T s,LPTHREAD_START_ROUTINE p,LPVOID arg,DWORD f,LPDWORD id){(void)a;(void)s;(void)f;(void)id;++starts;if(!startOk)return NULL;worker=p;workerArg=arg;return (HANDLE)2;}
static BOOL WINAPI FakeClose(HANDLE h){(void)h;++closes;return TRUE;}
static BOOL WINAPI FakeFree(HMODULE m){(void)m;++unloads;return TRUE;}
static void WINAPI FakeExit(HMODULE m,DWORD code){(void)code;FakeFree(m);}
#define TryAcquireSRWLockShared FakeTryShared
#define TryAcquireSRWLockExclusive FakeTryExclusive
#define AcquireSRWLockExclusive FakeAcquire
#define ReleaseSRWLockShared FakeRelease
#define ReleaseSRWLockExclusive FakeRelease
#define GetModuleFileNameW FakeFilename
#define GetModuleHandleExW FakeModule
#define CreateThread FakeThread
#define CloseHandle FakeClose
#define FreeLibrary FakeFree
#define FreeLibraryAndExitThread FakeExit
#include "runtime-actual.inc"
BOOL RiumDvaOwner_Initialize(void){++initializes;return initOk;}
void RiumDvaOwner_Capture(void*p,RiumOwnerStamp*s){(void)p;++captures;s->provider=1;s->kind=captureKind;s->processId=GetCurrentProcessId();s->threadId=GetCurrentThreadId();s->focus=GetFocus();s->logicalObject=0x200;s->lifetimeToken=1;}
void RiumDvaOwner_Shutdown(void){++shutdowns;if(raceAttach){raceAttach=0;RiumOwnerRuntime_Attach(&second);}}
BOOL RiumAeOwner_Initialize(void){++aeInitializes;return initOk;}
void RiumAeOwner_Capture(void*p,RiumOwnerStamp*s){(void)p;++aeCaptures;s->provider=3;s->profile=1;s->kind=captureKind;s->processId=GetCurrentProcessId();s->threadId=GetCurrentThreadId();s->focus=GetFocus();s->logicalObject=0x300;s->lifetimeToken=3;}
void RiumAeOwner_Shutdown(void){++aeShutdowns;}
uint32_t CclOwnerInitialize(CclOwnerContext*c){(void)c;++initializes;return initOk?CCL_REASON_OK:CCL_REASON_BUILD_MISMATCH;}
void CclOwnerCapture(void*c,RiumOwnerStamp*s){(void)c;++captures;s->provider=2;}
void CclOwnerShutdown(CclOwnerContext*c){memset(c,0,sizeof *c);}
static int checks,failures;
static void check(int ok,const char*n){++checks;if(!ok){++failures;printf("FAIL: %s\n",n);}}
static void runWorker(void){LPTHREAD_START_ROUTINE f=worker;void*a=workerArg;worker=NULL;workerArg=NULL;f(a);}
static void reset(void){runtimeStatus=runtimeProvider=0;lockHeld=starts=unloads=closes=captures=initializes=shutdowns=raceAttach=0;aeInitializes=aeCaptures=aeShutdowns=0;candidatePath=L"C:\\Adobe Premiere Pro.exe";g_DllRefCount=0;initOk=startOk=1;captureKind=RIUM_OWNER_TEXT;worker=NULL;memset(&state,0,sizeof state);memset(&second,0,sizeof second);}
int main(void){RiumOwnerStamp stamp;
 reset();RiumOwnerRuntime_Attach(&state);RiumOwnerRuntime_Attach(&second);check(starts==1&&g_DllRefCount==1,"activation starts exactly one retained verification worker");state.reader(state.readerContext,&stamp);check(!stamp.provider&&!captures,"verification in progress never exposes unverified provider");check(!RiumOwnerRuntime_ReleaseUnused()&&!shutdowns,"unload refuses while verification owns module");runWorker();check(g_DllRefCount==0&&unloads==1&&initializes==1,"worker completion balances module and COM unload accounting");RiumOwnerRuntime_AtBoundary(&state,TRUE);state.reader(state.readerContext,&stamp);check(stamp.provider==1&&captures==2,"only successful initialization publishes provider");lockHeld=1;state.reader(state.readerContext,&stamp);check(!stamp.provider&&captures==2,"contended hot capture returns without waiting or provider calls");lockHeld=0;check(RiumOwnerRuntime_ReleaseUnused()&&runtimeProvider==0&&runtimeStatus==0,"idle shutdown clears readiness and permits future verification");
 reset();initOk=0;RiumOwnerRuntime_Attach(&state);runWorker();state.reader(state.readerContext,&stamp);check(!stamp.provider&&!captures,"failed initialization retains unsupported ordinary TSF");
 reset();startOk=0;RiumOwnerRuntime_Attach(&state);check(!g_DllRefCount&&unloads==1&&runtimeStatus==0,"thread creation failure releases module and permits retry");
 reset();raceAttach=1;check(RiumOwnerRuntime_ReleaseUnused(),"idle cleanup completes");check(!worker&&starts==0&&runtimeStatus==0,"cleanup cannot admit an initializing worker inside shutdown critical section");if(worker)runWorker();
 reset();RiumOwnerRuntime_Attach(&state);runWorker();state.reader(state.readerContext,&stamp);
 check(!stamp.provider,"worker becoming ready during baseline composition cannot automatically adopt provider");
 RiumOwnerRuntime_AtBoundary(&state,FALSE);state.reader(state.readerContext,&stamp);
 check(!stamp.provider,"busy next key keeps baseline provider until its composition ends");
 RiumOwnerRuntime_AtBoundary(&state,TRUE);state.reader(state.readerContext,&stamp);
 check(stamp.provider==1,"first clean boundary adopts already verified provider");
 RiumOwnerRuntime_AtBoundary(&state,FALSE);state.reader(state.readerContext,&stamp);
 check(stamp.provider==1,"recognized composition keeps its provider on later busy keys");
 reset();RiumOwnerRuntime_Attach(&state);RiumOwnerRuntime_AtBoundary(&state,TRUE);runWorker();state.reader(state.readerContext,&stamp);
 check(!stamp.provider,"clean boundary while worker pending does not preauthorize mid-composition adoption");
 reset();RiumOwnerRuntime_Attach(&state);RiumOwnerBinding originalWork;RiumOwner_Capture(&state,&originalWork);runWorker();
 RiumOwnerRuntime_AtBoundary(&state,FALSE);
 check(RiumOwner_AllowsWrite(&state,&originalWork,FALSE),"actual bound baseline write survives worker readiness during composition");
 RiumOwnerRuntime_AtBoundary(&state,TRUE);
 check(!RiumOwner_AllowsWrite(&state,&originalWork,FALSE),"clean adoption still rejects replay of old provider-zero work");
 reset();RiumOwnerRuntime_Attach(&state);runWorker();captureKind=RIUM_OWNER_UNKNOWN;
 RiumOwnerRuntime_AtBoundary(&state,TRUE);RiumOwnerBinding unknownStart;RiumOwner_Capture(&state,&unknownStart);
 check(!state.runtimeProvider&&!unknownStart.stamp.provider&&RiumOwner_AllowsWrite(&state,&unknownStart,FALSE),
       "unsupported clean input starts ordinary TSF instead of an identity-incomplete provider composition");
 captureKind=RIUM_OWNER_TEXT;RiumOwnerRuntime_AtBoundary(&state,FALSE);
 check(!state.runtimeProvider&&RiumOwner_AllowsWrite(&state,&unknownStart,FALSE),
       "later positive TEXT cannot strand a baseline composition that started unsupported");
 RiumOwnerRuntime_AtBoundary(&state,TRUE);RiumOwnerBinding knownStart;RiumOwner_Capture(&state,&knownStart);
 check(state.runtimeProvider==1&&knownStart.stamp.kind==RIUM_OWNER_TEXT,
       "supported input adopts the verified provider only after ordinary composition finishes");
 captureKind=RIUM_OWNER_UNKNOWN;RiumOwnerRuntime_AtBoundary(&state,FALSE);RiumOwnerBinding failedSample;RiumOwner_Capture(&state,&failedSample);
 check(state.runtimeProvider==1&&!RiumOwner_AllowsWrite(&state,&knownStart,FALSE)&&
       !RiumOwner_AllowsWrite(&state,&failedSample,FALSE),
       "unknown during bound input cannot fall back or create a fresh unowned write");
 RiumOwnerRuntime_AtBoundary(&state,TRUE);RiumOwner_Capture(&state,&unknownStart);
 check(!state.runtimeProvider&&!unknownStart.stamp.provider&&RiumOwner_AllowsWrite(&state,&unknownStart,FALSE)&&
       !RiumOwner_AllowsWrite(&state,&knownStart,FALSE),
       "completed provider input may use baseline for unsupported next field without replaying its old binding");
 reset();candidatePath=L"C:\\Adobe\\After Effects\\AfterFX.exe";RiumOwnerRuntime_Attach(&state);
 check(starts==1&&g_DllRefCount==1&&!aeInitializes,"AE candidate verification stays off the activation thread");
 if(worker)runWorker();RiumOwnerRuntime_AtBoundary(&state,TRUE);
 check(aeInitializes==1&&!initializes&&state.current.provider==3&&aeCaptures==1,
       "AE worker publishes only its own provider and captures at a clean boundary");
 check(RiumOwnerRuntime_ReleaseUnused()&&aeShutdowns==1&&!runtimeProvider,
       "AE verified resources are released during idle unload");
 reset();candidatePath=L"C:\\AfterFX.exe";initOk=0;RiumOwnerRuntime_Attach(&state);
 if(worker)runWorker();RiumOwnerRuntime_AtBoundary(&state,TRUE);
 check(aeInitializes==1&&!state.current.provider&&!aeCaptures,
       "AE fingerprint rejection keeps the ordinary TSF route without capture");
 printf("Runtime lifecycle: %d checks, %d failures.\n",checks,failures);return failures?1:0;}

#else
#include "runtime-actual.inc"
static int observations;
static void sentinel(void *p,RiumOwnerStamp *s){(void)p;(void)s;++observations;}
int main(void){
 int checks=0,failures=0;
 RiumOwnerState state={0};state.reader=sentinel;state.readerContext=(void*)(uintptr_t)1;
 state.epoch=7;state.current.provider=9;
 RiumOwnerState original=state;
 RiumOwnerRuntime_Attach(&state);++checks;
 if(memcmp(&state,&original,sizeof state)){++failures;puts("FAIL: x86 attach must preserve state and existing reader");}
 ++checks;if(RiumOwnerRuntime_AtBoundary(&state,TRUE)!=RIUM_OWNER_UNKNOWN||observations!=1||state.reader!=sentinel||state.runtimeProvider){++failures;puts("FAIL: x86 boundary observes ordinary ownership without adopting providers");}
 RiumOwnerRuntime_Attach(NULL);++checks; // Actual no-op accepts no state and cannot start a worker.
 if(!RiumOwnerRuntime_ReleaseUnused()){++failures;puts("FAIL: x86 release is immediately available");}
 ++checks;if(!RiumOwnerRuntime_ReleaseUnused()){++failures;puts("FAIL: x86 repeated release remains a no-op");}
 printf("Runtime x86 ordinary route: %d checks, %d failures.\n",checks,failures);return failures?1:0;
}
#endif
