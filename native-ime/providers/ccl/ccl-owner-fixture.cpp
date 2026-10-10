#include "ccl-owner-core.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using Memory = std::map<uint64_t, std::vector<unsigned char>>;
static constexpr uint64_t C = 0x180000000, E = 0x140000000, W = 0x10000, F = 0x20000, N = 0x30000;
struct Fixture {
    CclOwnerContext context{sizeof(CclOwnerContext), 1, 1, 101, C, E};
    CclFocusMetadata first{}, second{};
    Memory a, b;
    unsigned captures = 0, code_checks = 0, reads = 0;
    bool code_ok = true, code_changes = false, unreadable = false;
    template<class T> static void put(Memory& memory, uint64_t address, T value) {
        auto& data = memory[address]; data.resize(sizeof(value)); std::memcpy(data.data(), &value, sizeof(value));
    }
    void pointer(uint64_t address, uint64_t value) { put(a, address, value); put(b, address, value); }
    void count(int32_t value) { put(a, C+0x55B414, value); put(b, C+0x55B414, value); }
    static bool Read(void* user, uint64_t address, void* target, size_t size) {
        auto& f = *static_cast<Fixture*>(user); ++f.reads;
        if(f.unreadable) return false;
        auto& m = f.captures == 1 ? f.a : f.b;
        auto it = m.find(address);
        if(it == m.end() || it->second.size() != size) return false;
        std::memcpy(target, it->second.data(), size); return true;
    }
    static bool Focus(void* user, CclFocusMetadata* output) {
        auto& f = *static_cast<Fixture*>(user); *output = f.captures++ == 0 ? f.first : f.second; return true;
    }
    static bool Code(void* user, const CclOwnerContext*) {
        auto& f = *static_cast<Fixture*>(user); const auto check=f.code_checks++;
        return f.code_ok && !(f.code_changes && check != 0);
    }
    Fixture(bool text = false, bool dialog = false) {
        first.process = first.foreground_process = 101;
        first.thread = first.foreground_thread = 202;
        first.foreground = first.active = 100;
        first.focus = {100,100,0,C+0x9C470,0,W,101,202,CCL_CLASS_WINDOW,0};
        first.owner = first.focus;
        pointer(W, C + (dialog ? 0x44B980 : 0x48FE18));
        pointer(W+0x1E8, C + (dialog ? 0x44B870 : 0x42B580));
        const auto iv = C + (dialog ? 0x44B870 : 0x42B580);
        pointer(iv+5*8, C+0xDE490); pointer(iv+31*8,C+0x93600);
        pointer(W+0x230,100); pointer(W+0x240,F); pointer(W+0x228,0);
        count(text ? 1 : 0);
        if(text) {
            first.focus = {101,100,100,C+0xAB5C0,0,N,101,202,CCL_CLASS_EDIT,0};
            if(dialog) { first.owner.kind=CCL_CLASS_DIALOG; first.owner.procedure=C+0x94130; first.owner.dialog_procedure=C+0x941A0; }
            pointer(N,C+0x4300F0); pointer(N+0x38,F); pointer(N+0x50,101);
            pointer(F,C+0x43FBC8); pointer(F+0x278,N);
        } else {
            pointer(C+0x48FE18+67*8,C+0x343D80); pointer(C+0x48FE18+68*8,C+0x343E90);
            pointer(F,C+0x447C78); pointer(F+0x148,C+0x447C48); pointer(C+0x447C48+4*8,C+0xB8AB0);
            pointer(C+0x447C78+67*8,C+0x141CD0); pointer(C+0x447C78+68*8,C+0x141CD0);
            pointer(F+0x158,0x40000); pointer(0x40000,E+0x14CB8D0);
            pointer(E+0x14CB8D0+3*8,E+0x126A3E0); pointer(E+0x14CB8D0+30*8,E+0xD57B0); pointer(E+0x14CB8D0+31*8,E+0xD57B0);
            pointer(C+0x52C2C8,C+0x55C990); pointer(C+0x55C990,C+0x4346F0); pointer(C+0x55C9C0,C+0x434590);
            pointer(C+0x55CAC8,0x50000); pointer(0x50000,0x60180); pointer(0x50008,0);
            pointer(0x60180,E+0x153EE80); pointer(0x60000,E+0x153EFE0); pointer(E+0x153EE80+3*8,E+0xAF0190);
        }
        second=first;
    }
    CclOwnerResult run() {
        CclOwnerResult result{}; CclOwnerOps ops{this,Read,Focus,Code};
        CclOwnerProbeSource(&context,ops,&result); return result;
    }
};
static unsigned checks=0, failures=0;
static void Check(bool value, const char* name) { ++checks; if(!value) { ++failures; std::printf("FAIL %s\n",name); } }
int main() {
    // Negative transitions and ambiguous cases are specified before positive cases.
    { Fixture f; f.second.focus.hwnd=102; Check(f.run().decision==CCL_OWNER_UNKNOWN,"focus HWND transition"); }
    { Fixture f; f.second.thread=203; Check(f.run().decision==CCL_OWNER_UNKNOWN,"UI thread transition"); }
    { Fixture f; f.first.foreground_thread=f.second.foreground_thread=303; Check(f.run().decision==CCL_OWNER_UNKNOWN,"not native focus thread"); }
    { Fixture f; f.first.gui_flags=f.second.gui_flags=4; Check(f.run().decision==CCL_OWNER_UNKNOWN,"menu loop"); }
    { Fixture f; f.first.capture=f.second.capture=100; Check(f.run().decision==CCL_OWNER_UNKNOWN,"mouse capture"); }
    { Fixture f; f.pointer(W+0x228,0x90000); Check(f.run().decision==CCL_OWNER_UNKNOWN,"active mouse handler"); }
    { Fixture f; f.pointer(0x60180,E+0x153EE88); Check(f.run().decision==CCL_OWNER_UNKNOWN,"unknown global handler"); }
    { Fixture f; f.pointer(0x50008,0x50000); Check(f.run().decision==CCL_OWNER_UNKNOWN,"handler list cycle"); }
    { Fixture f; f.pointer(E+0x14CB8D0+30*8,E+0xD57B1); Check(f.run().decision==CCL_OWNER_UNKNOWN,"changed delegate key slot"); }
    { Fixture f; f.count(1); Check(f.run().decision==CCL_OWNER_UNKNOWN,"workspace global text mode"); }
    { Fixture f; f.code_ok=false; Check(f.run().decision==CCL_OWNER_UNKNOWN && f.reads==0,"unverified loaded code"); }
    { Fixture f; f.code_changes=true; Check(f.run().decision==CCL_OWNER_UNKNOWN,"code changed between snapshots"); }
    { Fixture f; f.unreadable=true; Check(f.run().decision==CCL_OWNER_UNKNOWN,"unreadable object"); }
    { Fixture f(true); f.pointer(N+0x38,F+8); Check(f.run().decision==CCL_OWNER_UNKNOWN,"native owner mismatch"); }
    { Fixture f(true); f.pointer(F+0x278,N+8); Check(f.run().decision==CCL_OWNER_UNKNOWN,"reciprocal control mismatch"); }
    { Fixture f(true); f.first.focus.style=f.second.focus.style=0x800; Check(f.run().decision==CCL_OWNER_UNKNOWN,"readonly edit"); }
    { Fixture f(true); f.first.focus.procedure=f.second.focus.procedure=C+0xAB5C1; Check(f.run().decision==CCL_OWNER_UNKNOWN,"unverified edit subclass"); }
    { Fixture f(true); f.count(0); Check(f.run().decision==CCL_OWNER_UNKNOWN,"native text counter mismatch"); }
    { Fixture f(true,true); f.first.owner.dialog_procedure=f.second.owner.dialog_procedure=C+8; Check(f.run().decision==CCL_OWNER_UNKNOWN,"unverified dialog procedure"); }
    { Fixture f; f.first.focus.kind=f.second.focus.kind=CCL_CLASS_OTHER; Check(f.run().decision==CCL_OWNER_UNKNOWN,"foreign native/plugin focus"); }
    { Fixture f; f.pointer(F,C+0x447C80); Check(f.run().decision==CCL_OWNER_UNKNOWN,"windowless custom focus"); }
    { Fixture f; f.pointer(0x60000,E+0x153EFE8); Check(f.run().decision==CCL_OWNER_UNKNOWN,"handler object/adjustment mismatch"); }
    { Fixture f; Fixture::put(f.b,C+0x55CAC8,uint64_t(0x50100)); Fixture::put(f.b,0x50100,uint64_t(0x60180)); Fixture::put(f.b,0x50108,uint64_t(0)); auto r=f.run(); Check(r.decision==CCL_OWNER_UNKNOWN && r.reason==CCL_REASON_CHANGED_DURING_READ,"two valid command snapshots with different list episode"); }
    { Fixture f(true); f.second.focus.userdata=N+0x1000; Fixture::put(f.b,N+0x1000,C+0x4300F0); Fixture::put(f.b,N+0x1038,F); Fixture::put(f.b,N+0x1050,uint64_t(101)); Fixture::put(f.b,F+0x278,N+0x1000); auto r=f.run(); Check(r.decision==CCL_OWNER_UNKNOWN && r.reason==CCL_REASON_CHANGED_DURING_READ,"two valid text owners reuse same HWND"); }
    { Fixture f; f.pointer(C+0x55CAC8,0x80000); for(unsigned i=0;i<33;++i) { const uint64_t node=0x80000+i*0x20, object=0x90000+i*0x1000; f.pointer(node,object+0x180); f.pointer(node+8,node+0x20); f.pointer(object,E+0x153EFE0); f.pointer(object+0x180,E+0x153EE80); } auto r=f.run(); Check(r.decision==CCL_OWNER_UNKNOWN && r.reason==CCL_REASON_HANDLER_LIMIT,"global handler enumeration bound"); }
    { Fixture f; auto r=f.run(); Check(r.decision==CCL_OWNER_COMMAND,"known workspace keydown reaches commands"); Check(r.immediate_only==1 && r.generation_available==0 && r.authorizes_deferred_edit==0,"no async lifetime claim"); Check(f.captures==2 && f.code_checks==2,"two window/owner snapshots and code checks"); }
    { Fixture f(true); auto r=f.run(); Check(r.decision==CCL_OWNER_TEXT && r.native_control==N && r.focus_view==F,"current native edit positive"); }
    { Fixture f(true,true); Check(f.run().decision==CCL_OWNER_TEXT,"current StandardDialog edit positive"); }
    { Fixture f; f.pointer(C+0x55CAC8,0); Check(f.run().decision==CCL_OWNER_COMMAND,"empty global handler list also permitted"); }
    { RiumOwnerStamp stamp; std::memset(&stamp,0xA5,sizeof(stamp)); CclOwnerMakeStamp(nullptr,&stamp); Check(stamp.provider==2 && stamp.profile==1 && stamp.kind==RIUM_OWNER_UNKNOWN && !stamp.focus && !stamp.textObject && !stamp.deferredSafe && !stamp.lifetimeToken,"failed adapter retains provider identity and clears stale owner"); }
    { Fixture f; auto r=f.run(); RiumOwnerStamp stamp{}; CclOwnerMakeStamp(&r,&stamp); Check(stamp.provider==2 && stamp.profile==1 && stamp.kind==RIUM_OWNER_COMMAND && stamp.processId==101 && stamp.threadId==202 && reinterpret_cast<uintptr_t>(stamp.focus)==100 && stamp.windowObject==W && stamp.logicalObject==F && stamp.textObject==0 && !stamp.lifetimeToken && !stamp.deferredSafe,"command stamp maps numeric owner only"); }
    { Fixture f(true); auto r=f.run(); RiumOwnerStamp stamp{}; CclOwnerMakeStamp(&r,&stamp); Check(stamp.kind==RIUM_OWNER_TEXT && stamp.textObject==N && stamp.logicalObject==F && !stamp.deferredSafe && !stamp.lifetimeToken,"text stamp maps current native control without generation"); }
    { Fixture f(true); auto r=f.run(); r.native_control=0; RiumOwnerStamp stamp{}; CclOwnerMakeStamp(&r,&stamp); Check(stamp.kind==RIUM_OWNER_UNKNOWN,"text stamp rejects missing native owner"); }
    { Fixture f; auto r=f.run(); r.reason=CCL_REASON_CHANGED_DURING_READ; RiumOwnerStamp stamp{}; CclOwnerMakeStamp(&r,&stamp); Check(stamp.kind==RIUM_OWNER_UNKNOWN,"adapter rejects stale successful decision after failure"); }
    { Fixture f; auto r=f.run(); r.version++; RiumOwnerStamp stamp{}; CclOwnerMakeStamp(&r,&stamp); Check(stamp.kind==RIUM_OWNER_UNKNOWN,"adapter rejects unknown result ABI"); }
    std::printf("CclOwner fixture: checks=%u failures=%u windows=0 processes=0\n",checks,failures);
    return failures ? 1 : 0;
}
