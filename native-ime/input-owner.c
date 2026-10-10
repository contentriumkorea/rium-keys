#include "input-owner.h"
#include <string.h>

RiumOwnerKind RiumOwner_Observe(RiumOwnerState *state) {
    RiumOwnerStamp next={0};
    HWND focus=GetFocus();
    if (state->reader) state->reader(state->readerContext,&next);
    if (!next.provider && state->current.provider) {
        next.provider=state->current.provider;
        next.profile=state->current.profile;
        next.kind=RIUM_OWNER_UNKNOWN;
    }
    if (!next.provider || !focus || next.focus!=focus ||
        next.processId!=GetCurrentProcessId() || next.threadId!=GetCurrentThreadId() ||
        next.kind>RIUM_OWNER_COMMAND) next.kind=RIUM_OWNER_UNKNOWN;
    if (!state->initialized || memcmp(&state->current,&next,sizeof(next))) {
        ++state->epoch;
        state->current=next;
        state->initialized=TRUE;
    }
    return (RiumOwnerKind)next.kind;
}

void RiumOwner_Capture(RiumOwnerState *state, RiumOwnerBinding *out) {
    RiumOwner_Observe(state);
    out->stamp=state->current;
    out->epoch=state->epoch;
}

BOOL RiumOwner_AllowsWrite(RiumOwnerState *state, const RiumOwnerBinding *binding, BOOL deferred) {
    RiumOwnerKind kind=RiumOwner_Observe(state);
    if (kind==RIUM_OWNER_COMMAND) return FALSE;
    if (!binding->stamp.provider) return !state->current.provider; /* Ordinary TSF only while still unclassified. */
    if (binding->epoch!=state->epoch ||
        memcmp(&binding->stamp,&state->current,sizeof(binding->stamp))) return FALSE;
    // Unsupported input uses provider zero only at a clean runtime boundary.
    // A recognized UNKNOWN sample during a transaction must not start another
    // composition or rebind the old one to a newly discovered text target.
    if (kind==RIUM_OWNER_UNKNOWN) return FALSE;
    return kind==RIUM_OWNER_TEXT && (!deferred || binding->stamp.deferredSafe);
}
