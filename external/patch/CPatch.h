// CPatch.h - Memory patching utilities
#pragma once
#include <windows.h>
#include "MemoryMgr.h"

class CPatch {
public:
    static void RedirectJump(DWORD addr, void* func) {
        // Write a jump to the target function
        DWORD oldProtect;
        VirtualProtect((void*)addr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
        *(BYTE*)addr = 0xE9; // JMP rel32
        *(DWORD*)(addr + 1) = (DWORD)func - addr - 5;
        VirtualProtect((void*)addr, 5, oldProtect, &oldProtect);
    }
    
    static void WriteDWORD(DWORD addr, DWORD value) {
        DWORD oldProtect;
        VirtualProtect((void*)addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
        *(DWORD*)addr = value;
        VirtualProtect((void*)addr, 4, oldProtect, &oldProtect);
    }
};
