// crashhandler.cpp — crash handling: diagnostic VEH (CRASH[n] receipts +
// crash-relevant stats), crash-forensics primitives, the fatal
// unhandled-exception filter (log / FORENSICS / ring / flush / minidump /
// WER backup), and the crash 0x5D97E6 shutdown-ordering-UAF resolutions
// (Phase 2 FIX A + FIX B, m0271) plus the second latent quit-UAF in the
// RpMatFX material destructor (Phase 2 FIX C, 0x810CB2, 2026-11-26).
// Bodies are byte-identical to the blocks
// de-merged from src/core/debug_handl.cpp (that file absorbed this TU on
// 2026-10-26; the de-merge restored it 2026-11-26, original ancestor at
// _backup/src/core/crashhandler.cpp). Owns:
//   * diag_crashHandler (VEH) + crash-receipt budget (per-module allowance +
//     global safety cap) + zone classify +
//     CRASH[n] receipts + crash counters + diag_dumpStats
//   * diag_installVEH / diag_removeVEH (s_oldVEH) + the g_inGuardedIm2DPass /
//     g_renderPhase definitions (their decls: crashhandler.h)
//   * the guarded sandbox CHOKEPOINTS guardedSetRT / guardedIm2DRender /
//     g_guardDepth (bodies moved VERBATIM here from render/pipelinecommon.cpp
//     at m0272; decls stay in skygfx.h:1638-1651; contract: crashhandler.h)
//   * crash* forensics primitives (module snapshot, address classify,
//     SEH-guarded safe-read, instruction decode)
//   * crashAppendLog / crashAppendForensics / crashFlushLog / crashWriteDump,
//     crashUnhandledFilter + step fences, crashInstallWerBackup,
//     CrashHandlerInstaller (CRT static self-install)
//   * CrashHandler_InstallSpecPoolGuard (Phase 2 FIX A), the 0x5D97E6
//     conjunctive VEH skip (Phase 2 FIX B) and the 0x810CB2 MatFX data-UAF
//     conjunctive VEH skip (Phase 2 FIX C) — see the PHASE 2 sections
// Install: diag_installVEH() is called once from main.cpp DllMain at the
// original install position (right after diag_init); the unhandled filter
// and FIX A also self-install from the CrashHandlerInstaller namespace-scope
// static (CRT static init at DLL load) — each call site documents why.
// BUILD: build/skygfx.vcxproj carries the ClCompile entry (premake5.lua
// globs src/core/*.cpp, but fast_build.py runs MSBuild off the .vcxproj —
// the E1 LNK2019 lesson: the .vcxproj line is mandatory).

// ---- Includes (the crash-handling half of the old deduped union) ----
#include "skygfx.h"
#include "crashhandler.h"   // own layer header (pulls L0 log.h)
#include "postfx.h"         // overrideIm2dPixelShader — used by the
                            // guardedIm2DRender body moved in at m0272
#include <windows.h>
#include <tlhelp32.h>
#include <DbgHelp.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "dbghelp.lib")
// ============================================================
// Crash-relevant counters (log dissection; see diag_dumpStats)
// ============================================================
// Deliberately tiny: only the families a crash review actually branches on.
// The VEH can fire on any thread, so every increment is interlocked.  No
// per-log-line counting — that would cost more than it explains.
static volatile LONG s_statVehCatch = 0;    // non-guarded VEH catches
static volatile LONG s_statAvRead = 0;
static volatile LONG s_statAvWrite = 0;
static volatile LONG s_statAvDep = 0;
static volatile LONG s_statAvOther = 0;
static volatile LONG s_statGuardFault = 0;  // faults inside the Im2D pass guard

// Crash-receipt budget accounting (m0273).  s_statVehCatch above still
// counts EVERY non-guarded catch — it increments before FIX B/FIX C and
// before any budget logic, so veh_catch is the truth about how many faults
// the VEH saw.  These two explain the gap between that number and the
// CRASH[n] lines actually in the log: receipts (granted) vs suppressed
// (caught by the budget and therefore not printed).
static volatile LONG s_statCrashReceipts = 0;   // CRASH[n] receipts emitted
static volatile LONG s_statCrashSuppressed = 0; // catches the budget dropped

// ============================================================
// CRASH FORENSICS PRIMITIVES (module map, SEH-guarded probes)
// ============================================================
// Shared by the first-chance VEH (one-line receipt enrichment) and the
// fatal unhandled filter (multi-line FORENSICS block).  Everything here is
// POD-only and bounded: no allocation, no C++ unwinding objects, so it stays
// safe to call from an arbitrary crashed thread.  The module list is
// snapshotted once lazily via Toolhelp (kernel32, 32-bit safe).

#define CRASH_MOD_MAX 96
struct CrashModuleInfo { DWORD base; DWORD size; char name[40]; };
static CrashModuleInfo s_crashMods[CRASH_MOD_MAX];
static int s_crashModCount = 0;
static int s_crashModsReady = 0;

static void crashSnapshotModules(void)
{
	// Latch BEFORE the first risky API call: if Toolhelp (or a re-entered VEH)
	// runs this again mid-snapshot, the second caller sees ready and scans
	// the (possibly partial) list instead of recursing into Toolhelp.
	if(s_crashModsReady) return;
	s_crashModsReady = 1;
	s_crashModCount = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if(snap != INVALID_HANDLE_VALUE){
		// W-variant only: the 10.0.19041 um/tlhelp32.h declares no
		// MODULEENTRY32A/Module32FirstA at all (A names are C2065 there).
		// Module names are ASCII; wchar->char truncation is safe.
		MODULEENTRY32W me;
		me.dwSize = sizeof(me);
		if(Module32FirstW(snap, &me)){
			do {
				if(s_crashModCount >= CRASH_MOD_MAX) break;
				CrashModuleInfo *m = &s_crashMods[s_crashModCount];
				m->base = (DWORD)(uintptr_t)me.modBaseAddr;
				m->size = (DWORD)me.modBaseSize;
				int i = 0;
				for(; me.szModule[i] && i < (int)sizeof(m->name) - 1; i++)
					m->name[i] = (char)me.szModule[i];
				m->name[i] = 0;
				s_crashModCount++;
			} while(Module32NextW(snap, &me));
		}
		CloseHandle(snap);
	}
	// s_crashModsReady was latched at entry; never retried per stack dword.
}

// Resolve addr to "module+offset".  out/outOff may be NULL.  Returns 1 when
// the address lands inside a snapshotted module image, 0 otherwise.
static int crashFindModule(DWORD addr, char *out, int cap, DWORD *outOff)
{
	if(!s_crashModsReady) crashSnapshotModules();
	for(int i = 0; i < s_crashModCount; i++){
		DWORD base = s_crashMods[i].base;
		DWORD end = base + s_crashMods[i].size;
		if(end < base) end = 0xFFFFFFFFu;   // size overflow guard
		if(addr >= base && addr < end){
			if(out && cap > 0){
				int j = 0;
				for(; s_crashMods[i].name[j] && j < cap - 1; j++)
					out[j] = s_crashMods[i].name[j];
				out[j] = 0;
			}
			if(outOff) *outOff = addr - base;
			return 1;
		}
	}
	if(out && cap > 0) out[0] = 0;
	if(outOff) *outOff = 0;
	return 0;
}

static void crashFormatMod(DWORD addr, char *out, int cap)
{
	if(!out || cap <= 0) return;
	char mn[40];
	DWORD off = 0;
	if(crashFindModule(addr, mn, sizeof(mn), &off))
		_snprintf(out, cap, "%s+0x%X", mn, (unsigned)off);
	else
		_snprintf(out, cap, "(unmapped)");
	out[cap - 1] = 0;
}

// Classify an arbitrary dword from the stack window: module+offset when it is
// a code/data address inside an image, else the VirtualQuery region kind.
static void crashClassifyAddr(DWORD addr, char *out, int cap)
{
	if(!out || cap <= 0) return;
	char mn[40];
	DWORD off = 0;
	if(crashFindModule(addr, mn, sizeof(mn), &off)){
		_snprintf(out, cap, "%s+0x%X", mn, (unsigned)off);
		out[cap - 1] = 0;
		return;
	}
	MEMORY_BASIC_INFORMATION mbi = {};
	if(VirtualQuery((LPCVOID)(uintptr_t)addr, &mbi, sizeof(mbi)) == sizeof(mbi)){
		if(mbi.State == MEM_FREE)
			_snprintf(out, cap, "[free]");
		else if(mbi.Type == MEM_IMAGE)
			_snprintf(out, cap, "[image]");
		else if(mbi.Type == MEM_MAPPED)
			_snprintf(out, cap, "[mapped]");
		else
			_snprintf(out, cap, "[private]");
	} else {
		_snprintf(out, cap, "[unmapped]");
	}
	out[cap - 1] = 0;
}

// SEH-guarded byte reader: copies up to n bytes, stops at the first faulting
// byte (page-boundary safe) and returns the count actually copied.  POD-only
// body with a per-byte try so a fault mid-window still yields the prefix.
static int crashSafeRead(const void *src, void *dst, int n)
{
	int got = 0;
	const unsigned char *s = (const unsigned char *)src;
	unsigned char *d = (unsigned char *)dst;
	for(int i = 0; i < n; i++){
		int ok = 0;
		__try {
			d[i] = s[i];
			ok = 1;
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			ok = 0;
		}
		if(!ok) break;
		got++;
	}
	return got;
}

// Decode the first 12 bytes at eip as "instr=AA BB ..." (partial on fault).
static void crashFormatInstr(DWORD eip, char *out, int cap)
{
	if(!out || cap <= 0) return;
	unsigned char ib[12];
	int got = crashSafeRead((const void *)(uintptr_t)eip, ib, (int)sizeof(ib));
	int p = _snprintf(out, cap, "instr=");
	if(p < 0) p = 0;
	for(int i = 0; i < got && p < cap - 1; i++){
		int w = _snprintf(out + p, cap - p, "%02X ", (unsigned)ib[i]);
		if(w <= 0) break;
		p += w;
	}
	if(got < (int)sizeof(ib) && p < cap - 1)
		_snprintf(out + p, cap - p, "(partial)");
	out[cap - 1] = 0;
}


// ============================================================
// PHASE 3 — VEH receipt re-entrancy claim (m0271 fortification)
// ============================================================
// The fatal filter protects itself with s_crashHandling; the VEH had NO
// claim, so a fault raised inside its own receipt work (zone classify, scope
// lookup, formatting, dbglog's file I/O) re-entered diag_crashHandler and
// re-ran the receipt — an unbounded fault-in-receipt loop able to hang a
// crash in place of logging it.  Claimed/released around the receipt block
// in diag_crashHandler below; a nested re-entry sees the claim held and
// hands the exception straight back (EXCEPTION_CONTINUE_SEARCH — the VEH's
// only disposition, never a swallow).
// 0 = receipt idle, 1 = receipt work in progress (some thread).
static volatile LONG s_vehReceiptBusy = 0;


// ============================================================
// PHASE 2 / FIX B — 0x5D97E6 shutdown-UAF conjunctive skip (defense in depth)
// ============================================================
// Root cause (proven): pluginSpecMatDestructorCB (0x5D97D0) does
//   data = material->specData; if (data) { if (data->Texture) ...
// and the `data->Texture` load at 0x5D97E6 is `mov eax,[esi+4]` (texture is
// CustomSpecMapPipeMaterialData +0x4).  CGame::Shutdown runs
// CCarFXRenderer::Shutdown -> CCustomCarEnvMapPipeline::DestroyPipe
// (0x5DA130) FIRST, which deletes the 4096-slot spec pool (32 KB) and nulls
// the pool pointer; THEN RsEventHandler(rsRWTERMINATE) destroys the surviving
// materials, whose specData still dangles -> UAF read at QUIT, not in gameplay.
// FIX A (CrashHandler_InstallSpecPoolGuard) removes the free at its source;
// THIS is the backstop for any state where the pool is already gone:
//   code == 0xC0000005                       (access violation only)
//   AND site == 0x5D97E6                     (EXACT faulting instruction)
//   AND (pool pointer at 0xC02D30 == 0
//        OR esi outside [base, base+n*8)     (slot not in the live entries)
//        OR the slot's tPoolObjectFlags.bEmpty bit 0x80 is set)  (freed slot)
// all three conjuncts must hold; any fault while probing the pool evidence
// aborts the probe -> NO skip (crashSpecPoolSkipSafe), so unrelated faults
// propagate exactly as before — the "never swallow unrelated faults"
// discipline is preserved.  Recovery redirects EIP to the shared CB epilogue
// at 0x5D9820 (pop edi; pop esi; mov eax,ebx; pop ebx; ret): the frame is
// exactly push ebx/esi/edi with no locals — stack-neutral — and the CB
// convention returns the object in EAX (= EBX here), so the redirect is
// stack- and register-neutral.
static const DWORD kSpecCbFault     = 0x5D97E6; // mov eax,[esi+4] — data->Texture load
static const DWORD kSpecCbPoolLoad  = 0x5D97FD; // second site: pool->Delete path, pool ptr == 0
static const DWORD kSpecCbEpilogue  = 0x5D9820; // shared epilogue: pop edi; pop esi; mov eax,ebx; pop ebx; ret
// CPool<CustomSpecMapPipeMaterialData>*, plugin-sdk CCustomCarEnvMapPipeline
// (address in the declaration layer per HARD RULE — read-only probe target).
static const uint32_t kSpecPoolPtr = 0xC02D30;

// POD-only SEH island (same pattern as debugReadTick in debug_handl.cpp):
// no C++ objects, one __try, so it is C2712-safe and callable from the VEH.
// Returns 1 = safe to skip the fault, 0 = do not touch it.
static int __declspec(noinline)
crashSpecPoolSkipSafe(DWORD site, DWORD esi, DWORD *outWhy, DWORD *outPool)
{
	int skip = 0;
	*outWhy = 0;
	*outPool = 0;
	__try {
		uint32_t pool = *(const uint32_t *)(uintptr_t)kSpecPoolPtr;
		*outPool = pool;
		if(site == kSpecCbFault){
			if(pool == 0){
				skip = 1; *outWhy = 1;   // DestroyPipe already ran + nulled
			}else{
				uint32_t base  = *(const uint32_t *)(uintptr_t)(pool + 0x00); // m_pObjects
				uint32_t bmap  = *(const uint32_t *)(uintptr_t)(pool + 0x04); // m_byteMap
				int      nSize = *(const int      *)(uintptr_t)(pool + 0x08); // m_nSize
				uint32_t off   = esi - base;   // unsigned wrap: esi<base -> huge -> out of range
				if(nSize <= 0 || off >= (uint32_t)nSize * 8u){
					skip = 1; *outWhy = 2;    // esi not within [base, base+n*8)
				}else if(bmap &&
					(((const unsigned char *)(uintptr_t)bmap)[off / 8u] & 0x80) != 0){
					skip = 1; *outWhy = 3;    // bEmpty (bit 0x80): slot already freed
				}
			}
		}else if(site == kSpecCbPoolLoad && pool == 0){
			skip = 1; *outWhy = 4;            // second site, same skip
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		skip = 0;   // probe faulted: cannot verify -> never skip, never swallow
	}
	return skip;
}
// ============================================================
// PHASE 2 / FIX C — 0x810CB2 RpMatFX data-struct shutdown-UAF skip
// ============================================================
// Root cause (proven 2026-11-26, same quit chain as 0x5D97E6): the RpMatFX
// material plugin (registered by RpMatFXPluginAttach 0x810AA0; offset global
// MatFXMaterialDataOffset @ 0xC9AB74) stores a 4-BYTE POINTER at
// material+offset to a separately-allocated data struct.  Its destructor
// (0x810C60) loads that pointer and hands it to the effect-teardown helper
// (0x810CA0), whose switch reads [data+0x14] at 0x810CB2
// (`mov eax,[esi+0x10]`, esi=data+4).  At QUIT the material is still alive
// but the data struct was already freed WITHOUT the field being nulled
// (early freer unidentified — same DestroyPipe-before-RwTerminate ordering
// family as the specpool bug; no skygfx hook touches 0x810xxx), so the read
// AVs on unmapped heap.  FIX A removed the first quit-UAF (0x5D97E6); this
// is the next one destruction reaches.  Conjuncts (ALL must hold):
//   code == 0xC0000005 && accessType == READ      (AV read only)
//   AND ExceptionAddress == 0x810CB2              (EXACT faulting instruction)
//   AND [esp+0x10] == 0x810C78                    (return into the dtor ONLY —
//      excludes the helper's second caller at 0x811CDF, whose frame differs)
//   AND ExceptionInformation[1] == ebp + 0x14     (fault is data+0x14; ebp=data)
//   AND image bytes match: 0x810CB2 = 8B 46 10 48, 0x810C9B = 8B C7 5F 5E
//   AND a safe read of data+0x14 FAULTS            (freed struct confirmed;
//      readable -> skip=0, fault propagates — never-swallow discipline)
// Recovery (frame math verified against the captured dump):
//   ESP_crash = dtor ESP0-0x20 (helper pushed ebx,ebp,esi,edi; no sub esp),
//   so [ESP_crash+0] = helper's saved edi = the dtor's EDI = the material,
//   and ESP_crash+0x18 = the dtor's own saved-edi slot (ESP0-8).  Redirect:
//   Edi = material; Esp += 0x18; Eip = 0x810C9B
//   (`mov eax,edi; pop edi; pop esi; ret`) — skips the manager FREE (no
//   double-free of the already-freed block) and the field zero (the material
//   is dying anyway), restores the registry caller's edi/esi from the dtor
//   frame, returns the object in EAX per the CB convention, lands exactly on
//   ret-to-0x80875D (_rwPluginRegistryApply).  Stack-neutral vs the dtor.
static const DWORD kMatFxFault    = 0x810CB2; // mov eax,[esi+0x10]
static const DWORD kMatFxHelperLo = 0x810CA0; // helper entry (extended guard)
static const DWORD kMatFxHelperHi = 0x810D20; // helper end (padding)
static const DWORD kMatFxCallerRet= 0x810C78; // ret into dtor 0x810C60 (call at 0x810C73)
static const DWORD kMatFxRecover  = 0x810C9B; // mov eax,edi; pop edi; pop esi; ret
static const DWORD kMatFxFaultByte= 0x4810468B; // 8B 46 10 48 @ 0x810CB2
static const DWORD kMatFxRecovByte= 0x5E5FC78B; // 8B C7 5F 5E @ 0x810C9B
// FIX D (2026-11-28): Rw plugin-destructor NULL-container UAF at shutdown.
// 0x801D50 = push ebx/ebp/esi/edi; mov edi,[esp+0x14]; the arg (freed
// CustomSpecMap data) is NULL -> 0x801D58 `mov esi,[edi+0x10]` faults at 0x10.
// Called from the MatFX dtor at 0x810C8C. NULL-return epilogue at 0x801D83
// (pop edi;pop esi;pop ebp;xor eax,eax;pop ebx;ret) is stack-neutral and
// returns NULL � the fail-open result for a missing material-data lookup.
static const DWORD kUafNullFault   = 0x801D58;
static const DWORD kUafNullCaller  = 0x810C8C;
static const DWORD kUafNullRecover = 0x801D83;
static const DWORD kUafNullFaultB  = 0x8B10778B; // 8B 77 10 8B @ fault
static const DWORD kUafNullRecovB  = 0x335D5E5F; // 5F 5E 5D 33 @ recover
// FIX E (2026-11-28): FxManager_c::DestroyFxSystem -> List_c::RemoveItem UAF.
// Field log 06:11:20: CRASH[3212] WRITE at 0x4A8E49 accessing 0x3F800004,
// EAX=0x3F800000 (float 1.0f bit pattern used as a LIST LINK POINTER),
// ESI=EDI=0x229C5D18 (the FxSystem being removed, still valid),
// [ESP+4]=0x4A9886 (the ONLY caller: the call in DestroyFxSystem at 0x4A9881).
// The instruction is `89 50 04` (mov [eax+4],edx = next->prev = item->prev);
// EAX (= item->m_link.next) is already garbage, so the write AVs.  The prior
// unlink already stored 1.0f into prev->next (a valid block) — that damage is
// done and this is the LAST fx system torn down at quit, so the list is not
// traversed again.  Recovery: skip ONLY the faulting instruction (+3 bytes,
// runtime bytes confirmed by the [FORENSIC] instr= dump); RemoveItem's own
// tail (fix-10 style count decrement at 0x4A8E4C..: 8B 41 08 / 8D 50 FF /
// 89 51 08) and its normal epilogue then run, so NO manual frame surgery is
// needed — registers and stack stay exactly as the function expects.
// Conjuncts (ALL must hold; probe faults => no skip, never swallow):
//   code==0xC0000005, accessType==WRITE(1), EIP==0x4A8E49,
//   faultAddr==EAX+4, EAX is a float bit pattern [0x3F000000..0x3FFFFFFF]
//   (0.5f..2.0f — any float-as-link, not just 1.0f), ESI==EDI, [ESP+4]==0x4A9886.
// Static image bytes at 0x4A8E30 are NOT code (Hoodlum runtime-unpacked EXE) —
// the runtime instruction bytes are taken from the crash receipt itself.
static const DWORD kFxListFault   = 0x4A8E49;
static const DWORD kFxListNext    = 0x4A8E4C; // +3 (mov [eax+4],edx is 3 bytes)
static const DWORD kFxListCaller  = 0x4A9886; // ret into DestroyFxSystem
static const DWORD kFxListBadLinkLo = 0x3F000000; // 0.5f bits — float-as-link range
static const DWORD kFxListBadLinkHi = 0x3FFFFFFF; // ~2.0f bits (any float in [0.5,2))
// Read-only probe targets; addresses live in this declaration layer per the
// HARD RULE (install/forensics-layer exemption documented in crashhandler.h).

// POD-only SEH island (same pattern as crashSpecPoolSkipSafe): any probe
// fault inside the evidence reads degrades per-branch — only a fault on the
// DATA read itself means "skip".  Returns 1 = safe to skip.
static int __declspec(noinline)
crashMatFxSkipSafe(DWORD site, DWORD esp, DWORD ebp, DWORD accessType,
	DWORD faultAddr, DWORD *outWhy, DWORD *outMaterial)
{
	int skip = 0;
	*outWhy = 0;
	*outMaterial = 0;
	__try {
		if(accessType == 0 &&
		   (site == kMatFxFault || (site >= kMatFxHelperLo && site <= kMatFxHelperHi)) &&
		   *(const volatile DWORD *)(uintptr_t)(esp + 0x10) == kMatFxCallerRet &&
		   *(const volatile DWORD *)(uintptr_t)kMatFxFault == kMatFxFaultByte &&
		   *(const volatile DWORD *)(uintptr_t)kMatFxRecover == kMatFxRecovByte)
		{
			*outMaterial = *(const volatile DWORD *)(uintptr_t)esp; // helper saved-edi = material
			volatile DWORD probe = *(const volatile DWORD *)(uintptr_t)faultAddr;
			(void)probe;
			skip = 0; *outWhy = 5;  // data+0x14 readable: NOT the UAF -> propagate
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		skip = 1; *outWhy = 1;      // data+0x14 unmapped: freed struct confirmed
	}
	return skip;
}
// FIX E probe island — POD only, no inline (C2712-safe like FIX B/C).  The
// probeArmed flag ensures only a fault on the DATA read sets "skip": a fault
// in any conjunct read propagates the original exception (never-swallow).
static int __declspec(noinline)
crashFxListSkipSafe(DWORD site, DWORD esp, DWORD accessType, DWORD faultAddr,
                    DWORD eaxValue, DWORD esiValue, DWORD ediValue)
{
	int skip = 0;
	volatile bool probeArmed = false;
	__try {
		if(accessType == 1 && site == kFxListFault &&
		   faultAddr == eaxValue + 4 &&
		   eaxValue >= kFxListBadLinkLo && eaxValue <= kFxListBadLinkHi &&
		   esiValue == ediValue &&
		   *(const volatile DWORD *)(uintptr_t)(esp + 4) == kFxListCaller)
		{
			probeArmed = true;
			volatile DWORD probe = *(const volatile DWORD *)(uintptr_t)faultAddr;
			(void)probe;
			skip = 0;   // link readable: a different fault class -> propagate
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		if(probeArmed) skip = 1;   // all conjuncts held + data unmapped -> UAF
	}
	return skip;
}
// ============================================================
// CRASH-RECEIPT BUDGET — per-module allowance + global safety cap (m0273)
// ============================================================
// Defect fixed (session forensics 2026-11-26 23:41:10 -> 23:42:04): the
// budget was ONE global `crashCount > 20`, so a third-party ASI fault storm
// (20x "CRASH[n] ... mod=Renewed120.asi+0xD41D NULLDEREF" inside a 143 ms
// burst) spent the entire allowance before any skygfx fault could be
// reported — the 148 VEH catches that followed logged NOTHING, i.e. a real
// skygfx fault in the last 3 s would have been invisible.  A budget a single
// external module can exhaust hides exactly the faults it exists to show.
//
// Shape now — two gates, both evaluated BEFORE the only file I/O in the
// handler (dbglog opens+closes the log per line, which is what the budget
// bounds):
//   * per-module allowance CRASH_MOD_ALLOWANCE — a burst from ONE faulting
//     module spends only that module's slots; every other module, and
//     skygfx itself, keeps its receipts;
//   * global safety cap CRASH_BUDGET_GLOBAL — hard bound on total receipt
//     file I/O for the process (the fault-loop floor the old 20 provided).
// Past both gates a catch is never dropped silently: s_statCrashSuppressed
// counts it, ONE throttled [crashbudget] receipt names it, and
// diag_dumpStats prints per-module granted/suppressed tallies.
//
// Invariants held (unchanged by this block):
//   * disposition: every path still returns EXCEPTION_CONTINUE_SEARCH — the
//     VEH is diagnostic-only and never swallows;
//   * FIX B (specpool 0x5D97E6) and FIX C (MatFX 0x810CB2) run ABOVE this
//     block, so a recovered UAF never spends budget and never counts as a
//     receipt;
//   * s_statVehCatch keeps counting everything (it increments above FIX B);
//   * CRASH[%d] keeps its format AND its meaning (the catch ordinal).
// Fault-path cost: one FNV-1a walk over a <=40-char module name, one
// <=96-entry linear module scan (the Toolhelp snapshot is latched once per
// process) and a few interlocked ops.  No heap, no std::map, no I/O.
#define CRASH_MOD_ALLOWANCE 8    // receipts one faulting module may spend
#define CRASH_BUDGET_GLOBAL 48    // hard cap: total receipts per process
#define CRASH_MOD_SLOTS 8        // fixed POD tally table (heap-free, map-free)

// One row per faulting module.  hash matches WITHOUT a string compare on the
// fault path; name is copied in at install time only, so the session summary
// can NAME the module that spent the budget instead of reporting a hash.
struct CrashModBudget {
	volatile LONG hash;   // FNV-1a of the module name; 0 = free row
	LONG recv;            // receipts granted to this module
	LONG sup;             // catches this module had suppressed
	char name[40];        // module name for the [STATS] tally (install-time)
};
static CrashModBudget s_crashModBudget[CRASH_MOD_SLOTS];
static volatile LONG  s_crashBudgetTotal = 0;  // receipts granted, all modules

// FNV-1a over a module name.  0 is the free-row sentinel, so a 0 hash folds
// to 1.  POD, no allocation, bounded by the 40-char caller buffer.
static uint32_t crashModHash(const char *s)
{
	uint32_t h = 2166136261u;
	for(const char *p = s; *p; p++){
		h ^= (uint32)(unsigned char)*p;
		h *= 16777619u;
	}
	return h ? h : 1u;
}

// Claim ONE receipt slot for faulting module `name`.  Returns 1 = granted
// (caller prints the CRASH[n] receipt), 0 = refused; either way the module's
// tally is updated so the gap is visible in [STATS].
// Interlocked because the VEH can fire on any thread.  A race between threads
// can mis-attribute a tally row or overshoot an allowance by the racing
// thread count at most — never the global cap, which is the actual bound on
// file I/O, so no lock is worth taking on a faulting thread.
static int crashBudgetClaim(const char *name)
{
	uint32_t h = crashModHash(name);
	int slot = -1, freeIdx = -1, lowIdx = -1;
	LONG low = 0;
	for(int i = 0; i < CRASH_MOD_SLOTS; i++){
		CrashModBudget *b = &s_crashModBudget[i];
		if(b->hash == (LONG)h){ slot = i; break; }
		if(b->hash == 0){
			if(freeIdx < 0) freeIdx = i;
		}else{
			LONG used = b->recv + b->sup;
			if(lowIdx < 0 || used < low){ low = used; lowIdx = i; }
		}
	}
	if(slot < 0)
		slot = (freeIdx >= 0) ? freeIdx : lowIdx;  // table full: recycle the coolest row
	if(slot < 0)
		return 0;   // unreachable with CRASH_MOD_SLOTS > 0; fail closed (no receipt)

	CrashModBudget *b = &s_crashModBudget[slot];
	if(b->hash != (LONG)h){
		// First occupant of this row (or a recycled one): install the key,
		// reset the row, copy the name for the summary tally.
		b->hash = (LONG)h;
		b->recv = 0;
		b->sup = 0;
		strncpy(b->name, name, sizeof(b->name) - 1);
		b->name[sizeof(b->name) - 1] = 0;
	}

	// Gate 1 — per-module allowance: one module's storm spends only its own
	// slots, so an external ASI can never crowd out other/faulting modules.
	if(InterlockedIncrement(&b->recv) > CRASH_MOD_ALLOWANCE){
		InterlockedDecrement(&b->recv);   // recv stays "receipts granted"
		InterlockedIncrement(&b->sup);
		return 0;
	}
	// Gate 2 — global safety cap: bounds TOTAL per-process receipt file I/O
	// when many distinct modules fault (the fault-loop floor).
	if(InterlockedIncrement(&s_crashBudgetTotal) > CRASH_BUDGET_GLOBAL){
		InterlockedDecrement(&s_crashBudgetTotal);
		InterlockedDecrement(&b->recv);
		InterlockedIncrement(&b->sup);
		return 0;
	}
	return 1;
}
// ============================================================
// VEH crash handler — matches original skygfx crashHandler
// Logs crashes, auto-fixes known issues, suppresses rendering crashes
// ============================================================

static LONG WINAPI
diag_crashHandler(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;

	// Let C++ exceptions (0xE06D7363) pass through to try/catch blocks in readint/readfloat
	// Swallowing them with EXCEPTION_EXECUTE_HANDLER corrupts C++ exception handling
	if(code == 0xE06D7363)
		return EXCEPTION_CONTINUE_SEARCH;

	// DBG_PRINTEXCEPTION_C (0x40010006) — the benign OutputDebugString
	// first-chance. Never a real crash: logging it burned the whole
	// crash-receipt budget (the old 20-trip cap) and dumped ~680 junk
	// ring-buffer lines per trip. Ignore
	// silently; disposition is unchanged (EXCEPTION_CONTINUE_SEARCH — the
	// exception still reaches whoever owns it, other codes unaffected).
	if(code == 0x40010006)
		return EXCEPTION_CONTINUE_SEARCH;

	// Inside SEH-guarded Im2D dispatches the VEH must be a pure pass-through.
	// Any logging here does file I/O on the faulting thread and derails
	// dispatch before frame-based __try/__except handlers can run.
	// Forensics first: push a bounded ring entry (no file I/O, no malloc) so a
	// later hard-crash ring dump carries the guarded-phase context. Behavior
	// past this point (EXCEPTION_CONTINUE_SEARCH) is unchanged.
	if(g_inGuardedIm2DPass){
		char gmsg[RING_MSG_LEN];
		_snprintf(gmsg, sizeof(gmsg), "GUARD-FAULT eip=%08X phase=%s depth=%ld",
			ep->ContextRecord->Eip,
			g_renderPhase ? g_renderPhase : "",
			(long)g_guardDepth);
		gmsg[sizeof(gmsg) - 1] = 0;
		diag_ringPut(gmsg);
		InterlockedIncrement(&s_statGuardFault);   // stats: guarded-phase faults
		return EXCEPTION_CONTINUE_SEARCH;
	}

	InterlockedIncrement(&s_statVehCatch);   // stats: all non-guarded catches

	// ---- PHASE 2 / FIX B — conjunctive 0x5D97E6 shutdown-UAF skip ----
	// Runs BEFORE the crash-receipt budget and before any receipt work: a
	// recovered fault must never consume the receipt budget (a full material
	// set would starve its own module's allowance) and must not be reported
	// as a catch.  Three conjuncts
	// only; probe faults degrade to "no skip" (see crashSpecPoolSkipSafe).
	if(code == 0xC0000005){
		DWORD skipWhy = 0, skipPool = 0;
		if(crashSpecPoolSkipSafe(
			(DWORD)(uintptr_t)ep->ExceptionRecord->ExceptionAddress,
			ep->ContextRecord->Esi, &skipWhy, &skipPool))
		{
			// Evidence line (throttled tag specpool_skip): bounded to one
			// receipt per throttle window — the recovery must not become
			// the log storm it exists to prevent.  Own claim/release so it
			// obeys the Phase 3 receipt discipline too.
			if(InterlockedCompareExchange(&s_vehReceiptBusy, 1, 0) == 0){
				if(dbglog_throttle("specpool_skip"))
					dbglog("[specpool_skip] UAF skip: eip=0x%08X pool=%08X esi=%08X why=%lu -> eip=0x%08X",
						kSpecCbFault, skipPool, ep->ContextRecord->Esi,
						skipWhy, kSpecCbEpilogue);
				InterlockedExchange(&s_vehReceiptBusy, 0);
			}
			ep->ContextRecord->Eip = kSpecCbEpilogue;   // shared CB epilogue
			ep->ContextRecord->Eax = ep->ContextRecord->Ebx;
			return EXCEPTION_CONTINUE_EXECUTION;

		}
	}

	// ---- PHASE 2 / FIX C — conjunctive 0x810CB2 MatFX data-UAF skip ----
	// Same discipline as FIX B: runs before the crash-receipt budget and before
	// any receipt work; all conjuncts inside the POD probe island; probe
	// faults degrade to "no skip".  ExceptionInformation[0]=0/1/8 = READ/
	// WRITE/DEP, [1] = the accessed address.
	if(code == 0xC0000005){
		DWORD mfWhy = 0, mfMaterial = 0;
		DWORD accessType = ep->ExceptionRecord->ExceptionInformation[0];
		DWORD faultAddr  = ep->ExceptionRecord->ExceptionInformation[1];
	// ---- FIX D: Rw plugin-destructor NULL-container UAF (shutdown) ----
	if(code == 0xC0000005 && accessType == 0 &&
	   (DWORD)(uintptr_t)ep->ExceptionRecord->ExceptionAddress == kUafNullFault &&
	   faultAddr == 0x10u)
	{
		bool verified = false;
		__try {
			verified = *(const volatile DWORD *)(uintptr_t)kUafNullFault == kUafNullFaultB &&
			           *(const volatile DWORD *)(uintptr_t)kUafNullRecover == kUafNullRecovB &&
			           *(const volatile DWORD *)(uintptr_t)(ep->ContextRecord->Esp + 0x10) == kUafNullCaller;
		} __except(EXCEPTION_EXECUTE_HANDLER){ verified = false; }
		if(verified){
			if(InterlockedCompareExchange(&s_vehReceiptBusy, 1, 0) == 0){
				if(dbglog_throttle("uaf_null_skip"))
					dbglog("[uaf_null_skip] Rw plugin NULL container: eip=0x%08X fault=0x%08X -> NULL return at 0x%08X",
					       kUafNullFault, faultAddr, kUafNullRecover);
				InterlockedExchange(&s_vehReceiptBusy, 0);
			}
			ep->ContextRecord->Eax = 0;
			ep->ContextRecord->Eip = kUafNullRecover;
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}
	// ---- FIX E: 0x4A8E49 FxManager destroy LIST link UAF (quit) ----
	// Runs above the crash-receipt budget like FIX B/C/D: a recovered fault
	// must not consume a receipt slot.  All conjuncts are checked in the POD
	// probe island so any non-UAF fault propagates (never-swallow).  Recovery
	// skips only the 3-byte faulting store; RemoveItem's own epilogue restores
	// EBX/ESI/EDI/ESP, so no register or stack surgery is performed here.
	if(accessType == 1 &&
	   crashFxListSkipSafe(
	       (DWORD)(uintptr_t)ep->ExceptionRecord->ExceptionAddress,
	       ep->ContextRecord->Esp, accessType, faultAddr,
	       ep->ContextRecord->Eax, ep->ContextRecord->Esi, ep->ContextRecord->Edi))
	{
		if(InterlockedCompareExchange(&s_vehReceiptBusy, 1, 0) == 0){
			if(dbglog_throttle("fxlist_uaf_skip"))
				dbglog("[fxlist_uaf_skip] FxManager LIST UAF skip: eip=0x%08X link=%08X fault=0x%08X item=%08X -> eip=0x%08X",
				       kFxListFault, ep->ContextRecord->Eax, faultAddr,
				       ep->ContextRecord->Esi, kFxListNext);
			InterlockedExchange(&s_vehReceiptBusy, 0);
		}
		ep->ContextRecord->Eip = kFxListNext;   // skip only the bad store
		return EXCEPTION_CONTINUE_EXECUTION;
	}
		if(crashMatFxSkipSafe(
			(DWORD)(uintptr_t)ep->ExceptionRecord->ExceptionAddress,
			ep->ContextRecord->Esp, ep->ContextRecord->Ebp,
			accessType, faultAddr, &mfWhy, &mfMaterial))
		{
			// Evidence line (throttled tag matfx_uaf_skip, FNV-clean on the
			// 1024/256 buckets): bounded, own claim/release (Phase 3 rules).
			if(InterlockedCompareExchange(&s_vehReceiptBusy, 1, 0) == 0){
				if(dbglog_throttle("matfx_uaf_skip"))
					dbglog("[matfx_uaf_skip] MatFX data UAF skip: eip=0x%08X ret=%08X data=%08X fault=%08X why=%lu -> eip=0x%08X",
						kMatFxFault, kMatFxCallerRet, ep->ContextRecord->Ebp,
						faultAddr, mfWhy, kMatFxRecover);
				InterlockedExchange(&s_vehReceiptBusy, 0);
			}
			ep->ContextRecord->Edi = mfMaterial;   // dtor's EDI (material) for `mov eax,edi`
			ep->ContextRecord->Esp = ep->ContextRecord->Esp + 0x18; // dtor saved-edi slot
			ep->ContextRecord->Eip = kMatFxRecover; // skip free+zero, pops+ret to registry
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}

	static int crashCount = 0;
	++crashCount;   // catch ordinal — the CRASH[%d] field, meaning unchanged

	CONTEXT *ctx = ep->ContextRecord;
	DWORD eip = ctx->Eip;
	DWORD esp = ctx->Esp;

	// PHASE 3 — claim all receipt work below (module resolve, format, scope
	// lookup, file I/O, the budget's throttled notice).  If the claim is
	// already held we are the nested re-entry of a fault raised inside our
	// own receipt: hand the exception straight back with the same disposition
	// as the normal tail — no repeated work, no loop, no swallow.
	if(InterlockedCompareExchange(&s_vehReceiptBusy, 1, 0) != 0)
		return EXCEPTION_CONTINUE_SEARCH;

	// ---- CRASH-RECEIPT BUDGET — per-module allowance + global safety cap ----
	// AFTER FIX B / FIX C (a recovered UAF must never spend budget) and
	// BEFORE the only file I/O in this handler.  The module resolve below is
	// the same source the receipt's own mod= field uses (crashFormatMod
	// resolves eip through crashFindModule too), so budget identity and the
	// logged mod= can never disagree.  It is a bounded linear scan of the
	// snapshotted module list — no I/O, no allocation — so a storm already
	// past the cap costs a few compares per catch, not a file open.
	char modName[40];
	if(!crashFindModule(eip, modName, sizeof(modName), NULL) || !modName[0])
		memcpy(modName, "(unmapped)", sizeof("(unmapped)"));

	if(!crashBudgetClaim(modName)){
		// Suppressed, but never SILENTLY: counted here, named by ONE
		// throttled receipt per window (dbglog_throttle, the same 2 s bucket
		// discipline FIX B/C evidence lines use), and tallied per module in
		// diag_dumpStats.  Release the claim before the tail so the next
		// catch's notice can run.
		InterlockedIncrement(&s_statCrashSuppressed);
		if(dbglog_throttle("crash_budget"))
			dbglog("[crashbudget] crash receipt budget exhausted: last mod=%s suppressed=%ld granted=%ld (global cap=%d per-module=%d)",
				modName, (long)s_statCrashSuppressed, (long)s_crashBudgetTotal,
				(int)CRASH_BUDGET_GLOBAL, (int)CRASH_MOD_ALLOWANCE);
		InterlockedExchange(&s_vehReceiptBusy, 0);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	InterlockedIncrement(&s_statCrashReceipts);

	// Determine crash context from EIP range
	const char *crashZone = "UNKNOWN";
	if(eip >= 0x400000 && eip < 0x800000) crashZone = "GAME_TEXT";
	else if(eip >= 0x10000000) crashZone = "HEAP/DLL";
	else if(eip < 0x10000) crashZone = "NULL_DEREF";

	// Scope tag lookup: nearest skygfx function (cheap, no I/O)
	char scopeName[128];
	int scopeOff;
	diag_lookupScope((void*)(uintptr_t)eip, scopeName, sizeof(scopeName), &scopeOff);

	// ONE line per VEH catch.  This handler also fires for RECOVERED
	// first-chance faults (SEH __except guards elsewhere — SMAA passes,
	// guardedSetRT, the frame-id reads), so the old 5-line receipt plus a
	// per-catch diag_ringDump() re-emitted every recent line bare on each
	// catch (~215 boot-log lines for 8 recovered NULL+8 reads), and each
	// receipt logged through dbglog re-faulted recursively while the
	// frame-id read was unguarded.  Regs/scope are folded inline; the
	// greppable CRASH[N] token and the zone/type/access fields survive.
	// The ring dump belongs to the fatal path only (crashUnhandledFilter).

	// Forensics folded into the ONE receipt line (log dissection): the
	// faulting EIP is resolved to module+offset and the first 12 instruction
	// bytes are decoded in place.  Both are APPENDED after the existing
	// fields so every current grep token (CRASH[, zone=, accessing, scope=)
	// stays byte-stable.  The full 24-dword stack window is reserved for the
	// fatal filter path (crashAppendForensics) so recovered first-chance
	// catches remain one line each.
	char crashModTag[64];
	crashFormatMod(eip, crashModTag, sizeof(crashModTag));
	char crashInstrTag[96];
	crashFormatInstr(eip, crashInstrTag, sizeof(crashInstrTag));

	if(code == 0xC0000005){
		DWORD accessType = ep->ExceptionRecord->ExceptionInformation[0];
		DWORD accessAddr = ep->ExceptionRecord->ExceptionInformation[1];
		const char *crashType =
			accessType == 0 ? "READ" :
			accessType == 1 ? "WRITE" :
			accessType == 8 ? "DEP" : "ACCESS_VIOLATION";
		if(accessType == 0)      InterlockedIncrement(&s_statAvRead);
		else if(accessType == 1) InterlockedIncrement(&s_statAvWrite);
		else if(accessType == 8) InterlockedIncrement(&s_statAvDep);
		else                     InterlockedIncrement(&s_statAvOther);
		dbglog("CRASH[%d]: %s at 0x%08X (zone=%s) accessing 0x%08X%s EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X scope=%s(+0x%X) mod=%s %s",
			crashCount, crashType, eip, crashZone, accessAddr,
			accessAddr < 0x1000 ? " NULLDEREF" : "",
			ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi,
			ctx->Ebp, esp,
			scopeName[0] ? scopeName : "(none)", scopeOff,
			crashModTag, crashInstrTag);
	} else {
		dbglog("CRASH[%d]: code=0x%08X at 0x%08X (zone=%s) EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X scope=%s(+0x%X) mod=%s %s",
			crashCount, code, eip, crashZone,
			ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi,
			ctx->Ebp, esp,
			scopeName[0] ? scopeName : "(none)", scopeOff,
			crashModTag, crashInstrTag);
	}

	// NO per-catch diag_ringDump() here — recovered catches must not
	// re-emit the ring (that was the "bare duplicate" noise).  The fatal
	// path owns the ring dump now.

	// CLASS 5: Removed EIP-skip auto-fixes — they silently mask corruption.
	// Our diagnostics now pinpoint the crash with ring buffer + scope tags.
	// Let real crashes surface so root causes are visible.

	// PHASE 3 (m0271): the old read here — `if(g_allowCrashPassThrough)
	// return EXCEPTION_CONTINUE_SEARCH;` — REMOVED: it was dead (both
	// branches were the identical disposition, main.cpp:3139 vs :644 of the
	// merged file).  The flag's writes in main.cpp (InitialiseGame_hook) are
	// intentionally kept — single-writer-sensitive — with a TODO at that
	// site; the decl stays in debug_handl.h.  Release the Phase 3 receipt
	// claim before the tail (the only path that holds it this far).
	InterlockedExchange(&s_vehReceiptBusy, 0);

	// CLASS 5: Removed blanket EXCEPTION_EXECUTE_HANDLER for 0xC0000005.
	// Previously swallowed all access violations, hiding real bugs.
	// Now we let the crash propagate to the OS for full diagnostics.
	return EXCEPTION_CONTINUE_SEARCH;
}

#ifndef DIAGNOSTICS_DISABLED   // stub macro would otherwise mangle this definition
// One-line end-of-session summary (log dissection).  Cheap and idempotent:
// intended as the last diagnostic line of a run so a log's crash families are
// visible without scanning the whole file.  Safe to call at any time.
void
diag_dumpStats(void)
{
	dbglog("[STATS] session summary: veh_catch=%ld av_read=%ld av_write=%ld av_dep=%ld av_other=%ld guard_fault=%ld",
		(long)s_statVehCatch, (long)s_statAvRead, (long)s_statAvWrite,
		(long)s_statAvDep, (long)s_statAvOther, (long)s_statGuardFault);

	// ADDITIVE receipt (m0273), the line above is byte-stable and stays:
	// veh_catch counts EVERY non-guarded catch, receipts= how many became
	// CRASH[n] lines, suppressed= the gap.  suppressed != 0 means the log has
	// catches it could not show, and the per-module rows NAME who spent the
	// budget (name:granted/suppressed) — so a third-party ASI fault storm is
	// visible in the same session summary instead of reading as a silent
	// gap between veh_catch and the CRASH[n] lines.
	char rows[CRASH_MOD_SLOTS * 64 + 8];
	int p = 0;
	rows[0] = 0;
	for(int i = 0; i < CRASH_MOD_SLOTS; i++){
		const CrashModBudget *b = &s_crashModBudget[i];
		if(b->hash == 0) continue;   // free row
		if(p >= (int)sizeof(rows) - 64) break;   // room for name + 2 counts
		int w = _snprintf(rows + p, (int)sizeof(rows) - p, "%s%s:%ld/%ld",
			p ? " " : "", b->name[0] ? b->name : "(unnamed)",
			(long)b->recv, (long)b->sup);
		if(w < 0) w = 0;
		p += w;
		if(p > (int)sizeof(rows) - 1) p = (int)sizeof(rows) - 1;
		rows[p] = 0;   // _snprintf may not terminate on truncation
	}
	dbglog("[STATS] crash budget: receipts=%ld suppressed=%ld global_cap=%d per_module=%d mods=%s",
		(long)s_statCrashReceipts, (long)s_statCrashSuppressed,
		(int)CRASH_BUDGET_GLOBAL, (int)CRASH_MOD_ALLOWANCE,
		rows[0] ? rows : "(none)");
}
#endif // DIAGNOSTICS_DISABLED

static LONG_PTR s_oldVEH = 0;
volatile LONG g_inGuardedIm2DPass = 0;
const char *g_renderPhase = "";


// ---- SANDBOX CHOKEPOINTS (L1 containment primitives) — m0272 layer split ----
// guardedSetRT / guardedIm2DRender / g_guardDepth moved VERBATIM here from
// render/pipelinecommon.cpp: these are the chokepoints features call, and
// containment must live in the layer that owns the VEH — g_inGuardedIm2DPass
// and g_guardDepth are both read by the GUARD-FAULT receipt above.  Their
// declarations stay in skygfx.h:1638-1651 (outside this lane's scope); the
// contract new features must follow is the block at the top of
// crashhandler.h.
// ============================================================
// Class-proof SEH choke-points — contract documented in skygfx.h.
// POD-only bodies (C2712-safe): no C++ objects with destructors live
// across the __try, so MSVC accepts SEH here and callers (DrawSMAA etc.)
// never need __try in functions that hold C++ locals.
// ============================================================
volatile LONG g_guardDepth = 0;

// Bind a raster as render-target 0. NULL raster fails without SEH; any fault
// inside RwD3D9SetRenderTarget (e.g. CAMERATEXTURE raster with no D3D9
// surface -> null COM vtable read at 0x7F9ECB) is caught, logged with the
// phase tag, and reported as false so the caller fails open.
bool guardedSetRT(RwRaster *ras, const char *phase)
{
	const char *ph = phase ? phase : "?";
	if(!ras){
		if(dbglog_throttle("gsetrt_null"))
			dbglog("[GUARD] guardedSetRT: raster NULL (phase=%s)", ph);
		return false;
	}
	bool ok = true;
	LONG outerGuard = g_inGuardedIm2DPass;
	InterlockedIncrement(&g_guardDepth);
	g_inGuardedIm2DPass = 1;
	__try {
		RwD3D9SetRenderTarget(0, ras);
	} __except(EXCEPTION_EXECUTE_HANDLER){
		dbglog("[GUARD] guardedSetRT: FAULT phase=%s code=0x%08X raster=%p",
			ph, GetExceptionCode(), ras);
		ok = false;
	}
	g_inGuardedIm2DPass = outerGuard;
	InterlockedDecrement(&g_guardDepth);
	return ok;
}

// Fullscreen Im2D draw (quad + override pixel shader). Pre-checks replace the
// common fault classes (null verts/idx, missing camera, camera not begun yet)
// with cheap false returns; anything else is caught by SEH. Same
// g_inGuardedIm2DPass save/restore semantics as the old inline __try blocks,
// so calling this from inside an already-hoisted guarded region is safe.
bool guardedIm2DRender(void *ps, RwInt32 primType, RwIm2DVertex *verts, RwInt32 numVerts,
                       RwImVertexIndex *indices, RwInt32 numIndices, const char *phase)
{
	const char *ph = phase ? phase : "?";

	if(!verts || !indices || numVerts <= 0 || numIndices <= 0){
		if(dbglog_throttle("gim2d_param"))
			dbglog("[GUARD] guardedIm2DRender: bad params (phase=%s verts=%p idx=%p n=%d/%d)",
				ph, verts, indices, (int)numVerts, (int)numIndices);
		return false;
	}
	if(!Scene.camera){
		if(dbglog_throttle("gim2d_nocam"))
			dbglog("[GUARD] guardedIm2DRender: Scene.camera NULL (phase=%s)", ph);
		return false;
	}
	// Begun-camera gate: Scene.camera can exist before the first
	// RwCameraBeginUpdate; Im2D dispatch dereferences the exe-side camera
	// global pCameraBeingUpdated (0xC9BCC0) -> null deref (same Lane G gate as
	// postfx).
	if(pCameraBeingUpdated == NULL){
		if(dbglog_throttle("gim2d_nobegun"))
			dbglog("[GUARD] guardedIm2DRender: camera not begun (phase=%s)", ph);
		return false;
	}

	bool ok = true;
	LONG outerGuard = g_inGuardedIm2DPass;
	InterlockedIncrement(&g_guardDepth);
	g_inGuardedIm2DPass = 1;
	__try {
		overrideIm2dPixelShader = ps;
		RwIm2DRenderIndexedPrimitive((RwPrimitiveType)primType, verts, numVerts, indices, numIndices);
		overrideIm2dPixelShader = nil;
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;
		dbglog("[GUARD] guardedIm2DRender: FAULT phase=%s code=0x%08X ps=%p",
			ph, GetExceptionCode(), ps);
		ok = false;
	}
	g_inGuardedIm2DPass = outerGuard;
	InterlockedDecrement(&g_guardDepth);
	return ok;
}

// ============================================================
// PHASE 2 / FIX A — neutralize the spec-pool free at shutdown (primary fix)
// ============================================================
// Prevents the 0x5D97E6 UAF outright: CCarFXRenderer::Shutdown (0x5D5AD0)
// destroys the dirt-texture loop, then TAIL-CALLS
// CCustomCarEnvMapPipeline::DestroyPipe via `jmp 0x5DA130` at 0x5D5AF7
// (bytes E9 34 46 00 00 = rel32 +0x4634, 0x5D5AFC+0x4634 = 0x5DA130).
// DestroyPipe frees all three Custom*MapPipe pools (spec pool = 4096 slots
// x 8 bytes = the 32 KB block) and nulls their pointers — BEFORE
// rsRWTERMINATE destroys surviving materials (shutdown-ordering UAF).
// NOP-ing exactly that 5-byte tail-jmp skips ONLY the three-pool teardown:
// the dirt-texture destruction loop above it is untouched, and the pools
// stay valid while the materials are destroyed — a clean, order-safe teardown
// of pool objects at process exit (leaking the pipeline/pool memory for the
// last milliseconds of the process, which is exactly what the OS reclaims).
//
// Install site (documented choice): self-installs from the
// CrashHandlerInstaller CRT static (so the fix never depends on the
// DIAGNOSTICS_DISABLED gate, which stubs diag_installVEH), with an
// idempotent second call from diag_installVEH purely to emit the throttled
// receipt once diag_init has opened the log path.  Single attempt per
// process; pattern mismatch => ONE receipt (throttled tag specpool_guard)
// and the patch is SKIPPED — never blind-patched.
//
// INSTALL-LAYER BYTE READS are exempt per HARD RULE (declaration/install
// layer: every read below precedes the only write, and the write is
// byte-verified first).  All reads are SEH-guarded so a foreign game image
// with these targets unmapped skips instead of faulting at load.
static const uint32_t kSpecPoolJumpSite      = 0x5D5AF7; // tail-jmp in CCarFXRenderer::Shutdown -> DestroyPipe
static const uint32_t kSpecPoolJumpExpect    = 0x004634E9; // E9 34 46 00 00 = jmp rel32 +0x4634 -> 0x5DA130
static const uint32_t kSpecPoolDestroyEntry  = 0x5DA130; // CCustomCarEnvMapPipeline::DestroyPipe
static const uint32_t kSpecPoolDestroyExpect  = 0xC02D28A1; // A1 28 2D C0 00 = mov eax,[0xC02D28] (entry sanity)

static volatile LONG s_specGuardState  = 0; // 0 = not attempted, 1 = neutralized, 2 = pattern mismatch (skipped)
static volatile LONG s_specGuardLogged  = 0; // 0 = receipt not yet emitted

// One attempt: verify -> (match ? 5x NOP : skip). Returns 1 = neutralized,
// 2 = mismatch/unreadable (patch skipped).
static int CrashHandler_TrySpecPoolGuard(void)
{
	DWORD jumpDword = 0, jumpByte5 = 0, destroyDword = 0, tailByte = 0;
	__try {
		jumpDword    = *(const volatile DWORD *)(uintptr_t)kSpecPoolJumpSite;
		jumpByte5    = *(const volatile unsigned char *)(uintptr_t)(kSpecPoolJumpSite + 4);
		destroyDword = *(const volatile DWORD *)(uintptr_t)kSpecPoolDestroyEntry;
		tailByte     = *(const volatile unsigned char *)(uintptr_t)(kSpecPoolDestroyEntry + 4);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		return 2;   // foreign image: targets unreadable -> skip, never patch
	}

	// Already neutralized (re-entry or a prior tool): 5x 0x90 in place.
	if(jumpDword == 0x90909090u && jumpByte5 == 0x90)
		return 1;

	// Byte-verify BOTH targets (dispatch table: 0x5D5AF7 == E9 34 46 00 00,
	// 0x5DA130 == A1 28 2D C0 00) before touching anything.
	if(jumpDword != kSpecPoolJumpExpect || jumpByte5 != 0x00 ||
	   destroyDword != kSpecPoolDestroyExpect || tailByte != 0x00)
		return 2;   // pattern mismatch -> receipt + SKIP (never blind-patch)

	DWORD oldProt = 0, junk = 0;
	if(!VirtualProtect((void *)(uintptr_t)kSpecPoolJumpSite, 5,
		PAGE_EXECUTE_READWRITE, &oldProt))
		return 2;   // cannot make it writable -> skip

	int patched = 0;
	__try {
		// Re-verify under write access (bytes could have changed), then NOP.
		if(*(const volatile DWORD *)(uintptr_t)kSpecPoolJumpSite == kSpecPoolJumpExpect &&
		   *(const volatile unsigned char *)(uintptr_t)(kSpecPoolJumpSite + 4) == 0x00)
		{
			volatile unsigned char *p =
				(volatile unsigned char *)(uintptr_t)kSpecPoolJumpSite;
			p[0] = p[1] = p[2] = p[3] = p[4] = 0x90;
			patched = 1;
		}
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		patched = 0;
	}
	FlushInstructionCache(GetCurrentProcess(),
		(const void *)(uintptr_t)kSpecPoolJumpSite, 5);
	VirtualProtect((void *)(uintptr_t)kSpecPoolJumpSite, 5, oldProt, &junk);
	return patched ? 1 : 2;
}

// Idempotent public entry (both call sites documented above).
void CrashHandler_InstallSpecPoolGuard(void)
{
	// Single attempt per process: the first caller (CRT static init) does
	// the work; later calls only reach the receipt below.
	if(InterlockedCompareExchange(&s_specGuardState, -1, 0) == 0)
		InterlockedExchange(&s_specGuardState, CrashHandler_TrySpecPoolGuard());

	// Receipt deferred until diag_init has set the log path (the CRT-time
	// call therefore never logs); the diag_installVEH call emits it once.
	if(!diag_getLogPath()[0])
		return;
	if(InterlockedCompareExchange(&s_specGuardLogged, 1, 0) != 0)
		return;
	long st = (long)s_specGuardState;
	if(dbglog_throttle("specpool_guard")){
		if(st == 1)
			dbglog("[specpool_guard] 0x5D5AF7 tail-jmp neutralized (5x NOP): dirt-texture destruction kept, three spec/env map pools survive shutdown (0x5D97E6 UAF prevented)");
		else
			dbglog("[specpool_guard] PATTERN MISMATCH (state=%ld) at 0x5D5AF7/0x5DA130 — patch SKIPPED, never blind-patched", st);
	}
}

#ifndef DIAGNOSTICS_DISABLED   // stub macro would otherwise mangle this definition
void
diag_installVEH(void)
{
	s_oldVEH = (LONG_PTR)AddVectoredExceptionHandler(1, diag_crashHandler);

	// PHASE 2 / FIX A — idempotent second call site: CRT static init already
	// attempted the patch; now that diag_init opened the log path this emits
	// the throttled specpool_guard receipt (mismatch evidence) exactly once.
	CrashHandler_InstallSpecPoolGuard();
}

void
diag_removeVEH(void)
{
	if(s_oldVEH){
		RemoveVectoredExceptionHandler((HANDLE)s_oldVEH);
		s_oldVEH = 0;
	}
}
#endif // DIAGNOSTICS_DISABLED

// ===========================================================================
// SELF-INSTALLING UNHANDLED-EXCEPTION FILTER + MINIDUMP  (own TU again — this
// section was merged into debug_handl.cpp on 2026-10-26 and restored here m0271)
// ===========================================================================
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
//   [YYYY-MM-DD HH:MM:SS.mmm] [#<frameId>:<code>] [CRASH] code=0x%08X addr=0x%08X
//   [YYYY-MM-DD HH:MM:SS.mmm] [#<frameId>:<code>] [CRASH] code=0x%08X addr=0x%08X op=N(READ|WRITE|DEP|OTHER) fault=0x%08X
// where addr = ContextRecord->Eip (ExceptionAddress), and for AV the two
// ExceptionInformation entries: [0] 0=read/1=write/8=DEP, [1] = fault address
// (the recon ask: the old dialog crash faulted at 0x39D1B71B — now logged).
// The frame prefix matches dbglog_internal's DBGLOG_FRAME_PREFIX_FMT so
// crash lines correlate with the overlay-lane code like every other line.
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

	unsigned fid = DebugFrameId();
	unsigned fcode = DebugFrameCode();
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
			"[%04u-%02u-%02u %02u:%02u:%02u.%03u] " DBGLOG_FRAME_PREFIX_FMT "[CRASH] code=0x%08X addr=0x%08X op=%u(%s) fault=0x%08X\n",
			(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			(unsigned)st.wMilliseconds,
			fid, fcode,
			code, addr, op, opStr, fault);
	}else{
		n = snprintf(line, sizeof(line),
			"[%04u-%02u-%02u %02u:%02u:%02u.%03u] " DBGLOG_FRAME_PREFIX_FMT "[CRASH] code=0x%08X addr=0x%08X\n",
			(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			(unsigned)st.wMilliseconds,
			fid, fcode,
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
// (a2) Deep forensics: faulting module, instruction bytes, stack window
// ------------------------------------------------------------
// FATAL path only.  The VEH receipt already carries mod=/instr= inline; this
// block adds the 24-dword stack window (with module-range / return-address
// annotations) that is too noisy for recovered first-chance catches.  Runs
// inside the caller's own SEH scope; every probe here is bounded and
// fault-tolerant, so a hostile stack cannot take the handler down.
static void
crashAppendForensics(EXCEPTION_POINTERS *ep)
{
	if(!ep || !ep->ContextRecord)
		return;
	CONTEXT *ctx = ep->ContextRecord;
	DWORD eip = ctx->Eip;
	DWORD esp = ctx->Esp;
	DWORD ebp = ctx->Ebp;

	char path[MAX_PATH];
	if(!selfDir(path, sizeof(path)))
		return;
	strncat(path, "skygfx_dbg.log", sizeof(path) - strlen(path) - 1);

	crashSnapshotModules();

	HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
		return;

	char line[256];
	int n;
	DWORD w;

	// (a) faulting EIP -> which module + offset
	char mod[48];
	crashFormatMod(eip, mod, sizeof(mod));
	n = _snprintf(line, sizeof(line),
		"--- FORENSICS ---\n[FORENSIC] eip=0x%08X mod=%s\n", eip, mod);
	if(n > 0) WriteFile(h, line, (DWORD)((n < (int)sizeof(line)) ? n : (int)sizeof(line) - 1), &w, NULL);

	// (c) instruction bytes at EIP (SEH-guarded probe)
	char instr[96];
	crashFormatInstr(eip, instr, sizeof(instr));
	n = _snprintf(line, sizeof(line), "[FORENSIC] %s\n", instr);
	if(n > 0) WriteFile(h, line, (DWORD)((n < (int)sizeof(line)) ? n : (int)sizeof(line) - 1), &w, NULL);

	// (b) raw stack window: 24 dwords from ESP, each annotated with the
	// module+offset it points into (ret? = candidate return address) or the
	// VirtualQuery region kind for non-image values.
	const int dwords = 24;
	n = _snprintf(line, sizeof(line),
		"[FORENSIC] stack esp=0x%08X ebp=0x%08X dwords=%d\n", esp, ebp, dwords);
	if(n > 0) WriteFile(h, line, (DWORD)((n < (int)sizeof(line)) ? n : (int)sizeof(line) - 1), &w, NULL);

	for(int i = 0; i < dwords; i++){
		DWORD addr = esp + (DWORD)(i * 4);
		DWORD val = 0;
		if(crashSafeRead((const void *)(uintptr_t)addr, &val, 4) != 4){
			n = _snprintf(line, sizeof(line),
				"[FORENSIC]   esp+%02X: <unreadable>\n", i * 4);
		} else {
			char cls[64];
			crashClassifyAddr(val, cls, sizeof(cls));
			int isRet = crashFindModule(val, NULL, 0, NULL) ? 1 : 0;
			n = _snprintf(line, sizeof(line),
				"[FORENSIC]   esp+%02X=0x%08X %s%s\n",
				i * 4, val, cls, isRet ? " ret?" : "");
		}
		if(n > 0) WriteFile(h, line, (DWORD)((n < (int)sizeof(line)) ? n : (int)sizeof(line) - 1), &w, NULL);
	}

	n = _snprintf(line, sizeof(line), "--- END FORENSICS ---\n");
	if(n > 0) WriteFile(h, line, (DWORD)((n < (int)sizeof(line)) ? n : (int)sizeof(line) - 1), &w, NULL);

	CloseHandle(h);
}

// ------------------------------------------------------------
// (a3) Flush the log tail BEFORE the minidump write
// ------------------------------------------------------------
// The minidump can be expensive (or stall on a broken disk).  FlushFileBuffers
// on the append handle forces the crash receipt + ring + forensics out of the
// kernel cache first, so the log tail survives even if the dump write hangs.
static void
crashFlushLog(void)
{
	char path[MAX_PATH];
	if(!selfDir(path, sizeof(path)))
		return;
	strncat(path, "skygfx_dbg.log", sizeof(path) - strlen(path) - 1);
	HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE)
		return;
	FlushFileBuffers(h);
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

	// 2.2 (a2) deep forensics — module+offset, instruction bytes, stack
	// window, each probe fault-tolerant.  Own SEH scope so a hostile stack
	// still lets the ring dump + minidump run.
	__try {
		crashAppendForensics(ep);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}

	// 2.5 Ring dump — FATAL path only.  Moved out of the per-catch VEH
	// receipt, where it re-emitted every recent ring line bare on each
	// recovered first-chance catch.  Own SEH scope like the steps above.
	__try {
		diag_ringDump();
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}

	// 2.8 Flush the log tail to disk BEFORE the (potentially slow/stalling)
	// minidump write, so the receipt + ring + forensics survive regardless.
	__try {
		crashFlushLog();
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

	// PHASE 2 / FIX A — primary self-install site (documented): the spec
	// pool guard patches at CRT static init so crash PREVENTION never
	// depends on the DIAGNOSTICS_DISABLED gate (which stubs diag_installVEH).
	// Idempotent; the later diag_installVEH call re-enters only to emit the
	// receipt once the log path is live.
	CrashHandler_InstallSpecPoolGuard();
}

// Namespace-scope static: constructor executes during CRT initialization of
// this DLL — no hook in main.cpp required.
CrashHandlerInstaller s_crashInstaller;

} // anonymous namespace
