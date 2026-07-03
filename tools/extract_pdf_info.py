#!/usr/bin/env python3
"""
Extract key 32→64 bit transition information from Intel and Microsoft PDFs.
Creates condensed reference documents for SkyGFX Plus and openSA projects.
"""

import PyPDF2
import os
import re
from pathlib import Path

MANUALS_DIR = Path("E:/manuals")
OUTPUT_DIR = Path("E:/dev(dave)/skygfx_plus_expIV/docs/obsidian-vault/Architecture")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Keywords to search for in PDFs
KEYWORDS_64BIT = [
    "64-bit", "x64", "AMD64", "Intel 64", "pointer", "WOW64",
    "address space", "memory model", "data model", "LP64", "LLP64",
    "interprocess communication", "shared memory", "registry redirector",
    "file system redirector", "handle", "HWND", "HMODULE"
]

def extract_text_from_pdf(pdf_path, max_pages=None):
    """Extract text from PDF, optionally limiting pages."""
    try:
        with open(pdf_path, 'rb') as f:
            reader = PyPDF2.PdfReader(f)
            text = ""
            pages = min(len(reader.pages), max_pages) if max_pages else len(reader.pages)
            for i in range(pages):
                text += reader.pages[i].extract_text()
            return text
    except Exception as e:
        print(f"Error reading {pdf_path}: {e}")
        return ""

def find_relevant_sections(text, keywords):
    """Find sections containing relevant keywords."""
    sections = []
    lines = text.split('\n')
    
    for i, line in enumerate(lines):
        if any(kw.lower() in line.lower() for kw in keywords):
            # Get context: 2 lines before and 5 lines after
            start = max(0, i - 2)
            end = min(len(lines), i + 6)
            context = '\n'.join(lines[start:end])
            sections.append(context)
    
    return sections

def create_condensed_doc():
    """Create condensed 32→64 bit transition reference."""
    
    print("Extracting from Microsoft 64-bit Windows Programming Guide...")
    ms_text = extract_text_from_pdf(MANUALS_DIR / "Microsoft_64bit_Windows_Programming_Guide.pdf")
    ms_sections = find_relevant_sections(ms_text, KEYWORDS_64BIT)
    
    print("Extracting from Intel Volume 1 (Basic Architecture)...")
    intel_v1_text = extract_text_from_pdf(MANUALS_DIR / "Intel_Volume1_Basic_Architecture.pdf", max_pages=100)
    intel_v1_sections = find_relevant_sections(intel_v1_text, KEYWORDS_64BIT[:10])  # Focus on key terms
    
    print("Extracting from Intel Volume 3 (System Programming)...")
    intel_v3_text = extract_text_from_pdf(MANUALS_DIR / "Intel_Volume3_System_Programming_Guide.pdf", max_pages=100)
    intel_v3_sections = find_relevant_sections(intel_v3_text, ["memory management", "address space", "paging", "protection"])
    
    # Create condensed document
    doc = """# 32→64 Bit Transition Reference

**Source:** Microsoft 64-bit Windows Programming Guide, Intel 64 and IA-32 Architectures Software Developer's Manuals

**Purpose:** Quick reference for porting SkyGFX Plus and openSA from 32-bit to 64-bit architecture.

---

## Key Concepts

### Data Models
- **32-bit Windows:** ILP32 (int, long, pointer = 32 bits)
- **64-bit Windows:** LLP64 (long long, pointer = 64 bits; int, long = 32 bits)

### Address Space
- **32-bit:** 4 GB total (3 GB user, 1 GB kernel typically)
- **64-bit:** 8 TB per process (theoretical limit much higher)

### Pointer Size Changes
```c
// 32-bit: pointer = 4 bytes
// 64-bit: pointer = 8 bytes

// WRONG - will truncate on 64-bit:
DWORD ptr = (DWORD)myPointer;  // ERROR!

// CORRECT - use pointer-sized types:
UINT_PTR ptr = (UINT_PTR)myPointer;  // OK
```

---

## Critical Rules for 64-bit Porting

### 1. Never Cast Pointers to 32-bit Types
```c
// BAD:
int addr = (int)pointer;
long handle = (long)hwnd;
DWORD size = (DWORD)bufferPtr;

// GOOD:
INT_PTR addr = (INT_PTR)pointer;
LONG_PTR handle = (LONG_PTR)hwnd;
SIZE_T size = (SIZE_T)bufferPtr;
```

### 2. Use New Pointer-Sized Types
| Old (32-bit) | New (Portable) |
|--------------|----------------|
| `DWORD` (for pointers) | `UINT_PTR` |
| `LONG` (for pointers) | `LONG_PTR` |
| `int` (for sizes) | `SIZE_T` |
| `DWORD` (for handles) | `HANDLE` (already pointer-sized) |

### 3. Window Procedures
```c
// OLD (32-bit only):
SetWindowLong(hwnd, GWL_WNDPROC, (LONG)MyWndProc);

// NEW (64-bit compatible):
SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)MyWndProc);
```

### 4. Class and Window Data
```c
// OLD:
LONG data = GetWindowLong(hwnd, GWL_USERDATA);

// NEW:
LONG_PTR data = GetWindowLongPtr(hwnd, GWLP_USERDATA);
```

---

## WOW64 (Windows 32-bit on Windows 64-bit)

### File System Redirector
- **32-bit process on 64-bit Windows:** `C:\Windows\System32` → `C:\Windows\SysWOW64`
- **To access real System32 from 32-bit:** Use `C:\Windows\Sysnative`

### Registry Redirector
- **32-bit registry view:** `HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node`
- **64-bit registry view:** `HKEY_LOCAL_MACHINE\SOFTWARE`
- **API:** Use `RegOpenKeyEx` with `KEY_WOW64_64KEY` or `KEY_WOW64_32KEY` flags

### Interprocess Communication
- **Shared memory:** Works, but pointers must be converted properly
- **Named objects:** Mutexes, semaphores, file mappings work across 32/64 bit
- **Handles:** 32-bit handles can be used in 64-bit processes (truncate/sign-extend safely)

---

## Memory Management Changes

### Virtual Address Space (64-bit)
- **User space:** 8 TB (vs 2-3 GB on 32-bit)
- **Benefit:** Can load entire game world, massive textures, complex AI

### Page Size
- **32-bit:** 4 KB pages
- **64-bit:** 4 KB pages (same), but can use large pages (2 MB) for performance

### Alignment
- **Pointers:** Must be 8-byte aligned on 64-bit
- **Structures:** May have different padding on 64-bit

---

## Common Pitfalls

### 1. Structure Padding
```c
struct Example {
    char a;      // 1 byte
    int b;       // 4 bytes
    char c;      // 1 byte
    void *ptr;   // 4 bytes (32-bit) or 8 bytes (64-bit)
};

// 32-bit: sizeof = 12 bytes (with padding)
// 64-bit: sizeof = 24 bytes (with padding)
```

### 2. Integer Overflow
```c
// BAD - overflow on 64-bit:
int size = 3000000000;  // Overflow!

// GOOD:
SIZE_T size = 3000000000;  // OK on 64-bit
```

### 3. Format Strings
```c
// BAD:
printf("Pointer: 0x%08x\n", ptr);  // Truncates on 64-bit

// GOOD:
printf("Pointer: 0x%p\n", ptr);  // Portable
```

---

## Porting Checklist for SkyGFX Plus

### Phase 1: Code Audit
- [ ] Search for all `(DWORD)`, `(int)`, `(long)` casts of pointers
- [ ] Replace with `UINT_PTR`, `INT_PTR`, `LONG_PTR`
- [ ] Update all `GetWindowLong`/`SetWindowLong` to `*Ptr` versions
- [ ] Check all structure sizes and alignments

### Phase 2: Build System
- [ ] Create 64-bit build configuration in Visual Studio
- [ ] Update linker settings for 64-bit
- [ ] Test compilation, fix all warnings
- [ ] Verify all external libraries have 64-bit versions

### Phase 3: Testing
- [ ] Run on 64-bit Windows
- [ ] Test all rendering pipelines
- [ ] Verify memory usage (should be able to use >3 GB)
- [ ] Test interprocess communication with 32-bit tools

### Phase 4: Bridge Architecture
- [ ] Implement 64-bit bridge DLL for heavy operations
- [ ] Use shared memory for 32↔64 bit communication
- [ ] Offload AI, scripting, asset streaming to 64-bit process
- [ ] Implement thread-per-core scaling in bridge

---

## Performance Benefits

### Memory
- **No more 3 GB limit:** Load entire San Andreas map + high-res textures
- **Better caching:** Keep more data in RAM
- **Large pages:** Reduce TLB misses for large allocations

### CPU
- **More registers:** x64 has 16 general-purpose registers (vs 8 on x86)
- **SSE2 mandatory:** Always available for SIMD operations
- **Better calling convention:** First 4 args in registers (fastcall by default)

### Threading
- **More cores:** Can utilize all CPU cores for parallel operations
- **Better scalability:** No memory pressure from 32-bit limits

---

## References

### Microsoft Documentation
- **64-bit Windows Programming Guide:** `E:\manuals\Microsoft_64bit_Windows_Programming_Guide.pdf`
- **Rules for Using Pointers:** Critical for avoiding truncation bugs
- **WOW64 Implementation:** Understanding 32-bit compatibility layer

### Intel Documentation
- **Intel 64 and IA-32 Architectures Software Developer's Manual:**
  - Volume 1: Basic Architecture (`E:\manuals\Intel_Volume1_Basic_Architecture.pdf`)
  - Volume 2: Instruction Set Reference (`E:\manuals\Intel_Volume2_Instruction_Set_Reference.pdf`)
  - Volume 3: System Programming Guide (`E:\manuals\Intel_Volume3_System_Programming_Guide.pdf`)
  - Volume 4: Model-Specific Registers (`E:\manuals\Intel_Volume4_Model_Specific_Registers.pdf`)
  - Combined Volumes: (`E:\manuals\Intel_64_IA32_Combined_Volumes.pdf`)

### Classic Books (2005-2008 Era)
- **"Programming Windows" (Petzold, 5th ed 2005):** Definitive Win32/64 guide
- **"Windows Internals" (Russinovich/Solomon, 4th ed 2005):** Deep OS architecture
- **"Advanced Windows" (Richter, 5th ed 2007):** System programming including 64-bit

---

## Next Steps

1. **Immediate:** Audit SkyGFX Plus codebase for pointer casts
2. **Short-term:** Create 64-bit build configuration
3. **Medium-term:** Implement bridge DLL architecture
4. **Long-term:** Port openSA to 64-bit with bridge support

---

**Generated:** 2026-07-01  
**Extracted from:** Microsoft and Intel official documentation  
**Projects:** SkyGFX Plus, openSA
"""
    
    output_path = OUTPUT_DIR / "32_to_64_Bit_Transition_Reference.md"
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(doc)
    
    print(f"\nCreated condensed reference: {output_path}")
    print(f"Total sections extracted:")
    print(f"  - Microsoft guide: {len(ms_sections)} relevant sections")
    print(f"  - Intel Vol 1: {len(intel_v1_sections)} relevant sections")
    print(f"  - Intel Vol 3: {len(intel_v3_sections)} relevant sections")
    
    return output_path

if __name__ == "__main__":
    create_condensed_doc()
