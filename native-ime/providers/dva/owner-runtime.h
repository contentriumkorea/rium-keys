#pragma once
#include "../../input-owner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exact reviewed Premiere build only. Call Initialize on a worker, never in
 * DllMain or a key callback. It hashes the current image once and retains the
 * read-only file handle until Shutdown. No worker/hook is created here.
 * Capture implements RiumOwnerReader and must run on the focused GUI thread.
 * It performs bounded metadata reads only, returns deferredSafe=0, and keeps
 * provider/profile identity on unavailable/unsupported samples after init.
 * Shutdown serializes with Capture; the embedding module owns its lifetime. */
BOOL RiumDvaOwner_Initialize(void);
void RiumDvaOwner_Capture(void *unused, RiumOwnerStamp *out);
void RiumDvaOwner_Shutdown(void);

#ifdef __cplusplus
}
#endif
