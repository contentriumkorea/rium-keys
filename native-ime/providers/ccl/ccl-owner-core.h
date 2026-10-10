#ifndef CCL_OWNER_CORE_H
#define CCL_OWNER_CORE_H
#include "ccl-owner-reader.h"
#include <stddef.h>

enum CclWindowClass { CCL_CLASS_OTHER, CCL_CLASS_WINDOW, CCL_CLASS_DIALOG, CCL_CLASS_EDIT };
struct CclWindowMetadata {
    uint64_t hwnd, root, parent, procedure, dialog_procedure, userdata;
    uint32_t process, thread, kind, style;
};
struct CclFocusMetadata {
    uint32_t process, thread, foreground_process, foreground_thread, gui_flags;
    uint64_t foreground, active, capture;
    CclWindowMetadata focus, owner;
};
/* All callbacks here are reader-owned OS wrappers or synthetic fixture doubles.
   They are never application interface methods. */
struct CclOwnerOps {
    void* user;
    bool (*read)(void*, uint64_t, void*, size_t);
    bool (*focus)(void*, CclFocusMetadata*);
    bool (*code_matches)(void*, const CclOwnerContext*);
};
uint32_t CclOwnerProbeSource(const CclOwnerContext*, const CclOwnerOps&, CclOwnerResult*);
void CclOwnerMakeStamp(const CclOwnerResult*, RiumOwnerStamp*);
#endif
