#include "fsm.h"
#include "layout.h"
#include "comp_path.h"
#include "transition.h"
#include <stdio.h>
#include <string.h>
static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL: %s\n", name); }
}
static void phrase(const wchar_t *keys, const wchar_t *expected, const char *name) {
    FsmContext fsm; Fsm_Init(&fsm);
    wchar_t out[256] = {0}; size_t used = 0;
    for (const wchar_t *p = keys; *p; ++p) {
        FsmResult step = Fsm_ProcessKey(&fsm, *p, KBD_DUBEOL, NULL);
        if (step.commitChar) out[used++] = step.commitChar;
        if (!step.eaten) out[used++] = *p;
    }
    wchar_t last = Fsm_Flush(&fsm); if (last) out[used++] = last;
    check(wcscmp(out, expected) == 0, name);
    check(Fsm_Flush(&fsm) == 0, "flush cannot duplicate text");
}
int main(void) {
    phrase(L"gksrmf", L"\ud55c\uae00", "Hangul");
    phrase(L"dksl", L"\uc544\ub2c8", "split final consonant");
    phrase(L"rhk", L"\uacfc", "compound vowel");
    phrase(L"rkqt", L"\uac12", "compound final");
    phrase(L"rkqtdl", L"\uac12\uc774", "compound final then next syllable");
    phrase(L"rkrtk", L"\uac01\uc0ac", "split compound final");
    phrase(L"Rk", L"\uae4c", "shifted consonant");
    phrase(L"dml", L"\uc758", "ui vowel");
    phrase(L"gksrmf dlqfur 123!", L"\ud55c\uae00 \uc785\ub825 123!", "Korean with ASCII boundaries");
    FsmContext fsm; Fsm_Init(&fsm);
    for (const wchar_t *p = L"gks"; *p; ++p) Fsm_ProcessKey(&fsm, *p, 0, NULL);
    wchar_t preedit;
    check(Fsm_Backspace(&fsm, &preedit) && preedit == L'\ud558', "backspace final");
    check(Fsm_Backspace(&fsm, &preedit) && preedit == L'\u314e', "backspace vowel");
    check(Fsm_Backspace(&fsm, &preedit) && !preedit, "backspace initial");
    check(!Fsm_Backspace(&fsm, &preedit), "empty backspace belongs to application");
    check(JamoPath_Decide(0, JAMO_TS_SS_TRANSITORY, 1, 1, 0) == JAMO_PATH_COMMIT,
          "legacy CUAS context uses commit path");
    check(JamoPath_Decide(0, 0, 1, 1, 0) == JAMO_PATH_STANDARD,
          "native TSF context uses inline path");
    check(!Trans_PendingMatches((void*)1, NULL, (void*)2, (void*)3),
          "pending syllable never follows another field");
    check(Trans_ResendRoute(1, 1, 0, 0) == TRANS_RESEND_DROP,
          "boundary key never enters newly focused non-edit window");
    printf("Reused engine: %d checks, %d failures.\n", checks, failures);
    return failures ? 1 : 0;
}
