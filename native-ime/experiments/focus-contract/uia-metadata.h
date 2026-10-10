#pragma once
#include <windows.h>

// Caller owns the COM apartment and process watchdog. These functions only read
// pattern availability, focus/identity metadata and read-only attributes.
void ReadUiaSnapshot(DWORD targetPid, HWND mainWindow, HWND focusedWindow, bool targetForeground);
int UiaSelfTest();
