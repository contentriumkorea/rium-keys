#include <windows.h>
#include <stdio.h>
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) { printf("LOAD_FAILED error=%lu\n", GetLastError()); return 3; }
    bool queue = GetProcAddress(module, "ProbeQueue") != nullptr;
    bool dispatch = GetProcAddress(module, "ProbeDispatch") != nullptr;
    bool unloaded = FreeLibrary(module) != FALSE;
    printf("LOAD_CHECK queueExport=%u dispatchExport=%u freeLibrary=%u noWindowsCreated=1 noHooksInstalled=1\n", queue, dispatch, unloaded);
    return queue && dispatch && unloaded ? 0 : 4;
}
