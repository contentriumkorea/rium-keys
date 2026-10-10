#include "Hangul.h"
#include "TextInput.h"
#include <iostream>
#include <stdexcept>
using rium::Hangul;
static int checks=0;
static void Require(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
static std::wstring Type(const std::string& keys){Hangul engine;std::wstring text;for(char key:keys)text+=engine.Feed(key).committed;return text+engine.Commit();}
int main(){
    try{
        Require(Type("gksrmf")==L"한글","hangul word");
        Require(Type("gkrry")==L"학교","repeated consonants must not form an implicit tense initial");
        Require(Type("dkssudgktpdy")==L"안녕하세요","greeting");
        Require(Type("rkqtdl")==L"값이","compound final followed by explicit initial");
        Require(Type("rkqtk")==L"갑사","compound final splits before vowel");
        Require(Type("dlfrrl")==L"읽기","compound final followed by consonant");
        Require(Type("rrk")==L"ㄱ가","two initial consonants");
        Require(Type("Rk")==L"까","shift tense initial");
        Require(Type("rhk")==L"과","compound vowel");
        Require(Type("dml")==L"의","eu plus i");
        Require(Type("hk")==L"ㅘ","standalone compound vowel");
        Require(Type("rkrk")==L"가가","single final moves to following syllable");
        Hangul engine;for(char key:std::string("rkqt"))engine.Feed(key);
        Require(engine.Text()==L"값","preedit final");
        engine.Backspace();Require(engine.Text()==L"갑","remove second compound final");
        engine.Backspace();Require(engine.Text()==L"가","remove simple final");
        engine.Backspace();Require(engine.Text()==L"ㄱ","remove vowel");
        engine.Backspace();Require(engine.Text().empty(),"remove initial");
        Require(!engine.Backspace(),"empty backspace passes through");
        for(char key:std::string("rhk"))engine.Feed(key);
        engine.Backspace();Require(engine.Text()==L"고","compound vowel backspace");
        auto committed=engine.Commit();Require(committed==L"고"&&engine.Text().empty(),"commit clears only pending preedit");
        engine.Feed('r');auto ignored=engine.Feed('1');Require(!ignored.accepted&&engine.Text()==L"ㄱ","unhandled key does not mutate composition");
        engine.Clear();Require(engine.Text().empty(),"cancel clears preedit");
        Require(engine.Commit().empty()&&engine.Commit().empty(),"empty commit is idempotent");
        engine.Clear();engine.Clear();Require(engine.Text().empty(),"cancel is idempotent");
        rium::TextInput input;
        input.KeyDown('R');input.KeyDown('K');auto shiftOnly=input.KeyDown(0x10);
        Require(!shiftOnly.eaten&&input.Text()==L"가","Shift alone preserves preedit");
        auto tenseFinal=input.KeyDown('T',{true});Require(tenseFinal.eaten&&tenseFinal.composing==L"갔","shifted final participates in existing syllable");
        input.Reset();input.KeyDown('R');auto digit=input.KeyDown('1');auto afterDigit=input.KeyDown('K');
        Require(!digit.eaten&&digit.committed==L"ㄱ"&&afterDigit.composing==L"ㅏ","digit separates compositions");
        input.Reset();input.KeyDown('R');auto punctuation=input.KeyDown(0xbe);input.KeyDown('K');
        Require(!punctuation.eaten&&punctuation.committed==L"ㄱ"&&input.Text()==L"ㅏ","punctuation separates compositions");
        input.Reset();input.KeyDown('R');input.KeyDown('K');auto shortcut=input.KeyDown('C',{false,true});
        Require(!shortcut.eaten&&shortcut.committed==L"가"&&input.Text().empty(),"control shortcut commits then passes through");
        auto latin=input.KeyDown(0x15);Require(latin.eaten&&!input.Korean(),"Hangul toggle enters Latin mode");
        auto repeatToggle=input.KeyDown(0x15,{},true);Require(repeatToggle.eaten&&!input.Korean(),"autorepeat does not toggle mode repeatedly");
        auto modifiedToggle=input.KeyDown(0x15,{false,true});Require(!modifiedToggle.eaten&&!input.Korean(),"modified Hangul chord passes through");
        Require(!input.KeyDown('R').eaten&&input.Text().empty(),"Latin mode passes through letters");
        input.KeyDown(0x15);input.KeyDown('R');Require(input.Text()==L"ㄱ","uppercase VK is not physical Shift or Caps Lock");
        auto escape=input.KeyDown(0x1b);Require(escape.eaten&&escape.cancel&&input.Text().empty(),"escape cancels pending composition");
        Require(!input.KeyDown(0x08).eaten,"backspace without composition passes through");
        const char* compoundFinals[]={"rkrt","rksw","rksg","rkfr","rkfa","rkfq","rkft","rkfx","rkfv","rkfg","rkqt"};
        const wchar_t* splitFinals[]={L"각사",L"간자",L"간하",L"갈가",L"갈마",L"갈바",L"갈사",L"갈타",L"갈파",L"갈하",L"갑사"};
        const wchar_t* reducedFinals[]={L"각",L"간",L"간",L"갈",L"갈",L"갈",L"갈",L"갈",L"갈",L"갈",L"갑"};
        for(int i=0;i<11;++i){Require(Type(std::string(compoundFinals[i])+"k")==splitFinals[i],"every compound-final split");Hangul h;for(char c:std::string(compoundFinals[i]))h.Feed(c);h.Backspace();Require(h.Text()==reducedFinals[i],"every compound-final deletion");}
        const char* compoundVowels[]={"rhk","rho","rhl","rnj","rnp","rnl","rml"};
        for(int i=0;i<7;++i){Hangul h;for(char c:std::string(compoundVowels[i]))h.Feed(c);h.Backspace();Require(h.Text()==(i<3?L"고":i<6?L"구":L"그"),"every compound-vowel deletion");}
        const char* initials[]={"r","R","s","e","E","f","a","q","Q","t","T","d","w","W","c","z","x","v","g"};
        const char* vowels[]={"k","o","i","O","j","p","u","P","h","hk","ho","hl","y","n","nj","np","nl","b","m","ml","l"};
        const char* finals[]={"","r","R","rt","s","sw","sg","e","f","fr","fa","fq","ft","fx","fv","fg","a","q","qt","t","T","d","w","c","z","x","v","g"};
        for(int l=0;l<19;++l)for(int v=0;v<21;++v)for(int t=0;t<28;++t){
            auto actual=Type(std::string(initials[l])+vowels[v]+finals[t]);
            Require(actual==std::wstring(1,static_cast<wchar_t>(0xac00+(l*21+v)*28+t)),"all 11172 modern syllables");
        }
        std::cout<<"PASS: "<<checks<<" Korean composition checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
