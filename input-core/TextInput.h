#pragma once
#include "Hangul.h"
namespace rium {
struct Modifiers {bool shift=false,control=false,alt=false,windows=false;};
struct KeyResult {bool eaten=false;bool cancel=false;std::wstring committed;std::wstring composing;};
// Pure composition state for an editable context. This class does not detect
// whether an application's current control is editable and is not a key hook.
// The host must finish the old context before Reset and never write its preedit
// into a newly focused application. Caps Lock is deliberately not a Korean modifier.
class TextInput {
    Hangul hangul;
    bool korean=true;
    static bool ModifierOnly(unsigned key){return key==0x10||key==0x11||key==0x12||key==0x14||key==0x5b||key==0x5c||(key>=0xa0&&key<=0xa5);}
public:
    bool Korean() const{return korean;}
    std::wstring Text() const{return hangul.Text();}
    void Reset(){hangul.Clear();}
    std::wstring Commit(){return hangul.Commit();}
    KeyResult KeyDown(unsigned key,Modifiers modifiers={},bool repeat=false){
        if(ModifierOnly(key))return {false,false,{},hangul.Text()};
        if(modifiers.control||modifiers.alt||modifiers.windows)return {false,false,hangul.Commit(),{}};
        if(key==0x15){if(repeat)return {true,false,{},hangul.Text()};auto text=hangul.Commit();korean=!korean;return {true,false,text,{}};}
        if(!korean)return {false,false,hangul.Commit(),{}};
        if(key==0x08){bool eaten=hangul.Backspace();return {eaten,false,{},hangul.Text()};}
        if(key==0x1b&&!hangul.Text().empty()){hangul.Clear();return {true,true,{},{}};}
        if(key>='A'&&key<='Z'){
            char mapped=static_cast<char>(modifiers.shift?key:key-'A'+'a');
            auto update=hangul.Feed(mapped);return {update.accepted,false,update.committed,update.composing};
        }
        return {false,false,hangul.Commit(),{}};
    }
};
}
