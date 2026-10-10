#include "owner-snapshot-core.h"
#include <array>
#include <map>
#include <memory>
#include <cstring>
#include <cstdio>
#include <limits>

// Synthetic sparse metadata, never an Adobe process/provider. No windows,
// input, hooks, COM or installed profiles are used by these boundary tests.
struct Memory {
    static constexpr uintptr_t base = 0x100000000ULL;
    std::map<uintptr_t,std::array<unsigned char,4096>> pages;
    uintptr_t next=0x300000000ULL, forbidden=0; unsigned forbiddenReads=0;
    void Put(uintptr_t p,const void* bytes,size_t size) {
        auto* s=static_cast<const unsigned char*>(bytes);
        for(size_t i=0;i<size;++i) pages[(p+i)&~uintptr_t(4095)][(p+i)&4095]=s[i];
    }
    template<class T> void Set(uintptr_t p,T value) { Put(p,&value,sizeof(value)); }
    uintptr_t Alloc() { const auto p=next;next+=0x1000;pages[p]={};return p; }
    static bool Read(void* context,uintptr_t p,void* bytes,size_t n) {
        auto& m=*static_cast<Memory*>(context); if(m.forbidden&&p<=m.forbidden&&p+n>m.forbidden)++m.forbiddenReads;
        auto* out=static_cast<unsigned char*>(bytes);
        for(size_t i=0;i<n;++i) { auto it=m.pages.find((p+i)&~uintptr_t(4095));if(it==m.pages.end())return false;out[i]=it->second[(p+i)&4095]; }
        return true;
    }
    OwnerReader Reader() { return {Read,this,base,kOwnerImageSize,GetTickCount64()+10000,nullptr}; }
    uintptr_t Object(uint32_t vt) { const auto p=Alloc();Set(p,base+vt);return p; }
    uintptr_t Intern(uintptr_t id) { const auto p=Alloc();Set(p+0x10,id);return p; }
    uintptr_t Dictionary() { const auto p=Object(0x263D95F8);Set<uint64_t>(p+0x88,0);return p; }
    void Entry(uintptr_t d,unsigned index,uintptr_t key,uint8_t tag,uintptr_t value) {
        const auto e=d+8+index*32;Set(e,key);Set(e+8,value);Set<uint8_t>(e+0x18,tag);Set<uint64_t>(d+0x88,index+1);
    }
    uintptr_t keyMatch,keyBypass,keyVary,keyFlag,expectedText;
    Memory() {
        for(const auto& c:kOwnerTypes) {
            pages.try_emplace((base+c.vtable+0x340)&~uintptr_t(4095));
            Set(base+c.vtable-8,base+c.col);
            uint32_t col[6]={1,c.offset,0,c.type,0,c.col};Put(base+c.col,col,sizeof(col));
            Set(base+c.vtable,base+0x1915ACB0);
        }
        for(const auto& s:kOwnerSpans)Put(base+s.rva,s.bytes,s.count);
        for(const auto& s:kOwnerSlots)Set(base+s.vtable+s.offset,base+s.target);
        keyMatch=Intern(0xAAAA01);keyBypass=Intern(0xAAAA02);keyVary=Intern(0xAAAA03);keyFlag=Intern(0xAAAA04);expectedText=Intern(0xBBBB01);
        Set(base+0x2D422098,keyMatch);Set(base+0x2D3F0F20,keyBypass);Set(base+0x2D3F0EA0,keyVary);Set(base+0x2D457F28,expectedText);
        Set(base+0x2D421B98,keyFlag);
        Set<uintptr_t>(base+0x2D3E0558,0);Set<uintptr_t>(base+0x2D9FF6F8,0);Set<uintptr_t>(base+0x2D9FF700,0);Set<uintptr_t>(base+0x2D9FF708,0);
        Set<uint32_t>(base+0x2D386FE0,GetCurrentThreadId());Set<uintptr_t>(base+0x2AAD6560,0xCAFE);Set<uintptr_t>(base+0x2AAD6568,0xBABE);
        Set<uint8_t>(base+0x2D3E1370,0);Set<uint8_t>(base+0x2D3E1B19,0);Set<uintptr_t>(base+0x2D3E1B38,0);
    }
};
struct Parameter {
    uintptr_t raw,owner,block,holder,flags;
    explicit Parameter(Memory& m) {
        raw=m.Object(0x265C8B48);owner=raw+0x658;m.Set(owner,Memory::base+0x265C9388);
        block=m.Alloc();holder=m.Alloc();flags=m.Dictionary();
        m.Set(raw+0x38,holder);m.Set(raw+0x60,flags);m.Set<uint64_t>(holder+0x10,0);m.Set<double>(holder+0x50,2.0);
    }
    OwnerEndpoint Read(Memory& m) { auto r=m.Reader();return OwnerReadEndpoint(r,raw,owner,block); }
};
struct Chain {
    uintptr_t manager,child,parent,children,sentinel,filterEntries,filter;
    explicit Chain(Memory& m) {
        manager=m.Object(0x267421F8);child=m.Object(0x26E30F30);parent=m.Object(0x26E182C8);
        children=m.Alloc();sentinel=m.Alloc();m.Set(sentinel,sentinel);m.Set(children,child);
        m.Set(Memory::base+0x2D3E0558,manager);m.Set<uintptr_t>(manager+0x80,101);m.Set(manager+0x88,child);m.Set(manager+0xF0,sentinel);
        m.Set<uintptr_t>(child+0x10,101);m.Set<uint32_t>(child+0x18,1);m.Set<uintptr_t>(child+0x1F0,202);m.Set(child+0x1F8,parent);m.Set(child+0x428,sentinel);
        m.Set<uintptr_t>(parent+0x10,202);m.Set<uint32_t>(parent+0x18,1);m.Set(parent+0xA0,children);m.Set(parent+0xA8,children+8);m.Set(parent+0x428,sentinel);
        filterEntries=m.Alloc();filter=m.Object(0x2776DA20);m.Set<uintptr_t>(filter+0x10,303);m.Set<uint32_t>(filter+0x18,1);
        m.Set(filter+0x3F8,sentinel);m.Set<uintptr_t>(filterEntries,303);m.Set(filterEntries+8,filter);
        m.Set(manager+0x30,filterEntries);m.Set(manager+0x38,filterEntries+16);
        m.Set(Memory::base+0x2776DA20+0x188,Memory::base+0x193A5340);
    }
    bool Collect(Memory& m,OwnerSnapshot* s) { auto r=m.Reader();OwnerInputs input{};OwnerCollect(r,input,s);return !r.failed&&!r.limited; }
};
struct MonitorFixture {
    OwnerInputs input{};uintptr_t bg,entry,dm,selected,component,owner,block,flags;
    explicit MonitorFixture(Memory& m) {
        const auto tab=m.Object(0x27875E58),wrap=m.Object(0x27294F90),monitor=m.Object(0x272943F8);
        bg=m.Object(0x27292E88);input.count=1;input.windows[0]={0x1234,tab};
        m.Set<uintptr_t>(tab+0x10,100);m.Set<uint32_t>(tab+0x18,2);m.Set(tab+0x6F8,wrap);m.Set(tab+0x700,m.Alloc());m.Set(wrap+0x60,monitor);
        m.Set(monitor+0x220,bg);m.Set(monitor+0x228,m.Alloc());m.Set(Memory::base+0x27875E58+0x190,Memory::base+0x1E8E0880);m.Set(Memory::base+0x272943F8+0x228,Memory::base+0x21399020);
        const auto sentinel=m.Alloc(),buckets=m.Alloc();entry=m.Alloc();
        m.Set<uint32_t>(bg+0x204,0x76636F6D);m.Set<uint32_t>(bg+0x3C4,1);m.Set(bg+0x3D0,sentinel);m.Set(bg+0x3E0,buckets);m.Set<uintptr_t>(bg+0x3F8,0);
        m.Set(buckets,entry);m.Set(buckets+8,entry);m.Set<uint32_t>(entry+0x10,1);
        const auto complete=m.Alloc();dm=complete+0x10;m.Set(dm,Memory::base+0x27996E48);m.Set(complete+0xCB8,Memory::base+0x279970E8);
        m.Set(entry+0x18,dm);m.Set(entry+0x20,complete+0xCB8);m.Set(entry+0x28,m.Alloc());m.Set(Memory::base+0x27996E48+0xA0,Memory::base+0x23975260);
        selected=m.Alloc();m.Set(dm+0x9D8,selected);m.Set(dm+0x9E0,selected+0x58);m.Set<uintptr_t>(dm+0x9F0,0);
        const auto c=m.Alloc();component=c+0x30;owner=c+0x780;block=m.Alloc();m.Set(component,Memory::base+0x26662DA0);m.Set(owner,Memory::base+0x266632C0);
        m.Set(selected+8,component);m.Set(selected+0x10,owner);m.Set(selected+0x18,block);
        flags=m.Dictionary();m.Set(component+0x518,flags);
        m.Set(selected+0x20,m.Object(0x12345678));m.Set(selected+0x28,m.Alloc());m.Set(selected+0x30,m.Alloc());
    }
    bool Collect(Memory& m,OwnerSnapshot* s){auto r=m.Reader();OwnerCollect(r,input,s);return !r.failed&&!r.limited;}
};
static unsigned checks=0,failures=0;
static void Check(bool value,const char* name) { ++checks;if(!value)++failures;std::printf("%s synthetic %s\n",value?"PASS":"FAIL",name); }
int main() {
    {
        Memory m;auto r=m.Reader();const auto p=m.Object(0x26E30F30);
        Check(OwnerReadObject(r,p).state==ORead,"exact-vtable-and-COL");
        m.Set<uint32_t>(Memory::base+0x26E30F30-8,0);r=m.Reader();
        Check(OwnerReadObject(r,p).state==OInvalid,"bad-COL-rejected");
        m.Set(p,Memory::base+0x12345678);r=m.Reader();Check(OwnerReadObject(r,p).state==OUnsupported,"unknown-vtable-no-layout-follow");
    }
    {
        Memory m;auto r=m.Reader();const auto p=m.Object(0x265085F0);
        Check(OwnerReadObject(r,p).state==ORead,"native-Edit-item-exact-COL-primary-UI-Node");
        r=m.Reader();Check(OwnerReadObject(r,m.Object(0x26507BA8)).state==ORead,"native-Edit-wrapper-exact-COL-primary-UI-Node");
        m.Set<uintptr_t>(p+0x10,12345);m.Set<uint32_t>(p+0x18,4);
        OwnerInputs in{};in.count=1;in.windows[0]={0x1234,p};auto s=std::make_unique<OwnerSnapshot>();r=m.Reader();OwnerCollect(r,in,s.get());
        Check(!r.failed&&!r.limited&&s->windows[0].propertyToken==12345&&s->windows[0].propertyRefcount==4,"native-Edit-lifetime-metadata-bound-to-window-property");
    }
    {
        Memory m;auto r=m.Reader();uint32_t bad=0;
        Check(OwnerVerifySpans(r,&bad)&&!bad,"exact-code-spans");
        m.Set<uint8_t>(Memory::base+kOwnerSpans[3].rva,0xCC);r=m.Reader();
        Check(!OwnerVerifySpans(r,&bad)&&bad==kOwnerSpans[3].rva,"modified-span-rejected");
        r=m.Reader();r.deadline=0;uintptr_t x=0;Check(!r.Get(Memory::base,&x)&&r.limited&&!r.reads,"deadline-stops-reads");
        r=m.Reader();r.reads=24000;Check(!r.Get(Memory::base,&x)&&r.limited,"read-count-cap");
        r=m.Reader();LARGE_INTEGER now{};QueryPerformanceCounter(&now);r.qpcDeadline=now.QuadPart-1;
        Check(!r.Get(Memory::base+kOwnerSpans[0].rva,&x)&&r.limited&&!r.reads,"high-resolution-deadline-stops-first-read");
        r=m.Reader();Check(!r.Get(0x700000000ULL,&x)&&r.failed,"unreadable-pointer-propagates");
    }
    {
        Memory m;m.Set<uint8_t>(Memory::base+0x263D728D,0xCC);auto r=m.Reader();uint32_t bad=0;
        Check(OwnerVerifySpans(r,&bad),"short-data-span-does-not-compare-neighbor-relocations");
        m.Set<uint8_t>(Memory::base+0x263D728A,0);r=m.Reader();
        Check(!OwnerVerifySpans(r,&bad)&&bad==0x263D728A,"modified-comparison-order-constant-rejected");
    }
    {
        Memory m;const auto& slot=kOwnerSlots[0];m.Set<uintptr_t>(Memory::base+slot.vtable+slot.offset,Memory::base+slot.target+1);
        auto r=m.Reader();uint32_t bad=0;
        Check(!OwnerVerifySpans(r,&bad)&&bad==slot.vtable+slot.offset,"modified-relocated-vtable-slot-rejected");
    }
    {
        Memory m;Parameter p(m);
        auto value=p.Read(m);Check(value.state==ORead&&value.value==2&&value.bypass.state==OMissing,"missing-UIBypassed-and-zero-keyframes-static-endpoint");
        const auto owner=p.owner;p.owner=m.Object(0x265C9388);Check(p.Read(m).state==OUnknown,"wrong-owner-identity-rejected");p.owner=owner;
        m.Entry(p.flags,0,m.keyBypass,3,1);m.forbidden=p.holder+0x50;Check(p.Read(m).state==OUnknown&&!m.forbiddenReads,"bypass-true-does-not-read-default-value");
        m.Set<uint64_t>(p.flags+0x88,0);m.Set<uint64_t>(p.holder+0x10,1);Check(p.Read(m).state==OUnknown&&!m.forbiddenReads,"animated-with-missing-default-stays-unknown");
        m.Entry(p.flags,0,m.keyVary,3,0);m.forbidden=0;Check(p.Read(m).state==ORead,"explicit-not-time-varying-selects-default");
        m.Set<double>(p.holder+0x50,std::numeric_limits<double>::quiet_NaN());Check(p.Read(m).state==OUnknown,"NaN-endpoint-rejected");
        m.Set<double>(p.holder+0x50,2.5);Check(p.Read(m).state==OUnknown,"fractional-endpoint-rejected");
        m.Set<double>(p.holder+0x50,-2);Check(p.Read(m).state==OUnknown,"out-of-domain-endpoint-rejected");
        m.Set<double>(p.holder+0x50,-1);Check(p.Read(m).state==ORead&&p.Read(m).value==-1,"sentinel-minus-one-preserved");
        m.Set<uint64_t>(p.flags+0x88,5);Check(p.Read(m).state==OUnknown,"oversized-fixed-dictionary-rejected");
    }
    {
        Memory m;const auto d=m.Dictionary(),contents=m.Object(0x263D5648);
        m.Set(contents+0x18,m.expectedText);m.Entry(d,0,m.keyMatch,7,contents);
        auto r=m.Reader();auto v=OwnerReadDictionary(r,d,0x2D422098,true);
        Check(v.textIdentityKnown&&v.textIdentity,"matchname-compares-interned-ID-only");
        m.Set(contents+0x18,m.Intern(0xBBBB02));r=m.Reader();v=OwnerReadDictionary(r,d,0x2D422098,true);
        Check(v.textIdentityKnown&&!v.textIdentity,"other-effect-does-not-match");
        m.Entry(d,1,m.keyMatch,7,contents);r=m.Reader();Check(OwnerReadDictionary(r,d,0x2D422098,true).state==OInvalid,"duplicate-metadata-key-unknown");
    }
    {
        Memory m;Chain c(m);auto a=std::make_unique<OwnerSnapshot>();auto b=std::make_unique<OwnerSnapshot>();
        const bool firstRead=c.Collect(m,a.get()),secondRead=c.Collect(m,b.get());
        Check(firstRead&&secondRead,"full-collection-all-synthetic-reads-covered");
        Check(a->chainState==ORead&&a->parentCount==2&&a->parents[0].parentMatched&&a->parents[0].parentContains,"bound-parent-token-and-reverse-membership");
        Check(!std::memcmp(a.get(),b.get(),sizeof(*a)),"identical-two-collections");
        Check(a->filters[1].entries[0].refcount==1&&a->filters[1].entries[0].callbackEmpty&&a->filters[1].entries[0].filterRva==0x193A5340,"raw-refcount-one-filter-is-not-skipped");
        m.Set<uint32_t>(c.filter+0x18,0);c.Collect(m,b.get());
        Check(b->zeroRefcounts==1&&!b->filters[1].entries[0].filterRva,"zero-refcount-does-not-follow-filter");
        Check(std::memcmp(a.get(),b.get(),sizeof(*a))!=0,"changed-metadata-detected");
        m.Set<uintptr_t>(c.manager+0x80,999);c.Collect(m,b.get());Check(!b->focusPairMatched&&!b->parentCount,"stale-focus-token-stops-chain");
        m.Set<uintptr_t>(c.manager+0x80,101);m.Set<uintptr_t>(c.child+0x1F0,999);c.Collect(m,b.get());Check(b->chainState==OInvalid&&!b->parents[0].parentMatched,"stale-parent-token-stops-chain");
        m.Set<uintptr_t>(c.child+0x1F0,202);m.Set<uintptr_t>(c.children,0);c.Collect(m,b.get());Check(b->chainState==OInvalid&&!b->parents[0].parentContains,"missing-reverse-membership-stops-chain");
        m.Set(c.children,c.child);m.Set<uintptr_t>(c.parent+0x1F0,101);m.Set(c.parent+0x1F8,c.child);
        const auto children=m.Alloc();m.Set(children,c.parent);m.Set(c.child+0xA0,children);m.Set(c.child+0xA8,children+8);c.Collect(m,b.get());
        Check(b->chainState==OInvalid&&b->parentCount==2,"parent-cycle-bounded");
        m.Set<uint32_t>(c.child+0x18,0);c.Collect(m,b.get());Check(b->chainState==OUnknown&&!b->parents[0].delegate.pointer,"zero-refcount-chain-unknown");
        m.Set<uintptr_t>(c.manager+0x38,c.filterEntries+257*16);c.Collect(m,b.get());Check(b->filters[1].state==OInvalid&&!b->filters[1].count,"filter-cap-rejected");
    }
    {
        Memory m;const auto manager=m.Object(0x2723DF48),block=m.Alloc();
        m.Set(Memory::base+0x2D9FF6F8,manager);m.Set(Memory::base+0x2D9FF700,manager);m.Set(Memory::base+0x2D9FF708,block);
        m.Set(Memory::base+0x2723DF48+0x68,Memory::base+0x20F08500);m.Set<uint32_t>(manager+0x158,1);m.Set<uint32_t>(block+8,2);
        auto s=std::make_unique<OwnerSnapshot>();OwnerInputs in{};auto r=m.Reader();OwnerCollect(r,in,s.get());
        Check(!r.failed&&s->selection.modeGetterRva==0x20F08500&&s->selection.managerStrongKnown&&s->selection.managerStrong==2,"selection-mode-getter-and-strong-reference-observed");
        m.Set<uint32_t>(block+8,0);r=m.Reader();OwnerCollect(r,in,s.get());
        Check(!r.failed&&s->selection.managerStrongKnown&&!s->selection.managerStrong,"selection-zero-strong-reference-is-not-guessed-live");
    }
    {
        Memory m;const auto key=m.Object(0x263D41B8),table=m.Alloc();
        m.Set(key+0x28,Memory::base+0x263D41C8);m.Set(key+8,Memory::base+0x263D41E0);
        m.Set<int32_t>(Memory::base+0x263D41E4,0x20);m.Set<uintptr_t>(key+0x10,0xAAAA04);
        m.Set(Memory::base+0x263D41C8,Memory::base+0x184E25C8);m.Set(Memory::base+0x263D41D0,Memory::base+0x184E5D48);
        m.Set(Memory::base+0x2D421B98,key);m.Set<uint8_t>(Memory::base+0x2D421EB8,1);
        m.Set(Memory::base+0x2D421EA8,table);m.Set(Memory::base+0x2D421EB0,table+40);
        m.Set(table+0x20,key);m.Set<uint32_t>(table+0x18,7);m.Set<uint8_t>(table+0x10,3);m.Set<uint8_t>(table,0);
        auto r=m.Reader();auto d=OwnerReadDefaultBypass(r);
        Check(d.state==ORead&&d.version==7&&d.booleanKnown&&!d.booleanValue,"static-default-bypass-explicit-bool-false");
        m.Set<uint8_t>(Memory::base+0x2D421EB8,0);r=m.Reader();Check(OwnerReadDefaultBypass(r).state!=ORead,"uninitialized-default-table-unknown");m.Set<uint8_t>(Memory::base+0x2D421EB8,1);
        m.Set(Memory::base+0x263D41C8,Memory::base+0x184E2440);r=m.Reader();Check(OwnerReadDefaultBypass(r).state!=ORead,"key-reference-callback-must-be-exact-no-op");m.Set(Memory::base+0x263D41C8,Memory::base+0x184E25C8);
        m.Set<uint8_t>(table+0x10,7);m.Set<uintptr_t>(table,0x700000000ULL);m.forbidden=0x700000000ULL;r=m.Reader();
        Check(OwnerReadDefaultBypass(r).state!=ORead&&!m.forbiddenReads,"default-nonscalar-payload-never-followed");m.Set<uint8_t>(table+0x10,3);
        m.Set<uint32_t>(table+0x18,0);r=m.Reader();Check(OwnerReadDefaultBypass(r).state!=ORead,"default-version-zero-comparator-path-excluded");m.Set<uint32_t>(table+0x18,7);
        m.Set(Memory::base+0x2D421EB0,table+80);m.Set(table+40+0x20,key);m.Set<uint32_t>(table+40+0x18,8);r=m.Reader();
        Check(OwnerReadDefaultBypass(r).state!=ORead,"duplicate-key-default-versions-unknown");
        m.Set(Memory::base+0x2D421EB0,table+129*40);r=m.Reader();Check(OwnerReadDefaultBypass(r).state!=ORead,"default-table-cap-bounded");
    }
    {
        Memory m;MonitorFixture f(m);auto s=std::make_unique<OwnerSnapshot>();
        Check(f.Collect(m,s.get())&&s->monitor.state==ORead&&s->monitor.currentComponent.pointer==f.component,"monitor-current-entry-is-bound-to-native-window");
        m.Set<uint8_t>(f.dm+0xA58,0);f.Collect(m,s.get());Check(s->monitor.modeOverrideKnown&&!s->monitor.modeOverride,"monitor-mode-override-zero-is-observed-not-default");
        m.Set<uint8_t>(f.dm+0xA58,1);f.Collect(m,s.get());Check(s->monitor.modeOverrideKnown&&s->monitor.modeOverride==1,"monitor-mode-override-one-preserved");
        m.Set<uint8_t>(f.dm+0x142,1);m.Set<uint8_t>(f.dm+0x1FD,1);f.Collect(m,s.get());
        Check(s->monitor.initEnabledKnown&&s->monitor.initEnabled==1&&s->monitor.providerModeKnown&&s->monitor.providerMode==1,"monitor-preinit-bytes-are-observed");
        m.Entry(f.flags,0,m.keyFlag,3,0);f.Collect(m,s.get());
        Check(s->monitor.componentFlag.state==ORead&&s->monitor.componentFlag.booleanKnown&&!s->monitor.componentFlag.booleanValue,"monitor-component-flag-direct-bool-false");
        m.Entry(f.flags,0,m.keyFlag,3,1);f.Collect(m,s.get());Check(s->monitor.componentFlag.booleanKnown&&s->monitor.componentFlag.booleanValue,"monitor-component-flag-direct-bool-true");
        m.Entry(f.flags,0,m.keyFlag,7,0x700000000ULL);m.forbidden=0x700000000ULL;f.Collect(m,s.get());
        Check(s->monitor.componentFlag.state==ORead&&!s->monitor.componentFlag.booleanKnown&&!m.forbiddenReads,"monitor-nonscalar-flag-payload-never-read");
        m.Set<uint32_t>(f.selected+0x38,1234);f.Collect(m,s.get());Check(s->monitor.entryContextKnown&&s->monitor.entryContext==1234,"monitor-current-entry-context-is-observed-scalar");
        m.Set<uint32_t>(f.bg+0x204,0);f.Collect(m,s.get());Check(s->monitor.state!=ORead&&!s->monitor.manipulator.pointer,"wrong-background-kind-does-not-follow-current-entry");m.Set<uint32_t>(f.bg+0x204,0x76636F6D);
        m.Set<uint32_t>(f.entry+0x10,2);f.Collect(m,s.get());Check(s->monitor.state!=ORead&&!s->monitor.manipulator.pointer,"missing-current-map-key-unknown");m.Set<uint32_t>(f.entry+0x10,1);
        m.Set<uintptr_t>(f.dm+0x9F0,1);f.Collect(m,s.get());Check(s->monitor.state!=ORead&&!s->monitor.currentComponent.pointer,"current-entry-index-equals-size-rejected");m.Set<uintptr_t>(f.dm+0x9F0,0);
        m.Set(f.selected+0x10,f.owner+8);f.Collect(m,s.get());Check(s->monitor.state!=ORead,"current-component-owner-mismatch-rejected");m.Set(f.selected+0x10,f.owner);
        m.Set<uintptr_t>(f.selected+0x30,0);f.Collect(m,s.get());Check(s->monitor.state!=ORead,"missing-current-provider-control-block-unknown");
    }
    {
        Memory m;Chain c(m);const auto edit=m.Object(0x265085F0);m.Set<uintptr_t>(edit+0x10,123);m.Set<uint32_t>(edit+0x18,2);
        OwnerInputs in{};in.count=1;in.windows[0]={0x1234,edit};auto s=std::make_unique<OwnerSnapshot>();
        m.forbidden=c.filterEntries;auto r=m.Reader();OwnerCollectNativeEditText(r,in,s.get());
        Check(!r.failed&&!r.limited&&s->kind==OwnerSnapshotKind::NativeEditText&&s->windows[0].propertyToken==123&&s->windows[0].propertyRefcount==2,"native-text-partial-kind-and-fresh-property");
        Check(!m.forbiddenReads&&!s->filters[1].count&&!s->parentCount&&s->selection.state==OUnknown,"native-text-does-not-read-global-input-route");
        Check(r.reads<=24,"native-text-bounded-small-metadata-read-count");
        const auto before=s->windows[0].propertyToken;m.Set<uintptr_t>(edit+0x10,124);r=m.Reader();OwnerCollectNativeEditText(r,in,s.get());
        Check(s->windows[0].propertyToken!=before,"native-text-rereads-current-token-no-cache");
        m.Set<uint32_t>(c.manager+0x120,1);r=m.Reader();OwnerCollectNativeEditText(r,in,s.get());
        Check(s->focusMutationDepth==1,"native-text-retains-focus-transition-guard");
    }
    {
        Memory m;Chain c(m);MonitorFixture f(m);auto s=std::make_unique<OwnerSnapshot>();m.forbidden=c.filterEntries;
        auto r=m.Reader();OwnerCollectCaptionText(r,f.input,s.get());
        Check(!r.failed&&!r.limited&&s->kind==OwnerSnapshotKind::CaptionText&&s->monitor.state==ORead&&s->parentCount==2,"caption-text-partial-keeps-current-entry-and-ancestry");
        Check(!m.forbiddenReads&&s->filters[1].state==OUnknown&&!s->observer.pointer&&!s->specialTree,"caption-text-skips-filter-special-observer-graph");
        m.forbidden=f.component+0x518;m.forbiddenReads=0;r=m.Reader();OwnerCollectCaptionText(r,f.input,s.get());
        Check(!m.forbiddenReads&&s->monitor.componentFlag.state==OUnknown&&s->monitor.defaultFlag.state==OUnknown,"caption-text-skips-command-only-bypass-default-query");
        m.forbidden=c.child+0x428;m.forbiddenReads=0;r=m.Reader();OwnerCollectCaptionText(r,f.input,s.get());
        Check(!m.forbiddenReads&&!s->parents[0].callback&&!s->parents[0].keyRva,"caption-text-skips-command-callback-metadata");
    }
    for(uint32_t vt:{0x272E9598u,0x272E9A18u}){
        for(uint32_t off:{0x168u,0x170u,0x178u}){
            Memory m;const uint32_t expected=off==0x168?0x1E912F10:off==0x170?0x1E912F20:0x1E912250;
            m.Set<uintptr_t>(Memory::base+vt+off,Memory::base+expected+1);
            auto r=m.Reader();uint32_t bad=0;Check(!OwnerVerifySpans(r,&bad)&&bad==vt+off,"audio-changed-relocated-key-slot-rejected");
        }
    }
    std::printf("synthetic_checks=%u failures=%u actual_provider=NOT_EXERCISED\n",checks,failures);
    return failures?1:0;
}
