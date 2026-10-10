#include "ae-owner-runtime.h"
#include <cstdio>
#include <cstring>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok)++failures;std::printf("%s %s\n",ok?"PASS":"FAIL",name);}
static DWORD WINAPI InitializeOnWorker(void*){return RiumAeOwner_Initialize()?1:0;}
int main(){
    const bool own=GetModuleHandleW(L"AfterFXLib.dll")==nullptr&&GetModuleHandleW(L"BEE.dll")==nullptr;
    Check(own,"lifecycle-fixture-is-not-an-Adobe-process");if(!own)return 1;
    RiumOwnerStamp stamp{};std::memset(&stamp,0xA5,sizeof(stamp));RiumAeOwner_Capture(nullptr,&stamp);
    Check(!stamp.provider&&!stamp.kind&&!stamp.textObject,"capture-before-init-clears-output");
    const HANDLE worker=CreateThread(nullptr,0,InitializeOnWorker,nullptr,0,nullptr);DWORD result=9;
    const bool joined=worker&&WaitForSingleObject(worker,5000)==WAIT_OBJECT_0&&GetExitCodeThread(worker,&result);
    Check(joined&&result==0,"unsupported-own-process-init-fails-closed-on-worker");
    Check(worker&&CloseHandle(worker),"initialization-worker-handle-closed");
    if(!joined)return 1;
    std::memset(&stamp,0xA5,sizeof(stamp));RiumAeOwner_Capture(nullptr,&stamp);
    Check(!stamp.provider&&!stamp.kind&&!stamp.windowObject&&!stamp.logicalObject&&!stamp.textObject&&!stamp.lifetimeToken,
        "failed-init-does-not-retain-partial-owner");
    RiumAeOwner_Shutdown();RiumAeOwner_Shutdown();RiumAeOwner_Capture(nullptr,&stamp);
    Check(!stamp.provider&&!stamp.kind,"shutdown-is-repeatable-after-failed-init");
    RiumAeOwner_Capture(nullptr,nullptr);
    Check(!GetModuleHandleW(L"AfterFXLib.dll")&&!GetModuleHandleW(L"BEE.dll"),"adapter-never-loads-host-modules");
    std::printf("ae_lifecycle checks=%u failures=%u windows=0 externalTarget=NOT_EXERCISED\n",checks,failures);
    return failures?1:0;
}
