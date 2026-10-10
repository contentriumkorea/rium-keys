// Isolated tests of the production owner-binding contract; no window or host calls.
#include "input-owner.h"
#include <stdio.h>
#include <string.h>
static HWND fixtureFocus;
static HWND FixtureGetFocus(void) { return fixtureFocus; }
#define GetFocus FixtureGetFocus
#include "input-owner.c"
static int checks, failures;
static RiumOwnerStamp observed;
static RiumOwnerState state;
static void readOwner(void *context,RiumOwnerStamp *out) { (void)context;*out=observed; }
static void check(BOOL ok,const char *name) { ++checks;if(!ok){++failures;printf("FAIL: %s\n",name);} }
static void setup(unsigned provider,RiumOwnerKind kind) {
    fixtureFocus=(HWND)(uintptr_t)0x100;
    memset(&state,0,sizeof(state));state.reader=readOwner;
    observed=(RiumOwnerStamp){.provider=provider,.profile=provider?1:0,.kind=kind,.deferredSafe=TRUE,
        .processId=GetCurrentProcessId(),.threadId=GetCurrentThreadId(),.focus=fixtureFocus,
        .windowObject=0x200,.logicalObject=0x300,.lifetimeToken=1,.textObject=0x400};
    if(!provider)memset(&observed,0,sizeof(observed));
}
int main(void) {
    RiumOwnerBinding b;
    setup(0,RIUM_OWNER_UNKNOWN);RiumOwner_Capture(&state,&b);
    check(RiumOwner_AllowsWrite(&state,&b,FALSE),"unsupported ordinary TSF synchronous path remains available");
    check(RiumOwner_AllowsWrite(&state,&b,TRUE),"unsupported ordinary TSF keeps existing deferred path");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);
    check(RiumOwner_AllowsWrite(&state,&b,FALSE),"exact current TEXT permits synchronous write");
    check(RiumOwner_AllowsWrite(&state,&b,TRUE),"explicitly deferred-safe TEXT permits captured work");
    observed.deferredSafe=FALSE;RiumOwner_Capture(&state,&b);
    check(RiumOwner_AllowsWrite(&state,&b,FALSE)&&!RiumOwner_AllowsWrite(&state,&b,TRUE),
          "non-deferred framework permits synchronous text only");
    setup(1,RIUM_OWNER_UNKNOWN);RiumOwner_Capture(&state,&b);
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"recognized UNKNOWN cannot start an identity-incomplete synchronous composition");
    check(!RiumOwner_AllowsWrite(&state,&b,TRUE),"recognized UNKNOWN cannot authorize deferred replay even with deferredSafe set");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);observed.kind=RIUM_OWNER_UNKNOWN;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE)&&!RiumOwner_AllowsWrite(&state,&b,TRUE),
          "TEXT to UNKNOWN invalidates previously bound text work");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);memset(&observed,0,sizeof(observed));
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE)&&state.current.provider==1,
          "failed provider read retains recognition and rejects old TEXT binding");
    for(unsigned kind=RIUM_OWNER_UNKNOWN;kind<=RIUM_OWNER_COMMAND;++kind){
        setup(0,RIUM_OWNER_UNKNOWN);RiumOwner_Capture(&state,&b);
        observed=(RiumOwnerStamp){.provider=1,.profile=1,.kind=kind,.deferredSafe=TRUE,
            .processId=GetCurrentProcessId(),.threadId=GetCurrentThreadId(),.focus=fixtureFocus,
            .windowObject=0x200,.logicalObject=0x300,.lifetimeToken=1,.textObject=0x400};
        check(!RiumOwner_AllowsWrite(&state,&b,FALSE)&&!RiumOwner_AllowsWrite(&state,&b,TRUE),
              "provider-zero old work cannot enter a newly recognized provider");
    }
    setup(1,RIUM_OWNER_COMMAND);RiumOwner_Capture(&state,&b);
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE)&&!RiumOwner_AllowsWrite(&state,&b,TRUE),"COMMAND never grants text mutation");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);++observed.logicalObject;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"same HWND logical owner replacement invalidates write");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);++observed.lifetimeToken;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"reused logical address with fresh lifetime token invalidates write");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);++observed.textObject;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"new text session under same node invalidates write");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);
    RiumOwnerStamp original=observed;observed.focus=fixtureFocus=(HWND)(uintptr_t)0x101;RiumOwner_Observe(&state);
    observed=original;fixtureFocus=original.focus;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"observed focus away and back cannot resurrect an old epoch");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);++observed.profile;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"verified framework profile change invalidates old work");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);++observed.threadId;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"foreign thread stamp never authorizes mutation");
    setup(1,RIUM_OWNER_TEXT);RiumOwner_Capture(&state,&b);fixtureFocus=NULL;
    check(!RiumOwner_AllowsWrite(&state,&b,FALSE),"missing native focus invalidates bound mutation");
    printf("Input owner bindings: %d checks, %d failures.\n",checks,failures);return failures?1:0;
}
