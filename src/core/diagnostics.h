#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

// diagnostics.h — Crash logging, debug output, exception handling.
// Separate from main.cpp so it can be ripped out for release builds.
//
// To disable all diagnostics: #define DIAGNOSTICS_DISABLED before including this header,
// or add DIAGNOSTICS_DISABLED to the project preprocessor definitions.

#include <windows.h>

#ifdef DIAGNOSTICS_DISABLED

// Stubs — all diagnostic calls become no-ops
#define diag_init(logPath)       ((void)0)
#define diag_shutdown()          ((void)0)
#define diag_getLogPath()        ((const char*)"")
#define diag_installVEH()       ((void)0)
#define diag_removeVEH()        ((void)0)

#else

// Initialize diagnostics: sets up log path, truncates old log.
// logPath should be the full path to the log file.
void diag_init(const char *logPath);

// Install the VEH crash handler (diagnostic-only, never suppresses).
void diag_installVEH(void);

// Remove the VEH crash handler.
void diag_removeVEH(void);

// Get the current log file path (for external code that needs it).
const char* diag_getLogPath(void);

// Write a minidump of the current crash (best-effort). Pass the
// EXCEPTION_POINTERS from a VEH or SEH handler. Safe to call any time.
void diag_writeMinidump(EXCEPTION_POINTERS *exceptionPointers);

#endif // DIAGNOSTICS_DISABLED

#endif // DIAGNOSTICS_H
