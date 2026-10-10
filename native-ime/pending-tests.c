#include "third_party/jamotong/src/jamotong.h"
#include "third_party/jamotong/src/transition.h"
#include <stdio.h>
static JamotongTextService svc;
static ITfContext oldCtx,newCtx;
static int oldRefs,newRefs,mode,edits,checks,failures;
static void installNewTarget(void);
static HWND WINAPI FakeFocus(void){return (HWND)1;}
#define GetFocus FakeFocus
void RiumOwner_Capture(RiumOwnerState*s,RiumOwnerBinding*b){*b=(RiumOwnerBinding){.stamp=s->current,.epoch=s->epoch};}
BOOL RiumOwner_AllowsWrite(RiumOwnerState*s,const RiumOwnerBinding*b,BOOL deferred){(void)deferred;return b->epoch==s->epoch&&!memcmp(&b->stamp,&s->current,sizeof b->stamp);}
HWND EditCtl_FocusEditWindow(void){if(mode==10)installNewTarget();return (HWND)1;}
static BOOL WINAPI FakeWindow(HWND h){return h!=NULL;}
static DWORD WINAPI FakeThread(HWND h,LPDWORD p){(void)h;if(p)*p=GetCurrentProcessId();return GetCurrentThreadId();}
#define IsWindow FakeWindow
#define GetWindowThreadProcessId FakeThread
static ULONG STDMETHODCALLTYPE add(ITfContext*p){ULONG refs=p==&oldCtx?++oldRefs:++newRefs;if(mode==9&&p==&oldCtx)installNewTarget();return refs;}
static ULONG STDMETHODCALLTYPE release(ITfContext*p){return p==&oldCtx?--oldRefs:--newRefs;}
static ITfContextVtbl vt={.AddRef=add,.Release=release};
BOOL JamoComp_IsActive(const JamotongTextService *s){return s->pComposition!=NULL;}
void JamoDiag(const char *fmt,...){(void)fmt;}
void Jamotong_PendingClear(JamotongTextService *s);
static void installNewTarget(void){
 ITfContext *old=svc.compTargetCtx;
 svc.compTargetCtx=&newCtx;++newRefs;svc.compTargetHwnd=(HWND)2;svc.compTargetFocusHwnd=(HWND)2;
 svc.compTargetOwner.stamp.logicalObject=22;
 if(old)old->lpVtbl->Release(old);
}
bool EditCtl_ReplaceSelectionOwned(HWND h,const wchar_t *text,JamotongTextService *s,const RiumOwnerBinding*b,BOOL deferred){
 (void)h;(void)text;(void)b;(void)deferred;++edits;
 if(mode==5 && !s->cpPendingInFlight)++edits; // Same gate used by RetryPendingAtOwnedFocus.
 if(mode==1||mode==6){installNewTarget();if(mode==6)Fsm_ProcessKey(&svc.fsm,L's',0,NULL);}
 if(mode==2||mode==3){Jamotong_PendingClear(s);s->cpPendingCommit=L'Z';s->cpPendingCtx=&newCtx;++newRefs;s->cpPendingFocusHwnd=(HWND)2;s->pendingOwner.stamp.logicalObject=22;++s->cpPendingGeneration;}
 return mode==3||mode==4;
}
typedef enum { TRANS_WHY_FOCUS,TRANS_WHY_KEY,TRANS_WHY_EXTERNAL } TransWhy;
static void ResetComposition(JamotongTextService *s);
static void Jamotong_ChordTimerCancel(JamotongTextService*s){(void)s;}
void ChordKb_ReleaseAll(ChordKbContext*c){(void)c;if(mode==7){installNewTarget();Fsm_Init(&svc.fsm);Fsm_ProcessKey(&svc.fsm,L's',0,NULL);}}
void SeqKb_Init(SeqState*s){memset(s,0,sizeof *s);}
void CodeInput_Hide(void){}
bool CandidateUI_IsVisible(void){return false;}
void CandidateUI_Cancel(void){}
static void UiCodeHide(JamotongTextService*s){(void)s;}
static void UiCandHide(JamotongTextService*s){(void)s;}
LayoutConfig* Config_GetCurrentLayout(JamotongConfig*c){(void)c;return NULL;}
SeqResult SeqKb_Flush(SeqState*s,const SeqLayout*l){(void)s;(void)l;return (SeqResult){0};}
static void SeqApply(JamotongTextService*s,ITfContext*c,const SeqResult*r){(void)s;(void)c;(void)r;}
static bool OutputResultSeq(JamotongTextService*s,ITfContext*c,FsmResult r,BOOL f){(void)s;(void)c;(void)r;(void)f;return true;}
static void Transition_FlushSeq(JamotongTextService*s,const char*w){(void)s;(void)w;}
HRESULT JamoComp_Finalize(JamotongTextService*s){if(mode==8){s->pComposition=(ITfComposition*)(uintptr_t)2;installNewTarget();Fsm_Init(&s->fsm);Fsm_ProcessKey(&s->fsm,L's',0,NULL);}return S_OK;}
void Jamotong_ClearCompositionState(JamotongTextService*s){Fsm_Init(&s->fsm);if(s->compTargetCtx)s->compTargetCtx->lpVtbl->Release(s->compTargetCtx);s->compTargetCtx=NULL;}
#include "pending-actual.inc"
static void check(int ok,const char*n){++checks;if(!ok){++failures;printf("FAIL: %s\n",n);}}
static void cleanup(void){Jamotong_PendingClear(&svc);if(svc.compTargetCtx){svc.compTargetCtx->lpVtbl->Release(svc.compTargetCtx);svc.compTargetCtx=NULL;}}
static void setup(void){ZeroMemory(&svc,sizeof svc);oldCtx.lpVtbl=newCtx.lpVtbl=&vt;oldRefs=1;newRefs=0;mode=edits=0;svc.compTargetCtx=&oldCtx;svc.compTargetHwnd=svc.compTargetFocusHwnd=(HWND)1;svc.compTargetOwner.stamp.logicalObject=11;Fsm_Init(&svc.fsm);Fsm_ProcessKey(&svc.fsm,L'r',0,NULL);}
int main(void){
 for(int scenario=9;scenario<=10;++scenario){
 setup();cleanup();mode=scenario;CompTarget_Remember(&svc,&oldCtx);
 check(svc.compTargetCtx==&newCtx&&svc.compTargetHwnd==(HWND)2&&svc.compTargetFocusHwnd==(HWND)2&&svc.compTargetOwner.stamp.logicalObject==22,
       scenario==9?"Remember AddRef callback keeps replacement complete tuple":"Remember EDIT callback keeps replacement complete tuple");
 check(oldRefs==0&&newRefs==1,"Remember abandoned retention released without losing replacement reference");
 cleanup();check(!oldRefs&&!newRefs,"Remember callback replacement cleanup balances references");
 }
 setup();cleanup();CompTarget_Remember(&svc,&oldCtx);
 check(svc.compTargetCtx==&oldCtx&&svc.compTargetHwnd==(HWND)1&&svc.compTargetFocusHwnd==(HWND)1&&oldRefs==1,"Remember normal publication retains exactly one reference");cleanup();check(!oldRefs&&!newRefs,"Remember normal cleanup balances references");
 setup();mode=1;Transition_FlushComposition(&svc,"test");
 check(svc.cpPendingCommit&&svc.cpPendingCtx==&oldCtx&&svc.cpPendingFocusHwnd==(HWND)1&&svc.pendingOwner.stamp.logicalObject==11,"failed host write preserves original tuple despite replacement target");
 check(svc.compTargetCtx==&newCtx&&svc.compTargetOwner.stamp.logicalObject==22,"new composition target survives old pending publication");cleanup();check(!oldRefs&&!newRefs,"target replacement references balanced");
 setup();svc.cpPendingCommit=L'Z';svc.cpPendingCtx=&newCtx;++newRefs;
 wchar_t before=Fsm_PeekPreedit(&svc.fsm);Transition_FlushComposition(&svc,"occupied");
 check(svc.cpPendingCommit==L'Z'&&Fsm_PeekPreedit(&svc.fsm)==before&&!edits,"occupied pending slot never flushes or loses current FSM text");cleanup();check(!oldRefs&&!newRefs,"occupied slot reference ownership balanced");
 for(mode=2;mode<=3;){int scenario=mode;setup();mode=scenario;Transition_FlushComposition(&svc,"reentry");
 check(svc.cpPendingCommit==L'Z'&&svc.cpPendingCtx==&newCtx&&svc.pendingOwner.stamp.logicalObject==22,"reentrant replacement pending survives old success or failure");
 cleanup();check(!oldRefs&&!newRefs,"reentrant replacement reference ownership balanced");mode=scenario+1;}
 setup();mode=4;Transition_FlushComposition(&svc,"success");check(!svc.cpPendingCommit&&!svc.compTargetCtx&&oldRefs==0,"successful original write releases transferred context exactly once");cleanup();
 setup();Transition_FlushComposition(&svc,"failure");check(svc.cpPendingCtx==&oldCtx&&oldRefs==1&&!svc.compTargetCtx,"failed write owns one transferred context with no AddRef");cleanup();check(!oldRefs&&!newRefs,"failure cleanup releases transferred reference");
 setup();mode=5;Transition_FlushComposition(&svc,"retry-reentry");check(edits==1&&!svc.cpPendingInFlight,"host insertion cannot recursively retry its published pending slot");cleanup();
 setup();svc.cpPendingCommit=L'Z';svc.cpPendingCtx=&newCtx;++newRefs;before=Fsm_PeekPreedit(&svc.fsm);Jamotong_Transition(&svc,TRANS_WHY_FOCUS,NULL);check(Fsm_PeekPreedit(&svc.fsm)==before&&svc.compTargetCtx==&oldCtx,"actual transition preserves FSM behind occupied pending slot");cleanup();
 setup();mode=6;Jamotong_Transition(&svc,TRANS_WHY_FOCUS,NULL);check(svc.fsm.state!=STATE_EMPTY&&svc.compTargetCtx==&newCtx,"outer transition does not fold callback's new target and syllable");cleanup();
 setup();mode=7;Jamotong_FoldInput(&svc);check(svc.fsm.state!=STATE_EMPTY&&svc.compTargetCtx==&newCtx,"fold callback cannot reset newly published input");cleanup();
 setup();svc.pComposition=(ITfComposition*)(uintptr_t)1;mode=8;ResetComposition(&svc);check(svc.pComposition==(ITfComposition*)(uintptr_t)2&&svc.fsm.state!=STATE_EMPTY&&svc.compTargetCtx==&newCtx,"finalization replacement is not reset after S_OK");cleanup();
 printf("Pending transfer: %d checks, %d failures.\n",checks,failures);return failures?1:0;}
