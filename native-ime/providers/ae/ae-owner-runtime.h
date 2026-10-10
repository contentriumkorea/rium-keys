#pragma once
#include "../../input-owner.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Exact audited x64 AE modules only. Initialize on a worker outside DllMain
 * and key callbacks. It hashes already-loaded module files once and holds
 * read-only file handles through Shutdown. No host DLL, hook, or worker is
 * created by this adapter. Capture is same focused UI-thread metadata only.
 * The embedding TIP owns code lifetime and serializes its reader with unload. */
BOOL RiumAeOwner_Initialize(void);
void RiumAeOwner_Capture(void *unused,RiumOwnerStamp *out);
void RiumAeOwner_Shutdown(void);
#ifdef __cplusplus
}
#endif
