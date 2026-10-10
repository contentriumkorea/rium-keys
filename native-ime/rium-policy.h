#pragma once
#include "third_party/jamotong/src/config.h"

// The local preview reuses composition, not upstream global commands or helpers.
// Apply after loading a file as well, so an old/imported config cannot undo it.
static inline void Rium_ApplyPolicy(JamotongConfig *config) {
    config->options.useUiHelper = false;
    for(int i=0;i<config->layoutCount;++i){
        if(config->layouts[i].enabled && config->layouts[i].type==LAYOUT_TYPE_KOREAN_FSM && config->layouts[i].kbdVariant==0){
            config->currentLayoutIndex=i;
            break;
        }
    }
    memset(config->shortcuts, 0, sizeof(config->shortcuts));
    ShortcutList *rotate = &config->shortcuts[SC_FN_ROTATE];
    rotate->count = 2;
    rotate->keys[0].vKey = VK_HANGUL;
    rotate->keys[1].vKey = VK_RMENU;
}
