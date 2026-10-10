// comp_inline.c — Standard TSF composition and Korean interim selection for CUAS.
//   외부 종료(sink)·세션 실패는 감추지 않는다 — forget + FSM 리셋 + 강등 카운트.
#include "comp_inline.h"
#include "edit_session.h"   // JamoDiag

// StartComposition이 S_OK인데 NULL을 준 경우를 실패로 다룬다 (lab §7.1과 동일).
#define JAMO_E_COMPOSITION_REJECTED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201)

typedef enum { COMP_OP_UPDATE = 0, COMP_OP_FINALIZE = 1, COMP_OP_CANCEL = 2,
               COMP_OP_SPACE = 3 } CompOp;

typedef struct InlineEditSession {
    ITfEditSessionVtbl *lpVtbl;
    LONG refCount;
    JamotongTextService *svc;
    ITfContext *ctx;
    CompOp op;
    wchar_t commit;    // COMP_OP_UPDATE: 확정 음절 (0=없음)
    wchar_t preedit;   // COMP_OP_UPDATE: 조합 중 음절 (0=없음)
    HWND focusHwnd;   // Captured before any reentrant host call.
    ITfComposition *expectedComposition; // Bind queued finalization to its original document.
} InlineEditSession;

// A retained, different document can finish after focus leaves its window.
// Prove distinct COM identity; a shared CUAS context must still match its HWND.
static BOOL SessionOwnsTarget(InlineEditSession *es) {
    if (es->focusHwnd && es->focusHwnd == GetFocus()) return TRUE;
    if (es->op != COMP_OP_FINALIZE || !es->svc->threadMgr) return FALSE;
    ITfDocumentMgr *doc=NULL;ITfContext *current=NULL;
    IUnknown *oldId=NULL,*newId=NULL;BOOL distinct=FALSE;
    if (SUCCEEDED(es->svc->threadMgr->lpVtbl->GetFocus(es->svc->threadMgr,&doc)) && doc &&
        SUCCEEDED(doc->lpVtbl->GetTop(doc,&current)) && current && current != es->ctx &&
        SUCCEEDED(es->ctx->lpVtbl->QueryInterface(es->ctx,&IID_IUnknown,(void**)&oldId)) && oldId &&
        SUCCEEDED(current->lpVtbl->QueryInterface(current,&IID_IUnknown,(void**)&newId)) && newId)
        distinct=oldId != newId;
    if(newId)newId->lpVtbl->Release(newId);
    if(oldId)oldId->lpVtbl->Release(oldId);
    if(current)current->lpVtbl->Release(current);
    if(doc)doc->lpVtbl->Release(doc);
    return distinct;
}

// ── 내부 헬퍼 ────────────────────────────────────────────────────────────────────

// 로컬 composition 참조 정리 (문서 상태는 건드리지 않음).
static void ForgetComposition(JamotongTextService *svc) {
    ITfComposition *composition = svc->pComposition;
    ITfContext *context = svc->pCompContext;
    // Release can call back into the service. Detach the complete old state
    // first so that cleanup cannot release or erase a replacement composition.
    svc->pComposition = NULL;
    svc->pCompContext = NULL;
    svc->compUpdatedOnce = FALSE;
    svc->compFinalizePending = FALSE;
    svc->compBoundaryWritten = FALSE;
    svc->pCompFocusHwnd = NULL;
    if (composition) composition->lpVtbl->Release(composition);
    if (context) context->lpVtbl->Release(context);
}

// Missing from the pinned MinGW headers/uuid library. Value from Microsoft's
// dotnet/wpf UnsafeNativeMethodsTextServices.cs: GUID_COMPARTMENT_TRANSITORYEXTENSION_PARENT.
static const GUID kTransitoryExtensionParent =
    {0x8be347f8,0xc7a0,0x11d7,{0xb4,0x08,0x00,0x06,0x5b,0x84,0x43,0x5c}};

// Chromium's native TSF store also reports TRANSITORY. Only use the Korean
// interim-character compatibility style when this document positively exposes
// an existing transitory-extension parent. This is a display choice, never an
// assertion about whether the app wants text or a command. Unknown => caret.
static BOOL HasTransitoryExtensionParent(ITfContext *ctx) {
    TF_STATUS status = {0};
    if (ctx->lpVtbl->GetStatus(ctx, &status) != S_OK ||
        !(status.dwStaticFlags & JAMO_TS_SS_TRANSITORY)) return FALSE;

    ITfDocumentMgr *doc = NULL;
    ITfCompartmentMgr *manager = NULL;
    IEnumGUID *items = NULL;
    ITfCompartment *compartment = NULL;
    VARIANT value;
    VariantInit(&value);
    BOOL found = FALSE, interim = FALSE;
    if (ctx->lpVtbl->GetDocumentMgr(ctx, &doc) != S_OK || !doc) goto done;
    if (doc->lpVtbl->QueryInterface(doc, &IID_ITfCompartmentMgr, (void**)&manager) != S_OK || !manager)
        goto done;
    if (manager->lpVtbl->EnumCompartments(manager, &items) != S_OK || !items) goto done;
    // GetCompartment can create a missing compartment. Enumerate first, and
    // bound work if a host supplies a malformed or never-ending enumerator.
    for (unsigned i = 0; i < 256; ++i) {
        GUID guid;
        ULONG fetched = 0;
        if (items->lpVtbl->Next(items, 1, &guid, &fetched) != S_OK || fetched != 1) break;
        if (IsEqualGUID(&guid, &kTransitoryExtensionParent)) { found = TRUE; break; }
    }
    if (!found || manager->lpVtbl->GetCompartment(manager, &kTransitoryExtensionParent, &compartment) != S_OK ||
        !compartment) goto done;
    // The predefined-compartment contract describes an IUnknown parent value.
    // Require that representation, not the inconsistent VT_I4 table label.
    // CUAS can expose an opaque parent that rejects ITfDocumentMgr QI; do not
    // query or dereference it merely to choose a selection presentation.
    interim = compartment->lpVtbl->GetValue(compartment, &value) == S_OK &&
              value.vt == VT_UNKNOWN && value.punkVal != NULL;
done:
    VariantClear(&value);
    if (compartment) compartment->lpVtbl->Release(compartment);
    if (items) items->lpVtbl->Release(items);
    if (manager) manager->lpVtbl->Release(manager);
    if (doc) doc->lpVtbl->Release(doc);
    return interim;
}

// Marked transitory extensions retain the one-character CUAS interim style.
// Native and unconfirmed contexts use a collapsed caret with fInterimChar=FALSE.
static HRESULT SelectRangeEnd(InlineEditSession *es, TfEditCookie ec, ITfRange *range, BOOL interim) {
    ITfContext *ctx=es->ctx;
    ITfRange *pEnd = NULL;
    HRESULT hr = range->lpVtbl->Clone(range, &pEnd);
    if (SUCCEEDED(hr) && pEnd) {
        hr = pEnd->lpVtbl->Collapse(pEnd, ec, TF_ANCHOR_END);
        if (SUCCEEDED(hr) && interim) {
            LONG moved = 0;
            hr = pEnd->lpVtbl->ShiftStart(pEnd, ec, -1, &moved, NULL);
            if (SUCCEEDED(hr) && moved != -1) hr = E_FAIL;
        }
        if (SUCCEEDED(hr)) {
            TF_SELECTION sel;
            sel.range = pEnd;
            sel.style.ase = interim ? TF_AE_NONE : TF_AE_END;
            sel.style.fInterimChar = interim;
            hr = SessionOwnsTarget(es) ? ctx->lpVtbl->SetSelection(ctx, ec, 1, &sel) : E_PENDING;
        }
        pEnd->lpVtbl->Release(pEnd);
    }
    return hr;
}

// 조합 캐럿 화면 rect 캡처(팝업 위치용) — edit_session.c CaptureCaretRect와 동일한 폴백 계약.
static void CaptureCaret(JamotongTextService *svc, ITfContext *ctx, TfEditCookie ec) {
    svc->lastCaretValid = FALSE;
    TF_SELECTION sel; ULONG fetched = 0;
    if (FAILED(ctx->lpVtbl->GetSelection(ctx, ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched == 0)
        return;
    ITfContextView *pView = NULL;
    if (SUCCEEDED(ctx->lpVtbl->GetActiveView(ctx, &pView)) && pView) {
        RECT rc; BOOL clipped = FALSE;
        if (SUCCEEDED(pView->lpVtbl->GetTextExt(pView, ec, sel.range, &rc, &clipped))
            && (rc.right - rc.left >= 0) && (rc.bottom - rc.top > 0)) {
            svc->lastCaretRect = rc;
            svc->lastCaretValid = TRUE;
        }
        pView->lpVtbl->Release(pView);
    }
    sel.range->lpVtbl->Release(sel.range);
}

// composition 확보: 있으면 range 재획득(재사용 = 생존 → 강등 카운트 리셋), 없으면 생성.
// 전부 성공한 뒤에만 svc->pComposition에 publish. 실패 시 만든 것은 rollback(End)한다.
static HRESULT EnsureComposition(InlineEditSession *es, TfEditCookie ec, ITfRange **rangeOut) {
    JamotongTextService *svc = es->svc;
    *rangeOut = NULL;

    if (!SessionOwnsTarget(es)) return E_PENDING;

    if (svc->pComposition && svc->pCompContext != es->ctx) return E_UNEXPECTED;

    if (svc->pComposition) {
        HRESULT hr = svc->pComposition->lpVtbl->GetRange(svc->pComposition, rangeOut);
        if (SUCCEEDED(hr)) {
            svc->compUpdatedOnce = TRUE;   // 갱신에서 생존한 조합 — 이 컨텍스트는 건강
            svc->pathDemerits = 0;
        }
        return hr;
    }

    ITfInsertAtSelection *pIns = NULL;
    ITfRange *insRange = NULL;
    ITfContextComposition *pCC = NULL;
    ITfComposition *created = NULL;
    HRESULT hr = es->ctx->lpVtbl->QueryInterface(es->ctx, &IID_ITfInsertAtSelection, (void**)&pIns);
    if (FAILED(hr)) goto done;
    if (!SessionOwnsTarget(es)) { hr=E_PENDING; goto done; }
    // TF_IAS_QUERYONLY: 텍스트 없이 삽입 지점 range만 얻는다.
    hr = pIns->lpVtbl->InsertTextAtSelection(pIns, ec, TF_IAS_QUERYONLY, NULL, 0, &insRange);
    if (FAILED(hr) || !insRange) { if (SUCCEEDED(hr)) hr = E_UNEXPECTED; goto done; }
    hr = es->ctx->lpVtbl->QueryInterface(es->ctx, &IID_ITfContextComposition, (void**)&pCC);
    if (FAILED(hr)) goto done;
    if (!SessionOwnsTarget(es)) { hr=E_PENDING; goto done; }
    // 실제 sink 필수 — NULL sink는 일부 경로에서 E_INVALIDARG로 실패한다(실기 교훈).
    hr = pCC->lpVtbl->StartComposition(pCC, ec, insRange,
                                       (ITfCompositionSink*)&svc->lpVtblCompSink, &created);
    JamoDiag("COMP start hr=0x%08lX p=%p", (unsigned long)hr, (void*)created);
    if (SUCCEEDED(hr) && created == NULL) { hr = JAMO_E_COMPOSITION_REJECTED; goto done; }
    if (FAILED(hr)) goto done;
    hr = created->lpVtbl->GetRange(created, rangeOut);
    if (FAILED(hr)) goto done;
    if (!SessionOwnsTarget(es)) { hr=E_PENDING; if(*rangeOut){(*rangeOut)->lpVtbl->Release(*rangeOut);*rangeOut=NULL;} goto done; }

    svc->pComposition = created;   // publish (참조 이관)
    created = NULL;
    svc->pCompContext = es->ctx;
    es->ctx->lpVtbl->AddRef(es->ctx);
    svc->pCompFocusHwnd = es->focusHwnd;
    svc->compUpdatedOnce = FALSE;

done:
    if (FAILED(hr) && created) created->lpVtbl->EndComposition(created, ec);   // rollback
    if (created) created->lpVtbl->Release(created);
    if (pCC) pCC->lpVtbl->Release(pCC);
    if (insRange) insRange->lpVtbl->Release(insRange);
    if (pIns) pIns->lpVtbl->Release(pIns);
    return hr;
}

// 확정 prefix(commitLen자)를 composition 밖으로 민다 (lab §7.3: ShiftStart, 부분 이동=실패).
static HRESULT CommitPrefix(InlineEditSession *es, TfEditCookie ec, LONG commitLen) {
    JamotongTextService *svc = es->svc;
    if (!svc->pComposition) return S_OK; // Host already committed during SetSelection.
    ITfComposition *composition = svc->pComposition;
    composition->lpVtbl->AddRef(composition);
    ITfRange *whole = NULL, *newStart = NULL;
    LONG moved = 0;
    HRESULT hr = composition->lpVtbl->GetRange(composition, &whole);
    if (FAILED(hr)) goto done;
    hr = whole->lpVtbl->Clone(whole, &newStart);
    if (FAILED(hr)) goto done;
    hr = newStart->lpVtbl->ShiftStart(newStart, ec, commitLen, &moved, NULL);
    if (FAILED(hr) || moved != commitLen) { if (SUCCEEDED(hr)) hr = E_FAIL; goto done; }
    hr = newStart->lpVtbl->Collapse(newStart, ec, TF_ANCHOR_START);
    if (FAILED(hr)) goto done;
    if (svc->pComposition == composition)
        hr = composition->lpVtbl->ShiftStart(composition, ec, newStart);
done:
    if (newStart) newStart->lpVtbl->Release(newStart);
    if (whole) whole->lpVtbl->Release(whole);
    composition->lpVtbl->Release(composition);
    return hr;
}

// ── 편집 세션 본체 (한 키 = 한 동기 트랜잭션) ───────────────────────────────────────
static HRESULT DoInlineWork(InlineEditSession *es, TfEditCookie ec) {
    JamotongTextService *svc = es->svc;

    if (es->expectedComposition && es->expectedComposition != svc->pComposition) return S_FALSE;
    if (!SessionOwnsTarget(es) || (svc->pComposition && svc->pCompContext != es->ctx))
        return E_PENDING; // A queued cookie cannot authorize writing into a newly focused control.

    if (es->op == COMP_OP_FINALIZE || es->op == COMP_OP_CANCEL) {
        if (!svc->pComposition) return S_OK;
        if (es->expectedComposition && es->expectedComposition != svc->pComposition) return S_FALSE;
        ITfComposition *composition = svc->pComposition;
        composition->lpVtbl->AddRef(composition);
        ITfRange *range = NULL;
        HRESULT hr = composition->lpVtbl->GetRange(composition, &range);
        if (SUCCEEDED(hr) && !range) hr = E_UNEXPECTED;
        if (SUCCEEDED(hr) && range) {
            if (es->op == COMP_OP_CANCEL) {
                hr = SessionOwnsTarget(es) ? range->lpVtbl->SetText(range, ec, 0, L"", 0) : E_PENDING;
            } else {
                hr = SelectRangeEnd(es, ec, range, FALSE);
            }
            range->lpVtbl->Release(range);
        }
        // Do not leave an interim character selected and claim finalization succeeded.
        if (FAILED(hr)) { composition->lpVtbl->Release(composition); return hr; }
        HRESULT endHr = S_OK;
        if (svc->pComposition == composition && SessionOwnsTarget(es))
            endHr = composition->lpVtbl->EndComposition(composition, ec);
        else if (svc->pComposition == composition) endHr=E_PENDING;
        JamoDiag("COMP %s end hr=0x%08lX", es->op == COMP_OP_CANCEL ? "cancel" : "finalize",
                 (unsigned long)endHr);
        if (SUCCEEDED(endHr) && svc->pComposition == composition) ForgetComposition(svc);
        composition->lpVtbl->Release(composition);
        CaptureCaret(svc, es->ctx, ec);
        return FAILED(hr) ? hr : endHr;
    }

    // COMP_OP_UPDATE: whole = [commit][preedit] (각 0 또는 1자 — FsmResult 계약)
    wchar_t whole[3];
    LONG n = 0;
    if (es->commit) whole[n++] = es->commit;
    if (es->preedit) whole[n++] = es->preedit;
    whole[n] = L'\0';

    if (n == 0) {
        // 백스페이스로 조합이 비었다 — 조합 텍스트를 지우고 끝낸다 (취소와 동일 동작).
        if (!svc->pComposition) return S_OK;
        es->op = COMP_OP_CANCEL;
        return DoInlineWork(es, ec);
    }

    ITfRange *range = NULL;
    BOOL createdNow = (svc->pComposition == NULL);
    HRESULT hr = EnsureComposition(es, ec, &range);
    if (FAILED(hr)) return hr;

    hr = SessionOwnsTarget(es) ? range->lpVtbl->SetText(range, ec, 0, whole, n) : E_PENDING;
    if (FAILED(hr)) {
        // 방금 만든 조합이면 rollback — 실패를 감추면 증상이 엉뚱한 곳에서 나타난다.
        if (createdNow && svc->pComposition) {
            svc->pComposition->lpVtbl->EndComposition(svc->pComposition, ec);
            ForgetComposition(svc);
        }
        range->lpVtbl->Release(range);
        return hr;
    }
    if (es->op == COMP_OP_SPACE) {
        range->lpVtbl->Release(range);
        if (!svc->pComposition) return S_OK; // Host committed the boundary during SetText.
        svc->compBoundaryWritten = TRUE;
        es->op = COMP_OP_FINALIZE;
        return DoInlineWork(es, ec);
    }
    // 표시 속성(밑줄) 실패는 조합을 무효로 만들지 않는다 — 밑줄이 없을 뿐이다.
    DA_ApplyToRange(es->ctx, ec, range, svc->daAtom);
    BOOL interim = es->preedit && HasTransitoryExtensionParent(es->ctx);
    hr = SelectRangeEnd(es, ec, range, interim);
    range->lpVtbl->Release(range);
    if (FAILED(hr)) return hr;

    if (es->commit) {
        hr = CommitPrefix(es, ec, 1);
        if (SUCCEEDED(hr) && !es->preedit && svc->pComposition) {
            HRESULT endHr = svc->pComposition->lpVtbl->EndComposition(svc->pComposition, ec);
            (void)endHr;   // 확정은 이미 성립 — End 실패로 되돌리지 않는다
            ForgetComposition(svc);
        }
    }
    CaptureCaret(svc, es->ctx, ec);
    return hr;
}

// ── ITfEditSession ──────────────────────────────────────────────────────────────
static HRESULT STDMETHODCALLTYPE IES_QueryInterface(ITfEditSession *pThis, REFIID riid, void **ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfEditSession)) {
        *ppv = pThis;
        pThis->lpVtbl->AddRef(pThis);
        return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE IES_AddRef(ITfEditSession *pThis) {
    return InterlockedIncrement(&((InlineEditSession*)pThis)->refCount);
}
static ULONG STDMETHODCALLTYPE IES_Release(ITfEditSession *pThis) {
    InlineEditSession *es = (InlineEditSession*)pThis;
    ULONG res = InterlockedDecrement(&es->refCount);
    if (res == 0) {
        es->svc->lpVtblTIP->Release((ITfTextInputProcessor*)es->svc);
        es->ctx->lpVtbl->Release(es->ctx);
        if (es->expectedComposition) es->expectedComposition->lpVtbl->Release(es->expectedComposition);
        HeapFree(GetProcessHeap(), 0, es);
    }
    return res;
}
static HRESULT IES_DoEditSession_Inner(ITfEditSession *pThis, TfEditCookie ec) {
    return DoInlineWork((InlineEditSession*)pThis, ec);
}
static HRESULT STDMETHODCALLTYPE IES_DoEditSession(ITfEditSession *pThis, TfEditCookie ec) {
    InlineEditSession *es = (InlineEditSession*)pThis;
    BOOL finalizing = es->op == COMP_OP_FINALIZE;
    BOOL matched = es->expectedComposition && es->expectedComposition == es->svc->pComposition;
    g_ourEditDepth++;                      // 우리 인라인 조합 편집 (light dismiss 판정용)
    HRESULT hr = IES_DoEditSession_Inner(pThis, ec);
    g_ourEditDepth--;
    if (finalizing && matched &&
        (!es->svc->pComposition || es->svc->pComposition == es->expectedComposition)) {
        es->svc->compFinalizePending = FALSE;
        if (hr == S_OK && !es->svc->pComposition) Jamotong_ClearCompositionState(es->svc);
    }
    return hr;
}
static ITfEditSessionVtbl g_InlineSessionVtbl = {
    IES_QueryInterface, IES_AddRef, IES_Release, IES_DoEditSession
};

// 동기 요청 + 세션 내부 hr 전파 (RFC-0004 P2-2 — 요청 hr만 반환하면 내부 실패가 숨는다).
static HRESULT RequestInline(JamotongTextService *svc, ITfContext *ctx, CompOp op,
                             wchar_t commit, wchar_t preedit) {
    InlineEditSession *es = (InlineEditSession*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*es));
    if (!es) return E_OUTOFMEMORY;
    es->lpVtbl = &g_InlineSessionVtbl;
    es->refCount = 1;
    es->svc = svc; es->ctx = ctx;
    es->op = op; es->commit = commit; es->preedit = preedit;
    es->focusHwnd = op == COMP_OP_UPDATE ? GetFocus() : svc->pCompFocusHwnd;
    if (op != COMP_OP_UPDATE && svc->pComposition) {
        es->expectedComposition = svc->pComposition;
        es->expectedComposition->lpVtbl->AddRef(es->expectedComposition);
    }
    svc->lpVtblTIP->AddRef((ITfTextInputProcessor*)svc);
    ctx->lpVtbl->AddRef(ctx);

    HRESULT hrSession = S_OK;
    HRESULT hr = ctx->lpVtbl->RequestEditSession(ctx, svc->clientId, (ITfEditSession*)es,
                                                 TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    // 키 이벤트 밖(compartment 통지로 온 자판 전환 등)에서는 동기 세션이 TF_E_SYNCHRONOUS 로 거부된다.
    // MS SampleIME 의 _TerminateComposition 처럼 비동기(ASYNCDONTCARE)로 다시 건다 — 세션 객체는 힙+참조계수라 안전.
    if (op == COMP_OP_FINALIZE &&
        (hr == TF_E_SYNCHRONOUS || (SUCCEEDED(hr) && hrSession == TF_E_SYNCHRONOUS))) {
        hrSession = S_OK;
        hr = ctx->lpVtbl->RequestEditSession(ctx, svc->clientId, (ITfEditSession*)es,
                                             TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
    }
    es->lpVtbl->Release((ITfEditSession*)es);
    return FAILED(hr) ? hr : hrSession;
}

// ── ITfCompositionSink: 호스트/CUAS가 조합을 끝냈을 때 ─────────────────────────────
static HRESULT STDMETHODCALLTYPE CS_QueryInterface(ITfCompositionSink *pThis, REFIID riid, void **ppv) {
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    return obj->lpVtblTIP->QueryInterface((ITfTextInputProcessor*)obj, riid, ppv);
}
static ULONG STDMETHODCALLTYPE CS_AddRef(ITfCompositionSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    return obj->lpVtblTIP->AddRef((ITfTextInputProcessor*)obj);
}
static ULONG STDMETHODCALLTYPE CS_Release(ITfCompositionSink *pThis) {
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    return obj->lpVtblTIP->Release((ITfTextInputProcessor*)obj);
}
static HRESULT STDMETHODCALLTYPE CS_OnCompositionTerminated(ITfCompositionSink *pThis,
                                                            TfEditCookie ec,
                                                            ITfComposition *pComposition) {
    (void)ec;
    JamotongTextService *obj = IMPL_TO_OBJ(CompSink, pThis);
    // 우리 종료와 겹쳐도 pointer identity로 안전하게 판별한다 (lab §7.4).
    if (pComposition && pComposition == obj->pComposition) {
        // 외부 종료: 조합 텍스트는 호스트가 확정한 그대로 문서에 남는다.
        // 갱신에서 한 번도 생존하지 못한 조합의 키별 종료가 반복되면 이 컨텍스트를 강등한다
        // (비단명 플래그가 보증이 아닌 호스트 대비 — 매뉴얼 §12.7.8).
        BOOL survived = obj->compUpdatedOnce;
        JamoDiag("COMP terminated externally (survived=%d demerits=%d)",
                 (int)survived, obj->pathDemerits);
        Fsm_Init(&obj->fsm);   // 다음 키는 새 조합으로 시작 (이어 붙이면 자모가 겹친다)
        if (survived) {
            obj->pathDemerits = 0;
        } else if (obj->pathDemerits < JAMO_PATH_DEMOTE_LIMIT) {
            obj->pathDemerits++;
            if (obj->pathDemerits >= JAMO_PATH_DEMOTE_LIMIT) obj->pathKind = JAMO_PATH_COMMIT;
        }
        // No state writes after releasing host references: Release may enter
        // another document and publish a new composition/FSM synchronously.
        ForgetComposition(obj);
    }
    return S_OK;
}
static const ITfCompositionSinkVtbl g_CompSinkVtbl = {
    CS_QueryInterface, CS_AddRef, CS_Release, CS_OnCompositionTerminated
};

// ── 공개 API ────────────────────────────────────────────────────────────────────
void JamoComp_Init(JamotongTextService *svc) {
    svc->lpVtblCompSink = &g_CompSinkVtbl;
    svc->pComposition = NULL;
    svc->pCompContext = NULL;
    svc->pCompFocusHwnd = NULL;
    svc->pPathContext = NULL;
    svc->pathKind = JAMO_PATH_COMMIT;
    svc->pathDemerits = 0;
    svc->compUpdatedOnce = FALSE;
    svc->compFinalizePending = FALSE;
    svc->compBoundaryWritten = FALSE;
}

JamoPathKind JamoComp_PathForContext(JamotongTextService *svc, ITfContext *pic) {
    if (!pic) return JAMO_PATH_COMMIT;
    if (!svc->config.options.inlineComposition) return JAMO_PATH_COMMIT;   // 킬스위치
    if (pic == svc->pPathContext) return (JamoPathKind)svc->pathKind;

    // 새 컨텍스트: 판정 1회 + 캐시. GetStatus는 edit cookie가 필요 없는 동기 호출이다.
    TF_STATUS status;
    ZeroMemory(&status, sizeof status);
    HRESULT statusHr = pic->lpVtbl->GetStatus(pic, &status);

    void *probe = NULL;
    int hasInsert = SUCCEEDED(pic->lpVtbl->QueryInterface(pic, &IID_ITfInsertAtSelection, &probe));
    if (probe) { ((IUnknown*)probe)->lpVtbl->Release((IUnknown*)probe); probe = NULL; }
    int hasCtxComp = SUCCEEDED(pic->lpVtbl->QueryInterface(pic, &IID_ITfContextComposition, &probe));
    if (probe) { ((IUnknown*)probe)->lpVtbl->Release((IUnknown*)probe); probe = NULL; }

    svc->pathDemerits = 0;   // 컨텍스트가 바뀌면 강등 이력도 새로 센다
    JamoPathKind kind = JamoPath_Decide((long)statusHr, (unsigned long)status.dwStaticFlags,
                                        hasInsert, hasCtxComp, svc->pathDemerits);
    JamoDiag("COMP path=%s statusHr=0x%08lX static=0x%lX ins=%d cc=%d",
             kind == JAMO_PATH_STANDARD ? "STANDARD" : "COMMIT",
             (unsigned long)statusHr, (unsigned long)status.dwStaticFlags, hasInsert, hasCtxComp);
    svc->pPathContext = pic;   // weak — 포인터 비교 전용, 포커스 이동 시 무효화
    svc->pathKind = (int)kind;
    return kind;
}

HRESULT JamoComp_Apply(JamotongTextService *svc, ITfContext *pic, FsmResult res) {
    if (svc->compFinalizePending || svc->compBoundaryWritten ||
        (svc->pComposition && !JamoComp_OwnsFocus(svc, pic))) return E_PENDING;
    HRESULT hr = RequestInline(svc, pic, COMP_OP_UPDATE, res.commitChar, res.preeditChar);
    if (hr == E_PENDING) return hr; // Focus ownership is not a failed composition capability.
    if (FAILED(hr)) {
        JamoDiag("COMP apply failed hr=0x%08lX -> cancel+demote", (unsigned long)hr);
        JamoComp_Cancel(svc);   // 문서에 남았을 수 있는 조합 텍스트 제거 시도 (없으면 no-op)
        if (svc->pathDemerits < JAMO_PATH_DEMOTE_LIMIT) svc->pathDemerits = JAMO_PATH_DEMOTE_LIMIT;
        svc->pathKind = JAMO_PATH_COMMIT;   // 이 컨텍스트는 즉시 강등 — 같은 실패를 반복하지 않는다
    }
    return hr;
}

BOOL JamoComp_IsActive(const JamotongTextService *svc) {
    return svc->pComposition != NULL;
}

BOOL JamoComp_OwnsFocus(const JamotongTextService *svc, ITfContext *ctx) {
    return ctx && svc->pCompContext == ctx && svc->pCompFocusHwnd &&
           svc->pCompFocusHwnd == GetFocus();
}

HRESULT JamoComp_Finalize(JamotongTextService *svc) {
    if (!svc->pComposition || !svc->pCompContext) return S_OK;
    if (svc->compFinalizePending) return TF_S_ASYNC;
    ITfComposition *composition = svc->pComposition;
    composition->lpVtbl->AddRef(composition);
    if (svc->pComposition != composition || !svc->pCompContext || svc->compFinalizePending) {
        composition->lpVtbl->Release(composition);
        return E_PENDING;
    }
    ITfContext *ctx = svc->pCompContext;
    ctx->lpVtbl->AddRef(ctx);   // 세션 도중 ForgetComposition이 pCompContext를 놓아도 안전
    if (svc->pComposition != composition || svc->pCompContext != ctx || svc->compFinalizePending) {
        ctx->lpVtbl->Release(ctx);
        composition->lpVtbl->Release(composition);
        return E_PENDING;
    }
    svc->compBoundaryWritten = TRUE; // Even a failed boundary must finish before another update.
    svc->compFinalizePending = TRUE;
    HRESULT hr = RequestInline(svc, ctx, COMP_OP_FINALIZE, 0, 0);
    if (hr != TF_S_ASYNC && svc->pComposition == composition) svc->compFinalizePending = FALSE;
    ctx->lpVtbl->Release(ctx);
    composition->lpVtbl->Release(composition);
    return hr;
}

HRESULT JamoComp_CommitWithSpace(JamotongTextService *svc, wchar_t syllable) {
    if (!svc->pComposition || !svc->pCompContext) return E_UNEXPECTED;
    if (svc->compFinalizePending || svc->compBoundaryWritten) return E_PENDING;
    ITfContext *ctx = svc->pCompContext;
    ctx->lpVtbl->AddRef(ctx);
    HRESULT hr = RequestInline(svc, ctx, COMP_OP_SPACE, syllable, L' ');
    ctx->lpVtbl->Release(ctx);
    return hr;
}

BOOL JamoComp_PrepareInput(JamotongTextService *svc, ITfContext *ctx) {
    if (svc->compFinalizePending) return FALSE;
    if (svc->pComposition && svc->pCompContext == ctx &&
        svc->pCompFocusHwnd && svc->pCompFocusHwnd != GetFocus()) {
        svc->compBoundaryWritten = TRUE;
        return FALSE; // Retry only on the original target; new target keys belong to the app.
    }
    if (svc->compBoundaryWritten || (svc->pComposition && svc->pCompContext != ctx))
        return JamoComp_Finalize(svc) == S_OK;
    return TRUE;
}

void JamoComp_Cancel(JamotongTextService *svc) {
    if (!svc->pComposition || !svc->pCompContext) return;
    ITfContext *ctx = svc->pCompContext;
    ctx->lpVtbl->AddRef(ctx);
    HRESULT hr = RequestInline(svc, ctx, COMP_OP_CANCEL, 0, 0);
    if (FAILED(hr) && hr != E_PENDING) ForgetComposition(svc);
    ctx->lpVtbl->Release(ctx);
}

void JamoComp_ResetPathCache(JamotongTextService *svc) {
    svc->pPathContext = NULL;
    svc->pathKind = JAMO_PATH_COMMIT;
    svc->pathDemerits = 0;
}

void JamoComp_Release(JamotongTextService *svc) {
    JamoComp_Finalize(svc);   // 남은 조합은 텍스트를 보존한 채 확정
    ForgetComposition(svc);
    JamoComp_ResetPathCache(svc);
}
