#include "ccl-owner-core.h"
#include <cstring>

namespace {
struct Handler {
    uint64_t node, next, interface_pointer, vtable, object_vtable, method;
};
struct Snapshot {
    CclFocusMetadata windows;
    uint32_t decision, handler_count;
    int32_t edit_count;
    uint64_t window, window_vtable, window_ivtable, window_handle, view, view_vtable;
    uint64_t native, native_vtable, native_handle, native_owner, reciprocal_native;
    uint64_t host_interface_vtable, delegate, delegate_vtable, mouse_handler;
    uint64_t gui, gui_vtable, gui_interface_vtable, handler_head;
    Handler handlers[CCL_OWNER_MAX_HANDLERS];
};
struct Reader {
    const CclOwnerOps& ops;
    unsigned reads = 0;
    template<class T> bool get(uint64_t address, T& value) {
        if(address == 0 || address >= 0x0000800000000000ULL || address % alignof(T) || ++reads > 256) return false;
        return ops.read(ops.user,address,&value,sizeof(value));
    }
    bool equal_pointer(uint64_t address, uint64_t expected) {
        uint64_t value=0; return get(address,value) && value==expected;
    }
};
uint32_t Capture(const CclOwnerContext& c, const CclOwnerOps& ops, Snapshot& s) {
    Reader r{ops};
    if(!ops.focus(ops.user,&s.windows)) return CCL_REASON_WINDOW_STATE;
    const auto& q=s.windows;
    const auto& f=q.focus;
    const auto& w=f.kind==CCL_CLASS_EDIT ? q.owner : f;
    if(q.process!=c.process_id || !q.thread || q.foreground_process!=q.process || q.foreground_thread!=q.thread ||
       f.process!=q.process || f.thread!=q.thread || w.process!=q.process || w.thread!=q.thread)
        return CCL_REASON_WRONG_THREAD_OR_PROCESS;
    if(!q.foreground || q.active!=q.foreground || q.capture || (q.gui_flags&0x1E) ||
       !f.hwnd || !w.hwnd || f.root!=q.foreground || w.root!=q.foreground || (f.style&0x08000000))
        return CCL_REASON_WINDOW_STATE;
    const bool text=f.kind==CCL_CLASS_EDIT;
    const bool dialog=w.kind==CCL_CLASS_DIALOG;
    if(!text && (f.kind!=CCL_CLASS_WINDOW || f.hwnd!=q.foreground)) return CCL_REASON_UNSUPPORTED_FOCUS;
    if(w.kind!=CCL_CLASS_WINDOW && w.kind!=CCL_CLASS_DIALOG) return CCL_REASON_UNSUPPORTED_FOCUS;
    if(w.procedure!=c.ccl_base+(dialog?0x94130:0x9C470) ||
       (dialog && w.dialog_procedure!=c.ccl_base+0x941A0)) return CCL_REASON_OWNER_MISMATCH;
    if(text && (f.parent!=w.hwnd || f.procedure!=c.ccl_base+0xAB5C0 || (f.style&0x800)))
        return CCL_REASON_OWNER_MISMATCH;
    s.window=w.userdata;
    if(!r.get(s.window,s.window_vtable) || !r.get(s.window+0x1E8,s.window_ivtable)) return CCL_REASON_READ_FAILED;
    if(s.window_vtable!=c.ccl_base+(dialog?0x44B980:0x48FE18) ||
       s.window_ivtable!=c.ccl_base+(dialog?0x44B870:0x42B580) ||
       !r.equal_pointer(s.window_ivtable+5*8,c.ccl_base+0xDE490) ||
       !r.equal_pointer(s.window_ivtable+31*8,c.ccl_base+0x93600)) return CCL_REASON_OWNER_MISMATCH;
    if(!r.get(s.window+0x230,s.window_handle) || !r.get(s.window+0x240,s.view) ||
       !r.get(c.ccl_base+0x55B414,s.edit_count)) return CCL_REASON_READ_FAILED;
    if(s.window_handle!=w.hwnd || !s.view) return CCL_REASON_OWNER_MISMATCH;
    if(s.edit_count<0 || s.edit_count>64) return CCL_REASON_EDIT_MODE_MISMATCH;
    if(!r.get(s.view,s.view_vtable)) return CCL_REASON_READ_FAILED;
    if(text) {
        if(s.view_vtable!=c.ccl_base+0x43FBC8) return CCL_REASON_OWNER_MISMATCH;
        s.native=f.userdata;
        if(!r.get(s.native,s.native_vtable)) return CCL_REASON_READ_FAILED;
        if(s.native_vtable!=c.ccl_base+0x4300F0) return CCL_REASON_OWNER_MISMATCH;
        if(!r.get(s.native+0x50,s.native_handle) || !r.get(s.native+0x38,s.native_owner) ||
           !r.get(s.view+0x278,s.reciprocal_native)) return CCL_REASON_READ_FAILED;
        if(s.native_handle!=f.hwnd || s.native_owner!=s.view || s.reciprocal_native!=s.native)
            return CCL_REASON_OWNER_MISMATCH;
        if(s.edit_count==0) return CCL_REASON_EDIT_MODE_MISMATCH;
        s.decision=CCL_OWNER_TEXT; return CCL_REASON_OK;
    }
    if(s.edit_count!=0) return CCL_REASON_EDIT_MODE_MISMATCH;
    if(!r.equal_pointer(s.window_vtable+67*8,c.ccl_base+0x343D80) ||
       !r.equal_pointer(s.window_vtable+68*8,c.ccl_base+0x343E90) ||
       s.view_vtable!=c.ccl_base+0x447C78 ||
       !r.equal_pointer(s.view_vtable+67*8,c.ccl_base+0x141CD0) ||
       !r.equal_pointer(s.view_vtable+68*8,c.ccl_base+0x141CD0)) return CCL_REASON_OWNER_MISMATCH;
    if(!r.get(s.window+0x228,s.mouse_handler)) return CCL_REASON_READ_FAILED;
    if(s.mouse_handler) return CCL_REASON_MOUSE_HANDLER;
    if(!r.get(s.view+0x148,s.host_interface_vtable)) return CCL_REASON_READ_FAILED;
    if(s.host_interface_vtable!=c.ccl_base+0x447C48 ||
       !r.equal_pointer(s.host_interface_vtable+4*8,c.ccl_base+0xB8AB0)) return CCL_REASON_OWNER_MISMATCH;
    if(!r.get(s.view+0x158,s.delegate) || !r.get(s.delegate,s.delegate_vtable)) return CCL_REASON_READ_FAILED;
    if(s.delegate_vtable!=c.exe_base+0x14CB8D0 ||
       !r.equal_pointer(s.delegate_vtable+3*8,c.exe_base+0x126A3E0) ||
       !r.equal_pointer(s.delegate_vtable+30*8,c.exe_base+0xD57B0) ||
       !r.equal_pointer(s.delegate_vtable+31*8,c.exe_base+0xD57B0)) return CCL_REASON_OWNER_MISMATCH;
    if(!r.get(c.ccl_base+0x52C2C8,s.gui)) return CCL_REASON_READ_FAILED;
    if(s.gui!=c.ccl_base+0x55C990) return CCL_REASON_OWNER_MISMATCH;
    if(!r.get(s.gui,s.gui_vtable) || !r.get(s.gui+0x30,s.gui_interface_vtable)) return CCL_REASON_READ_FAILED;
    if(s.gui_vtable!=c.ccl_base+0x4346F0 || s.gui_interface_vtable!=c.ccl_base+0x434590)
        return CCL_REASON_OWNER_MISMATCH;
    if(!r.get(s.gui+0x138,s.handler_head)) return CCL_REASON_READ_FAILED;
    for(uint64_t node=s.handler_head; node;) {
        if(s.handler_count==CCL_OWNER_MAX_HANDLERS) return CCL_REASON_HANDLER_LIMIT;
        for(uint32_t i=0;i<s.handler_count;++i) if(s.handlers[i].node==node) return CCL_REASON_HANDLER_LIMIT;
        auto& h=s.handlers[s.handler_count++]; h.node=node;
        if(!r.get(node,h.interface_pointer) || !r.get(node+8,h.next) || !r.get(h.interface_pointer,h.vtable)) return CCL_REASON_READ_FAILED;
        if(h.vtable!=c.exe_base+0x153EE80 || h.interface_pointer<0x180) return CCL_REASON_UNKNOWN_GLOBAL_HANDLER;
        if(!r.get(h.interface_pointer-0x180,h.object_vtable) || !r.get(h.vtable+3*8,h.method)) return CCL_REASON_READ_FAILED;
        if(h.object_vtable!=c.exe_base+0x153EFE0 || h.method!=c.exe_base+0xAF0190)
            return CCL_REASON_UNKNOWN_GLOBAL_HANDLER;
        /* Verified KEYDOWN path stores one transient tracking word and returns
           false, with no call or document/focus access. Never read that word.
           KEYUP has a conditional application callback; no keyup claim here. */
        node=h.next;
    }
    s.decision=CCL_OWNER_COMMAND; return CCL_REASON_OK;
}
}

uint32_t CclOwnerProbeSource(const CclOwnerContext* context, const CclOwnerOps& ops, CclOwnerResult* result) {
    if(!result) return CCL_OWNER_UNKNOWN;
    std::memset(result,0,sizeof(*result));
    result->size=sizeof(*result); result->version=CCL_OWNER_VERSION; result->immediate_only=1;
    result->reason=CCL_REASON_NOT_INITIALIZED;
    if(!context || context->size!=sizeof(*context) || context->version!=CCL_OWNER_VERSION ||
       context->initialized!=1 || !ops.read || !ops.focus || !ops.code_matches) return CCL_OWNER_UNKNOWN;
    if(!ops.code_matches(ops.user,context)) { result->reason=CCL_REASON_BUILD_MISMATCH; return CCL_OWNER_UNKNOWN; }
    Snapshot first, second;
    std::memset(&first,0,sizeof(first)); std::memset(&second,0,sizeof(second));
    result->reason=Capture(*context,ops,first);
    if(result->reason!=CCL_REASON_OK) return CCL_OWNER_UNKNOWN;
    result->reason=Capture(*context,ops,second);
    if(result->reason!=CCL_REASON_OK) return CCL_OWNER_UNKNOWN;
    if(!ops.code_matches(ops.user,context)) { result->reason=CCL_REASON_BUILD_MISMATCH; return CCL_OWNER_UNKNOWN; }
    if(std::memcmp(&first,&second,sizeof(first))) { result->reason=CCL_REASON_CHANGED_DURING_READ; return CCL_OWNER_UNKNOWN; }
    result->decision=first.decision; result->reason=CCL_REASON_OK;
    result->process_id=first.windows.process; result->thread_id=first.windows.thread;
    result->global_handler_count=first.handler_count;
    result->foreground_hwnd=first.windows.foreground; result->focus_hwnd=first.windows.focus.hwnd;
    result->window_hwnd=first.window_handle; result->window_object=first.window; result->focus_view=first.view;
    result->native_control=first.native; result->delegate_interface=first.delegate;
    result->ccl_base=context->ccl_base; result->exe_base=context->exe_base;
    return result->decision;
}

void CclOwnerMakeStamp(const CclOwnerResult* result, RiumOwnerStamp* out) {
    if(!out) return;
    std::memset(out,0,sizeof(*out));
    out->provider=2; out->profile=1;
    if(!result || result->size!=sizeof(*result) || result->version!=CCL_OWNER_VERSION ||
       result->reason!=CCL_REASON_OK || !result->process_id || !result->thread_id ||
       !result->focus_hwnd || !result->window_object || !result->focus_view ||
       (result->decision!=CCL_OWNER_TEXT && result->decision!=CCL_OWNER_COMMAND) ||
       (result->decision==CCL_OWNER_TEXT && !result->native_control)) return;
    out->kind=result->decision;
    out->processId=result->process_id; out->threadId=result->thread_id;
    out->focus=reinterpret_cast<HWND>(static_cast<uintptr_t>(result->focus_hwnd));
    out->windowObject=static_cast<uintptr_t>(result->window_object);
    out->logicalObject=static_cast<uintptr_t>(result->focus_view);
    out->textObject=static_cast<uintptr_t>(result->native_control);
    /* LifetimeToken is zero: this is not a weak ref or lifecycle generation. */
}
