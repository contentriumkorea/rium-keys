#include "ae-owner-runtime-policy.h"
#include <cstring>
void AeMakeOwnerStamp(const AeSnapshot& a,const AeSnapshot& b,bool validCapture,
    bool plainLetterDomain,DWORD pid,DWORD tid,HWND focus,RiumOwnerStamp* out){
    if(!out)return;std::memset(out,0,sizeof(*out));
    out->provider=3;out->profile=1;out->processId=pid;out->threadId=tid;out->focus=focus;
    if(!pid||!tid||!focus||!a.windowCount||a.windows[0].hwnd!=reinterpret_cast<uintptr_t>(focus))return;
    const auto e=AeClassifyCurrentOwner(a,b,validCapture);
    if(e.decision==AeOwnerUnknown||(e.decision==AeOwnerCommand&&!plainLetterDomain))return;
    out->kind=e.decision;out->windowObject=a.windows[0].property.pointer;out->lifetimeToken=a.focusToken;
    if(e.decision==AeOwnerText){out->logicalObject=a.canvas.layer.pointer;out->textObject=a.canvas.currentTransaction;}
    else out->logicalObject=a.focusNode;
}
