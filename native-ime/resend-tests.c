// Build with generated actual resend blocks; Win32 delivery is replaced by counters.
#include "third_party/jamotong/src/jamotong.h"
#include "third_party/jamotong/src/transition.h"
#include <stdio.h>
#include <string.h>
static HWND focus=(HWND)(uintptr_t)1;
static HWND FakeFocus(void){return focus;}
#define GetFocus FakeFocus
#include "input-owner.c"
static RiumOwnerStamp observed;
static int posts,sends,queries,kills,checks,failures,changeOnQuery,failTimer;
static UINT_PTR nextTimer=10;
static BOOL WINAPI FakeGui(DWORD t,PGUITHREADINFO g){(void)t;g->hwndFocus=focus;return TRUE;}
static BOOL WINAPI FakeWindow(HWND w){return w!=NULL;}
static DWORD WINAPI FakeThread(HWND w,LPDWORD p){(void)w;*p=GetCurrentProcessId();return GetCurrentThreadId();}
static LRESULT WINAPI FakeMessage(HWND w,UINT m,WPARAM a,LPARAM b){(void)w;(void)m;++queries;*(DWORD*)a=*(DWORD*)b=0;if(changeOnQuery)++observed.logicalObject;return 0;}
static BOOL WINAPI FakePost(HWND w,UINT m,WPARAM a,LPARAM b){(void)w;(void)m;(void)a;(void)b;++posts;return TRUE;}
static UINT_PTR WINAPI FakeTimer(HWND w,UINT_PTR i,UINT ms,TIMERPROC p){(void)w;(void)i;(void)ms;(void)p;return failTimer?0:++nextTimer;}
static BOOL WINAPI FakeKill(HWND w,UINT_PTR i){(void)w;(void)i;++kills;return TRUE;}
#define GetGUIThreadInfo FakeGui
#define GetWindowThreadProcessId FakeThread
#define IsWindow FakeWindow
#define SendMessageW FakeMessage
#define PostMessageW FakePost
#define SetTimer FakeTimer
#define KillTimer FakeKill
static void SendKeyThrough(WPARAM v,LPARAM l){(void)v;(void)l;++sends;}
void JamoDiag(const char *fmt,...){(void)fmt;}
#include "resend-actual.inc"
static ULONG STDMETHODCALLTYPE ref(ITfTextInputProcessor *p){(void)p;return 1;}
static JamoTIPExVtbl tip={.AddRef=ref,.Release=ref};
static void reader(void *p,RiumOwnerStamp *s){(void)p;*s=observed;}
static JamotongTextService svc;
static void reset(void){memset(&svc,0,sizeof svc);svc.lpVtblTIP=&tip;svc.inputOwner.reader=reader;
 observed=(RiumOwnerStamp){.provider=1,.profile=1,.kind=RIUM_OWNER_TEXT,.deferredSafe=TRUE,.processId=GetCurrentProcessId(),.threadId=GetCurrentThreadId(),.focus=focus,.logicalObject=2,.lifetimeToken=3};posts=sends=queries=kills=changeOnQuery=failTimer=0;}
static void check(int x,const char*n){++checks;if(!x){++failures;printf("FAIL: %s\n",n);}}
int main(void){ResendTarget t;UINT_PTR id;
 reset();CaptureResendTarget(&svc,&t);ScheduleKeyResend(&svc,VK_RETURN,1,&t);id=g_pendResendTimer;++observed.logicalObject;ResendTimerProc(NULL,0,id,0);check(!posts&&!sends,"timer never sends to changed logical owner");
 reset();CaptureResendTarget(&svc,&t);++observed.logicalObject;ScheduleKeyResend(&svc,VK_RETURN,1,&t);check(!g_pendResendTimer&&!posts&&!sends,"composition callback change cannot be recaptured as new replay owner");
 reset();CaptureResendTarget(&svc,&t);ScheduleKeyResend(&svc,VK_RETURN,1,&t);changeOnQuery=1;ResendTimerProc(NULL,0,g_pendResendTimer,0);check(queries==1&&!posts&&!sends,"EM_GETSEL reentry revalidated before delivery");
 reset();CaptureResendTarget(&svc,&t);ScheduleKeyResend(&svc,VK_RETURN,1,&t);id=g_pendResendTimer;ResendTimerProc(NULL,0,id-1,0);check(!posts&&!sends&&!kills&&g_pendResendTimer==id,"stale timer cannot kill or deliver current pending key");FlushPendingKeyResend(&svc);check(posts==2&&!sends&&!g_pendResendTimer,"valid flush delivers exactly one balanced pair");
 reset();CaptureResendTarget(&svc,&t);ScheduleKeyResend(&svc,VK_RETURN,1,&t);++observed.logicalObject;FlushPendingKeyResend(&svc);check(!posts&&!sends,"deactivation flush obeys original binding");
 reset();CaptureResendTarget(&svc,&t);ScheduleKeyResend(&svc,VK_RETURN,1,&t);++observed.logicalObject;CaptureResendTarget(&svc,&t);ScheduleKeyResend(&svc,VK_TAB,1,&t);check(!posts&&!sends,"new schedule never flushes obsolete logical owner");FlushPendingKeyResend(&svc);check(posts==2,"replacement reservation delivers only its own pair");
 reset();CaptureResendTarget(&svc,&t);failTimer=1;changeOnQuery=1;ScheduleKeyResend(&svc,VK_RETURN,1,&t);check(!posts&&!sends,"timer allocation failure still rechecks host reentry");
 reset();observed.deferredSafe=FALSE;CaptureResendTarget(&svc,&t);ScheduleKeyResend(&svc,VK_RETURN,1,&t);check(!g_pendResendTimer&&!posts&&!sends,"unproven deferred lifetime never schedules replay");
 printf("Resend owner: %d checks, %d failures.\n",checks,failures);return failures?1:0;}
