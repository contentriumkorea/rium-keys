#pragma once
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RIUM_OWNER_UNKNOWN = 0,
    RIUM_OWNER_TEXT = 1,
    RIUM_OWNER_COMMAND = 2
} RiumOwnerKind;

/* Numeric identity only. A recognized provider retains its identity when a
 * sample fails. UNKNOWN never grants ownership of a previously bound target. */
typedef struct {
    uint32_t provider, profile, kind, deferredSafe;
    DWORD processId, threadId;
    HWND focus;
    uintptr_t windowObject, logicalObject;
    uint64_t lifetimeToken;
    uintptr_t textObject;
} RiumOwnerStamp;

typedef void (*RiumOwnerReader)(void *context, RiumOwnerStamp *out);
typedef struct {
    RiumOwnerReader reader;
    void *readerContext;
    RiumOwnerStamp current;
    uint64_t epoch;
    BOOL initialized;
    unsigned runtimeProvider; /* Adopted only at a clean service key boundary. */
} RiumOwnerState;

typedef struct {
    RiumOwnerStamp stamp;
    uint64_t epoch;
} RiumOwnerBinding;

RiumOwnerKind RiumOwner_Observe(RiumOwnerState *state);
void RiumOwner_Capture(RiumOwnerState *state, RiumOwnerBinding *out);
BOOL RiumOwner_AllowsWrite(RiumOwnerState *state, const RiumOwnerBinding *binding, BOOL deferred);

#ifdef __cplusplus
}
#endif
