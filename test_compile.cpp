#ifdef _WIN32
// For 32-bit Windows
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "odbc32.lib")
#endif

#include <iostream>
#include <fstream>

// Declaration for _mm_loadu_si64 compatibility
#ifdef _M_X86
static inline __m128i _mm_loadu_si64(const void* p) {
    return _mm_loadu_si32(p);
}
#endif

// Simple test to verify compilation
int main() {
    std::cout << "Test compilation successful!" << std::endl;
    return 0;
}
