#pragma once
#include "input-owner.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Activation starts verification on a worker. No disk work occurs in a key
 * callback. Unsupported processes retain the ordinary TSF route. */
void RiumOwnerRuntime_Attach(RiumOwnerState *state);
/* Observe once before key processing, never from an owner read or host
 * callback. A clean unsupported input uses ordinary TSF for its composition;
 * a busy transaction never changes providers or its original binding. */
RiumOwnerKind RiumOwnerRuntime_AtBoundary(RiumOwnerState *state, BOOL clean);
/* Called from DllCanUnloadNow, never DllMain. FALSE means a worker/capture owns
 * the module; TRUE has released provider resources outside the loader lock. */
BOOL RiumOwnerRuntime_ReleaseUnused(void);
#ifdef __cplusplus
}
#endif
