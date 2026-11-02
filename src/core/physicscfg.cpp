// physicscfg.cpp - sky_physics.ini service (see physicscfg.h for the full
// contract: file format, key table, backend states, SSOT decision, and the
// later-lane split for menu/main.cpp wiring).
//
// Implementation shape: fixed key table + flat value store, Win32 profile
// API for I/O (forwardplus.cpp:168 self-read pattern), one commented
// first-create writer. No hooks, no Config coupling, no cross-TU globals -
// every consumer goes through the accessors in physicscfg.h.

#include "skygfx.h"
#include "physicscfg.h"

#include <string.h>	// strrchr, strcmpi, strncpy
#include <stdlib.h>	// atoi
#include <stdio.h>	// snprintf, fopen/fprintf (first-create writer)
#include <io.h>		// _open_osfhandle (atomic create-only writer)
#include <fcntl.h>	// _O_BINARY

#define PHYSICSCFG_FILENAME	"sky_physics.ini"
#define PHYSICSCFG_SECTION	"SkyPhysics"
#define PHYSICSCFG_PATH_FALLBACK	".\\" PHYSICSCFG_FILENAME

// ---- key table ----
// One row per INT key. `clamp`: 0 = none, 1 = 0..1 flag, 2 = 0..64 budget.
// Adding a future key (int or float) is one table row + one IDX constant.
enum { PHYSICSCFG_CLAMP_NONE = 0, PHYSICSCFG_CLAMP_FLAG, PHYSICSCFG_CLAMP_BUDGET };

static const struct PhysicsCfgKeyDef
{
	const char *key;
	int defI;
	int clamp;
} s_keyDefs[] =
{
	{ "softbodyNative",        0, PHYSICSCFG_CLAMP_FLAG },
	{ "softbodyBudget",        8, PHYSICSCFG_CLAMP_BUDGET },
	{ "crashFxEnable",         0, PHYSICSCFG_CLAMP_FLAG },
	{ "crashFxTirePops",       0, PHYSICSCFG_CLAMP_FLAG },
	{ "crashFxBurnoutSmoke",   0, PHYSICSCFG_CLAMP_FLAG },
	{ "crashFxWheelDetach",    0, PHYSICSCFG_CLAMP_FLAG },
	{ "crashFxEjectOccupants", 0, PHYSICSCFG_CLAMP_FLAG },
	{ "crashFxBloodDecals",    0, PHYSICSCFG_CLAMP_FLAG },
};
#define PHYSICSCFG_NUM_KEYS ((int)(sizeof(s_keyDefs) / sizeof(s_keyDefs[0])))

// ---- live value store (the single source of truth - physicscfg.h) ----
static int  s_intVals[PHYSICSCFG_NUM_KEYS];
static char s_backend[16];		// normalized "off" | "native" | "bullet"
static bool s_loaded = false;

static int
clampVal(int v, int kind)
{
	switch(kind){
	case PHYSICSCFG_CLAMP_FLAG:
		return v < 0 ? 0 : (v > 1 ? 1 : v);
	case PHYSICSCFG_CLAMP_BUDGET:
		return v < 0 ? 0 : (v > 64 ? 64 : v);
	default:
		return v;
	}
}

static int
keyIndex(const char *key)
{
	if(!key)
		return -1;
	for(int i = 0; i < PHYSICSCFG_NUM_KEYS; i++)
		if(strcmpi(s_keyDefs[i].key, key) == 0)
			return i;
	return -1;
}

// ---- path resolution: sibling of skygfx.ini (module dir), cwd fallback ----
static void
physicsCfgPath(char *out, size_t outSize)
{
	char modulePath[MAX_PATH];
	// Return-value discipline: a truncated GetModuleFileNameA result is not
	// guaranteed NUL-terminated, so strrchr on it could read past the buffer.
	// n < MAX_PATH guarantees modulePath[n] is the terminator.
	if(dllModule){
		DWORD n = GetModuleFileNameA(dllModule, modulePath, MAX_PATH);
		if(n > 0 && n < MAX_PATH){
			char *slash = strrchr(modulePath, '\\');
			if(slash){
				snprintf(out, outSize, "%.*s%s",
					(int)(slash - modulePath + 1), modulePath, PHYSICSCFG_FILENAME);
				return;
			}
		}
	}
	snprintf(out, outSize, "%s", PHYSICSCFG_PATH_FALLBACK);
}

// ---- backend normalization: case-insensitive, unknown -> "off" ----
// Internal form (no lazy-load - PhysicsCfgLoad calls this while loading).
static void
backendSetRaw(const char *backend)
{
	if(backend && strcmpi(backend, "native") == 0)
		strncpy(s_backend, "native", sizeof(s_backend) - 1);
	else if(backend && strcmpi(backend, "bullet") == 0)
		strncpy(s_backend, "bullet", sizeof(s_backend) - 1);
	else
		strncpy(s_backend, "off", sizeof(s_backend) - 1);
	s_backend[sizeof(s_backend) - 1] = '\0';
	// legacy softbodyNative stays mirrored: 1 iff backend == "native"
	s_intVals[keyIndex("softbodyNative")] =
		(strcmp(s_backend, "native") == 0) ? 1 : 0;
}

static int
backendIsNative(void)
{
	return strcmp(s_backend, "native") == 0;
}

// ---- first-create writer: full commented default file (exact bytes,
// ---- CRLF - the same "brand-new INI writes all defaults" contract as
// ---- main.cpp:6906, with the comment block linb round-trips would eat) ----
//
// NEVER overwrites an existing file (INI-preservation fix): the create is
// atomic (CreateFileA CREATE_NEW - no "wb" truncate step), so a file that
// appeared between the load-time existence check and this call - or any
// failed GetFileAttributesA on a file that in fact exists - can only be
// MERGED, never clobbered. On create-loss the caller falls through to
// writeMissingKeys (below), which fills only absent keys.
static void
writeFreshIni(const char *path)
{
	HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(h == INVALID_HANDLE_VALUE){
		// ERROR_FILE_EXISTS (or a transient failure) - never truncate a
		// live file; the caller's writeMissingKeys fills only absent keys.
		if(dbglog_throttle("pcfg_create_exists"))
			dbglog("[PhysicsCfg] %s exists - merge only, no rewrite", path);
		return;
	}
	int fd = _open_osfhandle((intptr_t)h, _O_BINARY);
	if(fd < 0){
		CloseHandle(h);
		if(dbglog_throttle("pcfg_create_fail"))
			dbglog("[PhysicsCfg] cannot create %s", path);
		return;
	}
	FILE *f = _fdopen(fd, "wb");
	if(!f){
		_close(fd);	// also closes the underlying HANDLE
		if(dbglog_throttle("pcfg_create_fail"))
			dbglog("[PhysicsCfg] cannot create %s", path);
		return;
	}
	fputs(
		"; =============================================================\r\n"
		"; SkyGFX Plus - physics / crash-effect configuration\r\n"
		"; Loaded independently of skygfx.ini (src/core/physicscfg.cpp).\r\n"
		"; All features are DEFAULT-OFF; GTA IV mode owns/ enables a subset\r\n"
		"; via the mode manager (see src/core/modeManager.cpp).\r\n"
		"; =============================================================\r\n"
		"\r\n"
		"[SkyPhysics]\r\n"
		"\r\n"
		"; ---- softbody / ragdoll ----\r\n"
		"; softbodyBackend: ped softbody backend.\r\n"
		";   off    = disabled (default)\r\n"
		";   native = built-in RW velocity-integrator softbody\r\n"
		";   bullet = reserved (Bullet port not implemented; acts as off)\r\n"
		"; softbodyNative: LEGACY 0/1 alias of softbodyBackend=native; kept\r\n"
		";   mirrored for old readers (softbodyBackend wins when present).\r\n"
		"; softbodyBudget: max simultaneously active ragdolls (0..64).\r\n"
		"\r\n"
		"; ---- crash effects (crashFxEnable is the master gate) ----\r\n"
		"; Every crashFx* key is a 0/1 flag.\r\n"
		"\r\n", f);
	fprintf(f, "softbodyBackend = %s\r\n", s_backend);
	for(int i = 0; i < PHYSICSCFG_NUM_KEYS; i++)
		fprintf(f, "%s = %d\r\n", s_keyDefs[i].key, s_intVals[i]);
	fclose(f);
	if(dbglog_first("pcfg_created"))
		dbglog("[PhysicsCfg] created %s with defaults", path);
}

// ---- presence probe (audit 2026-10-30): GetPrivateProfileStringA returns
// ---- the default BOTH for an absent key and for a present-but-empty one
// ---- (`softbodyBudget =` / blank value), so a "" default cannot tell them
// ---- apart - and a blank key line is still a USER EDIT that must survive a
// ---- load byte-verbatim. A sentinel default separates the cases: a result
// ---- equal to the sentinel means truly absent. This was the ONLY remaining
// ---- on-load clobber path (writeFreshIni is CREATE_NEW-only and every other
// ---- merge branch already skipped non-empty values).
#define PHYSICSCFG_MISSING "\x01<absent>"
static bool
keyPresent(const char *path, const char *key, char *cur, size_t curSize)
{
	GetPrivateProfileStringA(PHYSICSCFG_SECTION, key,
		PHYSICSCFG_MISSING, cur, (DWORD)curSize, path);
	return strcmp(cur, PHYSICSCFG_MISSING) != 0;
}

// ---- merge writer: fill ONLY keys absent from the file (INI-preservation
// ---- fix - the write-back rule is "defaults for MISSING keys; never
// ---- overwrite existing values"). Runs after loadKeys so the value written
// ---- for a missing key is the RESOLVED one: the seeded default, or the
// ---- legacy softbodyNative-derived backend when that alias carried the
// ---- value (softbodyBackend missing + softbodyNative=1 -> writes "native").
// ---- An existing key is never rewritten - user bytes stay verbatim even
// ---- when empty or when the value normalizes differently in memory (e.g. a
// ---- garbage backend string stays in the file while memory holds "off").
static void
writeMissingKeys(const char *path)
{
	char cur[64], val[64];

	// backend string first (it lives outside the int key table)
	if(!keyPresent(path, "softbodyBackend", cur, sizeof(cur)))
		WritePrivateProfileStringA(PHYSICSCFG_SECTION, "softbodyBackend",
			s_backend, path);

	for(int i = 0; i < PHYSICSCFG_NUM_KEYS; i++){
		if(keyPresent(path, s_keyDefs[i].key, cur, sizeof(cur)))
			continue;	// present (even blank): preserved verbatim, never overwritten
		snprintf(val, sizeof(val), "%d", s_intVals[i]);
		WritePrivateProfileStringA(PHYSICSCFG_SECTION, s_keyDefs[i].key,
			val, path);
	}
}

// ---- load ----
static void
loadKeys(const char *path)
{
	char buf[64];

	// INT keys
	for(int i = 0; i < PHYSICSCFG_NUM_KEYS; i++){
		buf[0] = '\0';
		GetPrivateProfileStringA(PHYSICSCFG_SECTION, s_keyDefs[i].key,
			"", buf, sizeof(buf), path);
		int v = (buf[0] == '\0') ? s_keyDefs[i].defI : atoi(buf);
		s_intVals[i] = clampVal(v, s_keyDefs[i].clamp);
	}

	// backend string; absent key falls back to the legacy int alias
	buf[0] = '\0';
	GetPrivateProfileStringA(PHYSICSCFG_SECTION, "softbodyBackend",
		"", buf, sizeof(buf), path);
	if(buf[0] != '\0')
		backendSetRaw(buf);
	else
		backendSetRaw(s_intVals[keyIndex("softbodyNative")] ? "native" : "off");
}

void
PhysicsCfgLoad(void)
{
	if(s_loaded)
		return;
	s_loaded = true;	// set early: reentrant accessor calls see "loaded"

	// seed defaults so an absent key (or missing file) is always valid
	for(int i = 0; i < PHYSICSCFG_NUM_KEYS; i++)
		s_intVals[i] = s_keyDefs[i].defI;
	strncpy(s_backend, "off", sizeof(s_backend) - 1);
	s_backend[sizeof(s_backend) - 1] = '\0';	// strncpy does not write the last byte

	char path[MAX_PATH];
	physicsCfgPath(path, sizeof(path));

	if(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES){
		// Legacy migration: the old softbody self-read lived in skygfx.ini
		// [SkyGfx] softbodyNative (softbody.cpp:548, now removed). Seed the
		// fresh sky_physics.ini from it so an existing native=1 setup keeps
		// working across the m0971 file split. backendSetRaw keeps the
		// softbodyNative slot mirrored, so both land in the written file.
		size_t n = strlen(path);
		if(n > strlen(PHYSICSCFG_FILENAME)){
			// same dir, "sky_physics.ini" -> "skygfx.ini"
			memcpy(path + n - strlen(PHYSICSCFG_FILENAME),
				"skygfx.ini", strlen("skygfx.ini") + 1);
			if(GetPrivateProfileIntA("SkyGfx", "softbodyNative", 0, path) != 0)
				backendSetRaw("native");
			// restore this module's own path for the create + load below
			physicsCfgPath(path, sizeof(path));
		}
		writeFreshIni(path);
	}

	loadKeys(path);

	// INI-preservation fix: fill only keys absent from the file (defaults or
	// the legacy-alias-derived backend - loadKeys has resolved them by now).
	// Existing keys are never rewritten; a complete file causes zero writes.
	writeMissingKeys(path);

	dbglog("[PhysicsCfg] loaded backend=%s crashFx=%d tirePops=%d "
		"burnoutSmoke=%d wheelDetach=%d eject=%d blood=%d budget=%d",
		s_backend,
		s_intVals[keyIndex("crashFxEnable")],
		s_intVals[keyIndex("crashFxTirePops")],
		s_intVals[keyIndex("crashFxBurnoutSmoke")],
		s_intVals[keyIndex("crashFxWheelDetach")],
		s_intVals[keyIndex("crashFxEjectOccupants")],
		s_intVals[keyIndex("crashFxBloodDecals")],
		s_intVals[keyIndex("softbodyBudget")]);
}

// ---- save: per-key in-place write (preserves comments / unknown keys).
// ---- INI-preservation rule: a key whose on-disk value already equals the
// ---- live store value is never rewritten (user bytes/formats stay verbatim
// ---- when nothing changed); only actually-changed keys are written.
void
PhysicsCfgSave(void)
{
	if(!s_loaded)
		PhysicsCfgLoad();
	char path[MAX_PATH];
	physicsCfgPath(path, sizeof(path));

	char cur[64], buf[64];
	cur[0] = '\0';
	GetPrivateProfileStringA(PHYSICSCFG_SECTION, "softbodyBackend",
		"", cur, sizeof(cur), path);
	if(strcmp(cur, s_backend) != 0)
		WritePrivateProfileStringA(PHYSICSCFG_SECTION, "softbodyBackend",
			s_backend, path);
	for(int i = 0; i < PHYSICSCFG_NUM_KEYS; i++){
		snprintf(buf, sizeof(buf), "%d", s_intVals[i]);
		cur[0] = '\0';
		GetPrivateProfileStringA(PHYSICSCFG_SECTION, s_keyDefs[i].key,
			"", cur, sizeof(cur), path);
		if(strcmp(cur, buf) == 0)
			continue;	// unchanged on disk - never touched
		WritePrivateProfileStringA(PHYSICSCFG_SECTION, s_keyDefs[i].key,
			buf, path);
	}
	if(dbglog_first("pcfg_saved"))
		dbglog("[PhysicsCfg] saved %s (backend=%s)", path, s_backend);
}

bool
PhysicsCfgIsLoaded(void)
{
	return s_loaded;
}

// ---- accessors ----

const char *
PhysicsCfgGetBackend(void)
{
	if(!s_loaded)
		PhysicsCfgLoad();
	return s_backend;
}

void
PhysicsCfgSetBackend(const char *backend)
{
	if(!s_loaded)
		PhysicsCfgLoad();
	if(backend &&
	   (strcmpi(backend, "native") == 0 ||
	    strcmpi(backend, "bullet") == 0 ||
	    strcmpi(backend, "off") == 0))
		backendSetRaw(backend);
	else{
		if(dbglog_throttle("pcfg_backend_bad"))
			dbglog("[PhysicsCfg] unknown backend '%s' -> off",
				backend ? backend : "(null)");
		backendSetRaw("off");
	}
}

int
PhysicsCfgGetInt(const char *key, int def)
{
	if(!s_loaded)
		PhysicsCfgLoad();
	int i = keyIndex(key);
	return (i < 0) ? def : s_intVals[i];
}

float
PhysicsCfgGetFloat(const char *key, float def)
{
	if(!s_loaded)
		PhysicsCfgLoad();
	// No float keys in the table yet (see physicscfg.h key list); the
	// accessor exists so the menu lane can bind float rows the day one
	// lands - extend s_keyDefs with a float column then.
	(void)key;
	return def;
}

void
PhysicsCfgSetInt(const char *key, int value)
{
	if(!s_loaded)
		PhysicsCfgLoad();
	int i = keyIndex(key);
	if(i < 0){
		if(dbglog_throttle("pcfg_set_unknown"))
			dbglog("[PhysicsCfg] SetInt on unknown key '%s' - ignored",
				key ? key : "(null)");
		return;
	}
	s_intVals[i] = clampVal(value, s_keyDefs[i].clamp);
	// keep the backend token mirrored with its legacy alias
	if(strcmpi(key, "softbodyNative") == 0)
		backendSetRaw(s_intVals[i] ? "native" : "off");
}

void
PhysicsCfgSetFloat(const char *key, float value)
{
	if(!s_loaded)
		PhysicsCfgLoad();
	// No float keys yet - same symmetric no-op-with-warn as GetFloat.
	if(dbglog_throttle("pcfg_setfloat_unknown"))
		dbglog("[PhysicsCfg] SetFloat('%s', %.3f): no float keys yet - ignored",
			key ? key : "(null)", (double)value);
}
