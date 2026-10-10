"""Read one verified CCL text-input counter; never hook, call or modify the host.

Research only. This records a framework-wide editing count, NOT a universally
valid current-window routing decision. No keyboard events or text are read.
The binary digest and getter/vtable bytes must match before reading the count.
"""

import argparse
import ctypes
from ctypes import wintypes as W
import datetime
import hashlib
import json
from pathlib import Path
import struct
import time


EXPECTED_SHA256 = "a7adf05dc29c9fd045c4803922d1c25825882bb17024790976fd3d77d0392a4b"
GET_DESKTOP_RVA = 0x33B890
DESKTOP_RVA = 0x52EC30
DESKTOP_VTABLE_RVA = 0x42B028
IS_IN_MODE_SLOT = 14  # Verified installed 4.0.3 binary; NOT the current header ABI.
IS_IN_MODE_RVA = 0x33C590
EDIT_COUNT_RVA = 0x55B414


def file_bytes_at_rva(data, rva, count):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Not a PE image")
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    start = pe + 24 + optional_size
    for index in range(sections):
        _, va, raw_size, raw_offset = struct.unpack_from("<IIII", data, start + index * 40 + 8)
        if va <= rva and rva + count <= va + raw_size:
            offset = raw_offset + rva - va
            return data[offset:offset + count]
    raise ValueError("RVA is outside initialized image sections")


class ReadOnlyProcess:
    def __init__(self, pid):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.psapi = ctypes.WinDLL("psapi", use_last_error=True)
        self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.kernel.OpenProcess.restype = W.HANDLE
        self.kernel.CloseHandle.argtypes = [W.HANDLE]
        self.kernel.ReadProcessMemory.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                                 ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self.kernel.ReadProcessMemory.restype = W.BOOL
        self.psapi.EnumProcessModulesEx.argtypes = [W.HANDLE, ctypes.POINTER(W.HMODULE),
                                                   W.DWORD, ctypes.POINTER(W.DWORD), W.DWORD]
        self.psapi.EnumProcessModulesEx.restype = W.BOOL
        self.psapi.GetModuleFileNameExW.argtypes = [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD]
        self.psapi.GetModuleFileNameExW.restype = W.DWORD
        self.handle = self.kernel.OpenProcess(0x0400 | 0x0010, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, size):
        if not 0 < size <= 4096:
            raise ValueError("Read size outside diagnostic limit")
        buffer = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, buffer, size, ctypes.byref(got)):
            raise ctypes.WinError(ctypes.get_last_error())
        if got.value != size:
            raise RuntimeError("Partial process-memory read")
        return buffer.raw

    def ccl_module(self):
        modules = (W.HMODULE * 2048)()
        needed = W.DWORD()
        if not self.psapi.EnumProcessModulesEx(self.handle, modules, ctypes.sizeof(modules),
                                              ctypes.byref(needed), 3):
            raise ctypes.WinError(ctypes.get_last_error())
        if needed.value > ctypes.sizeof(modules):
            raise RuntimeError("Module list exceeded diagnostic limit")
        matches = []
        for module in modules[:needed.value // ctypes.sizeof(W.HMODULE)]:
            name = ctypes.create_unicode_buffer(32768)
            if not self.psapi.GetModuleFileNameExW(self.handle, module, name, len(name)):
                raise ctypes.WinError(ctypes.get_last_error())
            if Path(name.value).name.lower() == "cclgui.dll":
                matches.append((module, Path(name.value)))
        if len(matches) != 1:
            raise RuntimeError("Expected exactly one loaded cclgui.dll")
        return matches[0]


def sample(pid, label):
    started = time.perf_counter()
    process = ReadOnlyProcess(pid)
    try:
        base, path = process.ccl_module()
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != EXPECTED_SHA256:
            raise RuntimeError("Unverified CCL binary; no state read")
        for rva, size in ((GET_DESKTOP_RVA, 8), (IS_IN_MODE_RVA, 0x95)):
            if process.read(base + rva, size) != file_bytes_at_rva(data, rva, size):
                raise RuntimeError("Loaded getter differs from verified binary; no state read")
        vtable = struct.unpack("<Q", process.read(base + DESKTOP_RVA, 8))[0]
        if vtable != base + DESKTOP_VTABLE_RVA:
            raise RuntimeError("Desktop vtable differs; no state read")
        method = struct.unpack("<Q", process.read(vtable + IS_IN_MODE_SLOT * 8, 8))[0]
        if method != base + IS_IN_MODE_RVA:
            raise RuntimeError("Mode getter differs; no state read")
        count = struct.unpack("<i", process.read(base + EDIT_COUNT_RVA, 4))[0]
        if not 0 <= count <= 64:
            raise RuntimeError("Counter outside diagnostic sanity bounds")
        return {"at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "pid": pid, "label": label, "verified_sha256": digest,
                "edit_count": count, "framework_has_text_input": count > 0,
                "scope": "CCL-wide counter; current-window ownership unverified",
                "probe_ms_including_module_and_hash_checks": round((time.perf_counter() - started) * 1000, 3)}
    finally:
        process.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--label", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = sample(args.pid, args.label)
    except (OSError, ValueError, RuntimeError) as error:
        result = {"pid": args.pid, "label": args.label, "state": "unknown", "error": str(error)}
    line = json.dumps(result, ensure_ascii=False)
    print(line)
    if args.output:
        with args.output.open("a", encoding="utf-8") as output:
            output.write(line + "\n")
    raise SystemExit(1 if result.get("state") == "unknown" else 0)
