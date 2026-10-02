// crashhandler.cpp — self-installing unhandled-exception filter: minidump on every hard crash.
//
// Scope: NEW standalone file. Zero shared-file edits, zero build-system lib edits
// (dbghelp is linked via #pragma comment below; registration is one ClCompile
// line in build/skygfx.vcxproj — the effective source list, since fast_build.py
// runs MSBuild directly and never regenerates the project from premake5.lua's globs).
//
// Install: a namespace-scope static object whose constructor runs at CRT static
// init (DLL load, before user DllMain) calls SetUnhandledExceptionFilter and
// saves the previous filter.
//
// Chaining convention (stated per spec 2c): we do our work FIRST (log line +
// minidump), THEN call the previous filter if non-null and RETURN ITS RESULT —
// its behavior is preferred so nothing it does is silently dropped. If there is
// no previous filter, we return EXCEPTION_EXECUTE_HANDLER.
//
// Why SetUnhandledExceptionFilter and NOT AddVectoredExceptionHandler: the
// codebase has expected/caught first-chance AVs inside SEH guards
// (guardedSetRT in pipelinecommon.cpp, SMAA passes in postfx.cpp). A VEH would
// see those and produce bogus dumps. The unhandled filter only runs when no
// other handler claimed the exception — exactly the "hard crash" set. (The
// separate diagnostic VEH in diagnostics.cpp stays untouched and is
// forensics-only.) terminate/new handlers: N/A per spec — ignored.
//
// Re-entrancy: static LONG claimed via InterlockedCompareExchange as the VERY
// FIRST act of the handler — no allocation, no file I/O before the claim.
// Recursive faults during handling skip the work and fall straight through.
// Each best-effort step is additionally wrapped in its own __try/__except so
// a fault in the log append still lets the dump happen, and nothing ever
// raises out of the filter.

#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "dbghelp.lib")

namespace {

// ------------------------------------------------------------
// Shared state
// ------------------------------------------------------------

// Re-entrancy guard: 0 = free, 1 = handler running (or ran).
volatile LONG s_crashHandling = 0;

// Previous unhandled filter, saved at install time; chained after our work.
LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = NULL;

// ------------------------------------------------------------
// Path helpers (best-effort; return 0 on failure)
// ------------------------------------------------------------

// Directory of the module containing this code (skygfx.asi) WITH trailing
// backslash — matches how main.cpp derives skygfx_dbg.log (dll dir + name).
// Falls back to the exe dir if the module can't be resolved.
static DWORD
selfDir(char *buf, DWORD cap)
{
	HMODULE mod = NULL;
	DWORD n = 0;
	if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)(uintptr_t)&selfDir, &mod) && mod)
		n = GetModuleFileNameA(mod, buf, cap);
	if(n == 0 || n >= cap)
		n = GetModuleFileNameA(NULL, buf, cap);
	if(n == 0 || n >= cap)
		return 0;
	char *slash = strrchr(buf, '\\');
	if(!slash)
		return 0;
	slash[1] = '\0';
	return 1;
}

// Directory of gta_sa.exe WITH trailing backslash (spec: resolve dump dir
// and WER DumpFolder from GetModuleFileNameA(NULL)).
static DWORD
exeDir(char *buf, DWORD cap)
{
	DWORD n = GetModuleFileNameA(NULL, buf, cap);
	if(n == 0 || n >= cap)
		return 0;
	char *slash = strrchr(buf, '\\');
	if(!slash)
		return 0;
	slash[1] = '\0';
	return 1;
}

// ------------------------------------------------------------
// (a) Append one crash line to skygfx_dbg.log
// ------------------------------------------------------------

// Line format (single line, append semantics — the main app truncates this
// log every launch via diag_init, so we open it fresh with "a" behavior):
//   [YYYY-MM-DD HH:MM:SS.mmm] [CRASH] code=0x%08X addr=0x%08X
//   [YYYY-MM-DD HH:MM:SS.mmm] [CRASH] code=0x%08X addr=0x%08X op=N(READ|WRITE|DEP|OTHER) fault=0x%08X
// where addr = ContextRecord->Eip (ExceptionAddress), and for AV the two
// ExceptionInformation entries: [0] 0=read/1=write/8=DEP, [1] = fault address
// (the recon ask: the old dialog crash faulted at 0x39D1B71B — now logged).
static void
crashAppendLog(EXCEPTION_POINTERS *ep)
{
	char path[MAX_PATH];
	if(!selfDir(path, sizeof(path)))
		return;
	strncat(path, "skygfx_dbg.log", sizeof(path) - strlen(path) - 1);

	SYSTEMTIME st;
	GetLocalTime(&st);

	DWORD code = 0;
	DWORD addr = 0;
	if(ep && ep->ExceptionRecord)
		code = ep->ExceptionRecord->ExceptionCode;
	if(ep && ep->ContextRecord)
		addr = ep->ContextRecord->Eip;   // x86 ExceptionAddress
	else if(ep && ep->ExceptionRecord)
		addr = (DWORD)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;

	char line[320];
	int n;
	if(code == EXCEPTION_ACCESS_VIOLATION && ep && ep->ExceptionRecord &&
	   ep->ExceptionRecord->NumberParameters >= 2)
	{
		DWORD op = (DWORD)ep->ExceptionRecord->ExceptionInformation[0];
		DWORD fault = (DWORD)ep->ExceptionRecord->ExceptionInformation[1];
		const char *opStr =
			op == 0 ? "READ" :
			op == 1 ? "WRITE" :
			op == 8 ? "DEP" : "OTHER";
		n = snprintf(line, sizeof(line),
			"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [CRASH] code=0x%08X addr=0x%08X op=%u(%s) fault=0x%08X\n",
			(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			(unsigned)st.wMilliseconds,
			code, addr, op, opStr, fault);
	}else{
		n = snprintf(line, sizeof(line),
			"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [CRASH] code=0x%08X addr=0x%08X\n",
			(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			(unsigned)st.wMilliseconds,
			code, addr);
	}
	if(n <= 0)
		return;
	if(n >= (int)sizeof(line))
		n = (int)sizeof(line) - 1;   // never read past the buffer

	// "a" semantics: append regardless of current EOF; independent handle.
	// The app's own writer opens/closes per message, so no long-lived lock.
	HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
		return;
	DWORD written = 0;
	WriteFile(h, line, (DWORD)n, &written, NULL);
	CloseHandle(h);
}

// ------------------------------------------------------------
// (b) Minidump: <exe dir>\skygfx_crash_YYYY-MM-DD_HHMMSS.dmp
// ------------------------------------------------------------

static void
crashWriteDump(EXCEPTION_POINTERS *ep)
{
	char dir[MAX_PATH];
	if(!exeDir(dir, sizeof(dir)))
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);
	char path[MAX_PATH];
	snprintf(path, sizeof(path),
		"%sskygfx_crash_%04u-%02u-%02u_%02u%02u%02u.dmp",
		dir,
		(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
		(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);

	HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE)
		return;   // best-effort: fall through, never raise

	MINIDUMP_EXCEPTION_INFORMATION mei;
	MINIDUMP_EXCEPTION_INFORMATION *pMei = NULL;
	if(ep){
		mei.ThreadId = GetCurrentThreadId();
		mei.ExceptionPointers = ep;
		mei.ClientPointers = FALSE;
		pMei = &mei;
	}

	MINIDUMP_TYPE type = (MINIDUMP_TYPE)(
		MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);

	BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
		hFile, type, pMei, NULL, NULL);
	DWORD size = GetFileSize(hFile, NULL);

	if(!ok || size == 0 || size == INVALID_FILE_SIZE){
		// Retry once with MiniDumpNormal.
		CloseHandle(hFile);
		hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile == INVALID_HANDLE_VALUE)
			return;
		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
			hFile, MiniDumpNormal, pMei, NULL, NULL);
	}
	CloseHandle(hFile);
}

// ------------------------------------------------------------
// The unhandled filter
// ------------------------------------------------------------

static LONG WINAPI
crashUnhandledFilter(EXCEPTION_POINTERS *ep)
{
	// 1. Claim the re-entrancy flag BEFORE anything else (no alloc, no I/O).
	if(InterlockedCompareExchange(&s_crashHandling, 1, 0) != 0)
		return EXCEPTION_EXECUTE_HANDLER;   // recursive fault: don't redo work

	// 2. (a) log line — own SEH scope so a fault here still lets the dump run.
	__try {
		crashAppendLog(ep);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}

	// 3. (b) minidump — own SEH scope; failure/0-byte retry handled
	// internally; nothing raises out.
	__try {
		crashWriteDump(ep);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}

	// 4. Chain: previous filter runs AFTER our work; prefer its behavior.
	if(s_prevFilter)
		return s_prevFilter(ep);
	return EXCEPTION_EXECUTE_HANDLER;
}

// ------------------------------------------------------------
// (4) Best-effort WER LocalDumps backup — silent failure (likely no admin)
// ------------------------------------------------------------

static void
crashInstallWerBackup(void)
{
	char dir[MAX_PATH];
	if(!exeDir(dir, sizeof(dir)))
		return;
	size_t len = strlen(dir);
	if(len > 0 && dir[len - 1] == '\\'){
		dir[len - 1] = '\0';   // DumpFolder without trailing slash
		len--;
	}

	HKEY hKey = NULL;
	LONG rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
		"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\gta_sa.exe",
		0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
	if(rc != ERROR_SUCCESS)
		return;   // silent: no admin rights, etc. — never abort

	RegSetValueExA(hKey, "DumpFolder", 0, REG_EXPAND_SZ,
		(const BYTE *)dir, (DWORD)(len + 1));   // +1 = NUL
	DWORD one = 1;
	RegSetValueExA(hKey, "DumpType", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
	RegSetValueExA(hKey, "DumpMonitored", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
	RegCloseKey(hKey);
}

// ------------------------------------------------------------
// Self-installer: CRT static init runs this at DLL load
// ------------------------------------------------------------

struct CrashHandlerInstaller {
	CrashHandlerInstaller();
};

CrashHandlerInstaller::CrashHandlerInstaller()
{
	// Save + install; chaining order documented at top of file (we run first,
	// previous filter runs after us and its return value wins).
	s_prevFilter = SetUnhandledExceptionFilter(crashUnhandledFilter);

	// WER backup for the case our own dump write fails (disk full, etc.).
	crashInstallWerBackup();
}

// Namespace-scope static: constructor executes during CRT initialization of
// this DLL — no hook in main.cpp required.
CrashHandlerInstaller s_crashInstaller;

} // anonymous namespace
