#include "owner-monitor-classifier.h"
#include <cstdio>
#include <memory>
#include "owner-command-blue-entry-v5-witness.inc"
#include "owner-command-blue-entry-v8-focused-witness.inc"
#include "owner-command-red-entry-witness.inc"
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok)++failures;std::printf("%s monitor-proof %s\n",ok?"PASS":"FAIL",name);}
// The positive is the actual same-thread v8 capture. Every later mutation is
// an explicitly synthetic boundary control, not another live observation.
static OwnerCommandEvidence Blue(OwnerSnapshot& s){return BlueEntryV8FocusedWitness(s);}
int main(){auto s=std::make_unique<OwnerSnapshot>();auto e=BlueEntryV5Witness(*s);
    Check(!OwnerMonitorCommand(e),"actual-v5-missing-early-override-proof-is-unknown");
    e=Blue(*s);Check(OwnerMonitorCommand(e),"actual-v8-blue-bound-initialized-bool-default");
    s->monitor.modeOverrideKnown=0;Check(!OwnerMonitorCommand(e),"unobserved-zero-is-unknown");
    e=Blue(*s);s->monitor.modeOverride=0;Check(OwnerMonitorCommand(e),"synthetic-zero-override-bypasses-provider-query");
    s->monitor.entryContext=0;Check(OwnerMonitorCommand(e),"synthetic-earlier-false-return-does-not-initialize-empty-entry");
    e=Blue(*s);s->monitor.defaultFlag.state=OUnknown;Check(!OwnerMonitorCommand(e),"missing-default-proof-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.initializedKnown=0;Check(!OwnerMonitorCommand(e),"unknown-default-initialization-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.initialized=0;Check(!OwnerMonitorCommand(e),"uninitialized-default-table-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.key.vtable++;Check(!OwnerMonitorCommand(e),"dynamic-key-reference-class-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.keyReference.pointer++;Check(!OwnerMonitorCommand(e),"unbound-key-reference-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.keyAddRefRva++;Check(!OwnerMonitorCommand(e),"unknown-key-AddRef-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.keyReleaseRva++;Check(!OwnerMonitorCommand(e),"unknown-key-Release-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.keyVbtableRva++;Check(!OwnerMonitorCommand(e),"unknown-key-reference-offset-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.version=0;Check(!OwnerMonitorCommand(e),"default-version0-value-comparator-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.matches=2;Check(!OwnerMonitorCommand(e),"duplicate-default-key-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.tag=7;Check(!OwnerMonitorCommand(e),"converting-string-default-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.booleanKnown=0;Check(!OwnerMonitorCommand(e),"unknown-default-bool-is-unknown");
    e=Blue(*s);s->monitor.defaultFlag.booleanValue=1;Check(!OwnerMonitorCommand(e),"different-default-bool-is-outside-narrow-proof");
    e=Blue(*s);s->monitor.defaultFlag.value++;Check(!OwnerMonitorCommand(e),"unaligned-default-entry-is-unknown");
    e=Blue(*s);s->monitor.componentFlag.state=ORead;Check(!OwnerMonitorCommand(e),"present-component-property-is-outside-narrow-proof");
    e=Blue(*s);s->monitor.componentFlag.dictionary.vtable++;Check(!OwnerMonitorCommand(e),"different-dictionary-class-is-unknown");
    e=Blue(*s);s->monitor.componentFlagGetterRva++;Check(!OwnerMonitorCommand(e),"different-component-query-is-unknown");
    e=Blue(*s);s->monitor.initEnabledKnown=0;Check(!OwnerMonitorCommand(e),"unknown-initialization-gate-is-unknown");
    e=Blue(*s);s->monitor.providerModeKnown=0;Check(!OwnerMonitorCommand(e),"unknown-provider-mode-is-unknown");
    e=Blue(*s);s->monitor.providerModeGetterRva++;Check(!OwnerMonitorCommand(e),"different-provider-mode-getter-is-unknown");
    e=Blue(*s);s->monitor.providerMode=0;Check(!OwnerMonitorCommand(e),"unproved-context-owner-branch-is-unknown");
    e=Blue(*s);s->monitor.entryContextKnown=0;Check(!OwnerMonitorCommand(e),"unknown-entry-initialization-is-unknown");
    e=Blue(*s);s->monitor.entryContext=0;Check(!OwnerMonitorCommand(e),"plugin-initialization-path-is-unknown");
    e=Blue(*s);s->selection.modeGetterRva++;Check(!OwnerMonitorCommand(e),"changed-mode-getter-unknown");
    e=Blue(*s);s->selection.managerStrongKnown=0;Check(!OwnerMonitorCommand(e),"unknown-manager-reference-count-unknown");
    e=Blue(*s);s->selection.managerStrong=0;Check(!OwnerMonitorCommand(e),"zero-manager-reference-count-unknown");
    e=RedEntryWitness(*s);Check(!OwnerMonitorCommand(e),"actual-red-caption-not-command");
    e=Blue(*s);s->selection.mode=2;Check(!OwnerMonitorCommand(e),"Type-tool-with-minus-one-endpoints-unknown");
    e=Blue(*s);s->selection.endpoints[0].value=0;Check(!OwnerMonitorCommand(e),"mode1-with-positive-text-endpoint-unknown");
    e=Blue(*s);s->monitor.currentComponent.pointer++;Check(!OwnerMonitorCommand(e),"current-component-must-match-selected-effect");
    e=Blue(*s);s->monitor.currentProvider.state=OUnsupported;Check(!OwnerMonitorCommand(e),"unknown-provider-class-unknown");
    e=Blue(*s);s->parents[1].delegate.vtable++;Check(!OwnerMonitorCommand(e),"unknown-background-ancestor-handler");
    e=Blue(*s);s->parents[4].callbackEmpty=0;Check(!OwnerMonitorCommand(e),"ancestor-callback-excludes-proof");
    e=Blue(*s);s->filters[1].entries[0].callbackEmpty=0;Check(!OwnerMonitorCommand(e),"refcount-one-filter-callback-excludes-proof");
    e=Blue(*s);s->parents[6].handler.pointer=0x1234;Check(!OwnerMonitorCommand(e),"top-level-alternate-handler-excludes-proof");
    e=Blue(*s);e.nativeFocus++;Check(!OwnerMonitorCommand(e),"different-native-focus-unknown");
    e=Blue(*s);e.mappedCharacter=':';Check(!OwnerMonitorCommand(e),"numeric-character-exception-excluded");
    e=Blue(*s);s->modifiers=1;Check(!OwnerMonitorCommand(e),"modifier-hover-route-excluded");
    e=Blue(*s);e.samplesEqual=0;Check(!OwnerMonitorCommand(e),"changed-metadata-excluded");
    e=Blue(*s);s->specialTreeEmpty=0;Check(!OwnerMonitorCommand(e),"special-handler-excluded");
    std::printf("monitor_proof_checks=%u failures=%u live_v8_positive=ACTUAL_CAPTURE routing=NOT_INSTALLED\n",checks,failures);return failures?1:0;}
