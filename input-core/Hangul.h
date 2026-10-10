#pragma once
#include <string>
namespace rium {
struct Update {bool accepted=false;std::wstring committed;std::wstring composing;};
// Two-beolsik composition only. No OS calls, process names, timing or key hooks.
// Uppercase input means physical Shift, not Caps Lock. Commit before printable
// non-Hangul keys, command chords or navigation; never commit on Shift alone.
class Hangul {
    int initial=-1,vowel=-1,final=0;
    static constexpr wchar_t initials[]=L"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";
    static constexpr int finalForInitial[]={1,2,4,7,0,8,16,17,0,19,20,21,22,0,23,24,25,26,27};
    static constexpr int initialForFinal[]={-1,0,1,-1,2,-1,-1,3,5,-1,-1,-1,-1,-1,-1,-1,6,7,-1,9,10,11,12,14,15,16,17,18};
    struct Pair {int first,second,result;};
    static constexpr Pair vowelPairs[]={{8,0,9},{8,1,10},{8,20,11},{13,4,14},{13,5,15},{13,20,16},{18,20,19}};
    static constexpr Pair finalPairs[]={{1,9,3},{4,12,5},{4,18,6},{8,0,9},{8,6,10},{8,7,11},{8,9,12},{8,16,13},{8,17,14},{8,18,15},{17,9,18}};
    static int Consonant(char key){
        switch(key){
        case 'r':return 0;case 'R':return 1;case 's':case 'S':return 2;case 'e':return 3;case 'E':return 4;
        case 'f':case 'F':return 5;case 'a':case 'A':return 6;case 'q':return 7;case 'Q':return 8;case 't':return 9;case 'T':return 10;
        case 'd':case 'D':return 11;case 'w':return 12;case 'W':return 13;case 'c':case 'C':return 14;case 'z':case 'Z':return 15;
        case 'x':case 'X':return 16;case 'v':case 'V':return 17;case 'g':case 'G':return 18;default:return -1;}
    }
    static int Vowel(char key){
        switch(key){
        case 'k':case 'K':return 0;case 'o':return 1;case 'i':case 'I':return 2;case 'O':return 3;case 'j':case 'J':return 4;case 'p':return 5;
        case 'u':case 'U':return 6;case 'P':return 7;case 'h':case 'H':return 8;case 'y':case 'Y':return 12;case 'n':case 'N':return 13;
        case 'b':case 'B':return 17;case 'm':case 'M':return 18;case 'l':case 'L':return 20;default:return -1;}
    }
public:
    std::wstring Text() const {
        if(initial>=0&&vowel>=0)return std::wstring(1,static_cast<wchar_t>(0xac00+(initial*21+vowel)*28+final));
        if(initial>=0)return std::wstring(1,initials[initial]);
        if(vowel>=0)return std::wstring(1,static_cast<wchar_t>(0x314f+vowel));
        return {};
    }
    void Clear(){initial=-1;vowel=-1;final=0;}
    std::wstring Commit(){auto text=Text();Clear();return text;}
    Update Feed(char key){
        const int c=Consonant(key),v=Vowel(key);if(c<0&&v<0)return {false,{},Text()};
        std::wstring committed;
        if(c>=0){
            if(initial>=0&&vowel>=0){
                if(final==0&&finalForInitial[c]!=0)final=finalForInitial[c];
                else {
                    int combined=0;for(auto pair:finalPairs)if(pair.first==final&&pair.second==c)combined=pair.result;
                    if(combined)final=combined;else {committed=Commit();initial=c;}
                }
            }else {committed=Commit();initial=c;}
        }else if(final!=0){
            int remaining=0,moved=initialForFinal[final];
            for(auto pair:finalPairs)if(pair.result==final){remaining=pair.first;moved=pair.second;break;}
            final=remaining;committed=Text();Clear();initial=moved;vowel=v;
        }else if(vowel>=0){
            int combined=-1;for(auto pair:vowelPairs)if(pair.first==vowel&&pair.second==v)combined=pair.result;
            if(combined>=0)vowel=combined;else {committed=Commit();vowel=v;}
        }else vowel=v;
        return {true,committed,Text()};
    }
    bool Backspace(){
        if(final!=0){for(auto pair:finalPairs)if(pair.result==final){final=pair.first;return true;}final=0;return true;}
        if(vowel>=0){for(auto pair:vowelPairs)if(pair.result==vowel){vowel=pair.first;return true;}vowel=-1;return true;}
        if(initial>=0){initial=-1;return true;}return false;
    }
};
}
