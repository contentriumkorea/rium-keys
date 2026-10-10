#ifndef CCL_OWNER_READER_H
#define CCL_OWNER_READER_H
#include <stdint.h>
#include "../../input-owner.h"
#if defined(_WIN32) && defined(CCL_OWNER_EXPORTS)
#define CCL_OWNER_API __declspec(dllexport)
#else
#define CCL_OWNER_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

enum CclOwnerDecision { CCL_OWNER_UNKNOWN = 0, CCL_OWNER_TEXT = 1, CCL_OWNER_COMMAND = 2 };
enum CclOwnerReason {
    CCL_REASON_OK = 0, CCL_REASON_NOT_INITIALIZED, CCL_REASON_BUILD_MISMATCH,
    CCL_REASON_WRONG_THREAD_OR_PROCESS, CCL_REASON_WINDOW_STATE,
    CCL_REASON_UNSUPPORTED_FOCUS, CCL_REASON_READ_FAILED,
    CCL_REASON_OWNER_MISMATCH, CCL_REASON_EDIT_MODE_MISMATCH,
    CCL_REASON_MOUSE_HANDLER, CCL_REASON_UNKNOWN_GLOBAL_HANDLER,
    CCL_REASON_HANDLER_LIMIT, CCL_REASON_CHANGED_DURING_READ
};
enum { CCL_OWNER_VERSION = 1, CCL_OWNER_MAX_HANDLERS = 32 };
/* Initialize outside DllMain. No module is loaded, activated, hooked, or retained.
   This x64 prototype accepts only the two exact audited, already-loaded builds. */
typedef struct CclOwnerContext {
    uint32_t size, version, initialized, process_id;
    uint64_t ccl_base, exe_base;
} CclOwnerContext;
/* Numeric observation only. Pointer equality is NOT a generation or weak ref.
   The result is valid only for immediate use in this same UI-thread callback.
   COMMAND applies to unmodified A-Z KEYDOWN; no key is observed by this API.
   Never use this token to authorize a delayed TSF edit/commit. */
typedef struct CclOwnerResult {
    uint32_t size, version, decision, reason;
    uint32_t process_id, thread_id, global_handler_count, immediate_only;
    uint64_t foreground_hwnd, focus_hwnd, window_hwnd;
    uint64_t window_object, focus_view, native_control, delegate_interface;
    uint64_t ccl_base, exe_base;
    uint32_t generation_available, authorizes_deferred_edit;
} CclOwnerResult;
CCL_OWNER_API uint32_t CclOwnerInitialize(CclOwnerContext* context);
CCL_OWNER_API uint32_t CclOwnerProbe(const CclOwnerContext* context, CclOwnerResult* result);
/* Exact RiumOwnerReader ABI. A configured reader retains provider 2/profile 1
   on failure, with kind UNKNOWN. No result grants deferred-edit permission. */
CCL_OWNER_API void CclOwnerCapture(void* context, RiumOwnerStamp* out);
/* Call only after all captures have returned. No module reference is owned. */
CCL_OWNER_API void CclOwnerShutdown(CclOwnerContext* context);
#ifdef __cplusplus
}
#endif
#endif
