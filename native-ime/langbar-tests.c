// Exercise the production langbar vtable and the candidate DLL's icon resources.
// Configuration/publish stubs avoid changing the user's profile or settings.
#include "third_party/jamotong/src/jamotong.h"
#include "third_party/jamotong/src/langbar.h"
#include <stdio.h>
#include <stdlib.h>

HINSTANCE g_hInst;
CRITICAL_SECTION g_configLock;
const CLSID CLSID_JamotongIME = {0};
LayoutConfig* Config_GetCurrentLayout(JamotongConfig *c) {
    return c->currentLayoutIndex < c->layoutCount ? &c->layouts[c->currentLayoutIndex] : NULL;
}
void Config_RotateLayout(JamotongConfig *c) { c->currentLayoutIndex = 1 - c->currentLayoutIndex; }
void Jamotong_FlushForExternalSwitch(JamotongTextService *s) { (void)s; }
void Jamotong_SetPassthrough(JamotongTextService *s, BOOL on) { s->passthrough = on; }
void Compart_Publish(JamotongTextService *s) { (void)s; }
static unsigned checks;
static void check(BOOL ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
    ++checks;
}
static unsigned long long pixels(HICON icon, int expectedSize) {
    ICONINFO info = {0}; BITMAP bitmap = {0};
    check(icon && GetIconInfo(icon, &info), "private icon loads");
    check(GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) != 0 &&
          bitmap.bmWidth == expectedSize && bitmap.bmHeight == expectedSize, "icon size matches DPI request");
    unsigned count = (unsigned)(bitmap.bmWidth * bitmap.bmHeight);
    DWORD *data = calloc(count, sizeof(DWORD));
    BITMAPINFO bi = {0}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bitmap.bmWidth; bi.bmiHeader.biHeight = -bitmap.bmHeight;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(NULL);
    check(data && GetDIBits(dc, info.hbmColor, 0, (UINT)bitmap.bmHeight, data, &bi, DIB_RGB_COLORS), "icon pixels readable");
    unsigned visible = 0, transparent = 0; BOOL white=TRUE; unsigned long long hash = 1469598103934665603ULL;
    for (unsigned i = 0; i < count; ++i) {
        unsigned a = data[i] >> 24, r = (data[i] >> 16) & 255, g = (data[i] >> 8) & 255, b = data[i] & 255;
        if (a == 0) ++transparent;
        // Accept straight or premultiplied white from GetDIBits. Small AA
        // strokes need not contain a fully opaque pixel.
        if (a >= 128) { ++visible; if(abs((int)r-(int)g)>2||abs((int)r-(int)b)>2||r+2<a)white=FALSE; }
        hash = (hash ^ data[i]) * 1099511628211ULL;
    }
    check(white,"mode glyph is white");
    check(visible > 0 && transparent > count / 4, "visible glyph with transparent background");
    free(data); DeleteDC(dc); DeleteObject(info.hbmColor); DeleteObject(info.hbmMask);
    DestroyIcon(icon); return hash;
}
static unsigned notices; static DWORD noticeFlags;
static HRESULT STDMETHODCALLTYPE sinkQI(ITfLangBarItemSink *s, REFIID id, void **p) { (void)s;(void)id;*p=NULL;return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE sinkRef(ITfLangBarItemSink *s) { (void)s;return 1; }
static HRESULT STDMETHODCALLTYPE sinkUpdate(ITfLangBarItemSink *s, DWORD flags) { (void)s;++notices;noticeFlags=flags;return S_OK; }
static ITfLangBarItemSinkVtbl sinkVtable = {sinkQI,sinkRef,sinkRef,sinkUpdate};
int wmain(int argc, wchar_t **argv) {
    check(argc == 2, "candidate DLL argument");
    g_hInst = LoadLibraryExW(argv[1], NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    check(g_hInst != NULL, "actual candidate resources open");
    InitializeCriticalSection(&g_configLock);
    int sizes[] = {16,20,24,32,40,48,64,128,256};
    for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        unsigned long long ko = pixels((HICON)LoadImageW(g_hInst,MAKEINTRESOURCEW(101),IMAGE_ICON,sizes[i],sizes[i],0),sizes[i]);
        unsigned long long en = pixels((HICON)LoadImageW(g_hInst,MAKEINTRESOURCEW(102),IMAGE_ICON,sizes[i],sizes[i],0),sizes[i]);
        check(ko != en, "Korean and English glyphs differ at every DPI");
        HICON brand=(HICON)LoadImageW(g_hInst,MAKEINTRESOURCEW(100),IMAGE_ICON,sizes[i],sizes[i],0);
        check(brand != NULL,"separate brand resource remains available");DestroyIcon(brand);
    }
    LayoutConfig layouts[2] = {{.type=LAYOUT_TYPE_KOREAN_FSM,.name=L"Korean"}, {.type=LAYOUT_TYPE_PASSTHROUGH,.name=L"English"}};
    JamotongTextService service = {0};service.config.layouts=layouts;service.config.layoutCount=2;
    JamotongLangBarItem *item=LangBar_Create(&service);check(item != NULL,"production langbar creation");
    ITfLangBarItemButton *button=(ITfLangBarItemButton*)&item->lpVtblButton;
    ITfLangBarItemSink sink={&sinkVtable};item->pSink=&sink;
    HICON icon=NULL;check(button->lpVtbl->GetIcon(button,&icon)==S_OK,"Korean GetIcon");
    int size=GetSystemMetrics(SM_CXSMICON);unsigned long long ko=pixels(icon,size);
    check(button->lpVtbl->OnClick(button,TF_LBI_CLK_LEFT,(POINT){0},NULL)==S_OK,"mode switch");
    check(notices==1&&(noticeFlags&1)&&(noticeFlags&4),"switch notifies icon and tooltip immediately");
    check(button->lpVtbl->GetIcon(button,&icon)==S_OK,"English GetIcon");unsigned long long en=pixels(icon,size);
    check(ko!=en,"real langbar switches glyph");
    button->lpVtbl->OnClick(button,TF_LBI_CLK_LEFT,(POINT){0},NULL);
    button->lpVtbl->GetIcon(button,&icon);check(pixels(icon,size)==ko,"return to Korean glyph");
    service.passthrough=TRUE;button->lpVtbl->GetIcon(button,&icon);check(pixels(icon,size)==en,"disabled input displays direct A mode");
    service.passthrough=FALSE;layouts[0].type=LAYOUT_TYPE_STATIC_MAP;
    button->lpVtbl->GetIcon(button,&icon);check(pixels(icon,size)==en,"preserved Dvorak layout remains English");
    BSTR tooltip=NULL;button->lpVtbl->GetTooltipString(button,&tooltip);
    check(tooltip&&wcsstr(tooltip,L"English"),"Dvorak tooltip agrees with actual mode");SysFreeString(tooltip);
    layouts[0].type=LAYOUT_TYPE_HANGUL_CUSTOM;button->lpVtbl->GetIcon(button,&icon);
    check(pixels(icon,size)==ko,"custom Hangul layout displays Korean");
    item->pService=NULL;icon=(HICON)1;check(button->lpVtbl->GetIcon(button,&icon)==S_OK&&icon==NULL,"detached service cannot expose stale mode");
    button->lpVtbl->Release(button);DeleteCriticalSection(&g_configLock);FreeLibrary(g_hInst);
    printf("Langbar resource and mode checks: %u passed. No input profile changed.\n",checks);return 0;
}
