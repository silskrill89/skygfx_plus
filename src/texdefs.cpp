// texdefs.cpp — unified texture-definition layer.
//
// Merges three former translation units:
//   * src/extras/texdb.cpp          texdb.txt metadata (TexInfo)
//   * src/render/normalmap.cpp      <diffuse>_n / _norm pairing (NmPair)
//   * src/rw/normalmap.cpp          DK22Pac external normal-map plugin lane
//
// FIND-CALLBACK (0x731FD5+1): the game registers its texture-find callback
// (GetTexture, 0x731720) via the setter at 0x7F34D0; the push immediate at
// 0x731FD5+1 is the registration site. There is now exactly ONE patch there.
// TexDefsFindCB calls the game's GetTexture directly, resolves the TexInfo
// from texdb.txt, then (when pairing is enabled) probes `foo_n` (DK22Pac
// convention) then `foo_norm` via gameGetTexture — no chained callback, no
// call-ordering contract.
//
// PAIRING STORAGE: an RW texture plugin slot ON the diffuse texture stores an
// NmPair by value. The plugin ctor zeroes the slot at texture creation, so a
// recycled RwTexture address can never surface a dead pairing. All four RW
// plugin slots (EDED + PDS atomic, TexInfo* + NmPair texture) are registered
// from TexDefsPluginAttach() during myPluginAttach (0x53D903) — before any
// texture or atomic exists.
//
// D3D TEXTURE EXTRACTION (game RwD3D9SetTexture internals, 0x7FDCD0):
//   ext = raster + *(DWORD*)0xB4E9E0;  ext+0 = IDirect3DTexture9*,
//   ext+4 = surface, ext+8 = valid byte flag. Verified against the exe.
//   The extraction is centralized in TexDefsGetD3DTexture() (read side
//   below) — the one guarded implementation of this formula. Extraction is
//   lazy (first render lookup); the format is then checked via GetLevelDesc so gloss-in-alpha only enables for formats with a real
//   alpha channel (DXT5/RGBA — DXT1's 1-bit alpha would decode all-opaque
//   and mirror-shine every surface).
//
// PLAYER SCOPING (normalMapPlayerOnly, vehicles only):
//   FindPlayerVehicle (0x56E0D0, __cdecl (int,int)) -> CEntity::m_pRWObject
//   (+0x18, gta.h CPlaceable layout) = the player vehicle's RpClump; compare
//   with RpAtomicGetClump(atomic). Cached per render frame.
//
// TXD NAMES: the sibling mechanism needs the name of the current TXD.
// CTxdStore only stores hashes, so addTxdSlot (0x731CCB) and setCurrentTxd
// (0x7319EB) are hooked to keep txdnames[]/currentTxdName in sync.

#include "texdefs.h"

#include <string>
#include <map>
#include <chrono>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>		// atoi — N9 miss-policy master lever
#include <rpnormmap.h>
#include "ini_parser.hpp"	// linb::ini — same parser/file the config lane reads

// ---- RW ATOMIC PLUGIN SLOTS (moved from src/rw/extendedplg.cpp + src/rw/pipeplg.cpp) ----

extern bool forceWindShader;

static RwInt32 ededAtmOffset;

static void* ededConstruct(void* object, RwInt32 offsetInObject, RwInt32)
{
	if (forceWindShader) *RWPLUGINOFFSET(RwUInt32, object, offsetInObject) = DefinedVertexShader::WIND;
	else *RWPLUGINOFFSET(RwUInt32, object, offsetInObject) = DefinedVertexShader::DEFAULT;
	return object;
}

static void* ededCopy(void* dstObject, const void* srcObject, RwInt32 offsetInObject, RwInt32)
{
	*RWPLUGINOFFSET(RwUInt32, dstObject, offsetInObject) = *RWPLUGINOFFSET(RwUInt32, srcObject, offsetInObject);
	return dstObject;
}

static RwStream* EDEDPluginRead(RwStream* stream, RwInt32 binaryLength, void* object, RwInt32 offsetInObject, RwInt32 sizeInObject)
{
	RwStreamRead((RwStream*)stream, (void*)((int)object + offsetInObject), binaryLength);
	return stream;
}

static RwInt32 EDEDPluginGetSize(const void* object, RwInt32 offsetInObject, RwInt32 sizeInObject)
{
	return 4;
}

uint8 TexDefsGetDefinedShader(RpAtomic *atomic)
{
	// CRITICAL: ededAtmOffset == 0 means EDEDPluginAttach() never ran. Reading
	// offset 0 would return RwObject.type (rpATOMIC == 1), which is exactly
	// DefinedVertexShader::WIND — silently routing EVERY atomic through the
	// wind vertex shader (per-vertex noise displacement => sheared/crossed
	// faces, "back of the face drawn on front"). Never read without a real
	// plugin offset.
	if(ededAtmOffset <= 0) return DefinedVertexShader::DEFAULT;
	return *RWPLUGINOFFSET(uint8, atomic, ededAtmOffset);
}

void TexDefsSetDefinedShader(RpAtomic *atomic, uint8 s)
{
	if(ededAtmOffset <= 0) return;
	*RWPLUGINOFFSET(uint8, atomic, ededAtmOffset) = s;
}

RwBool
pdsAtmCB(void *object, RwInt32, RwInt32, RwUInt32 extraData)
{
	RpAtomic *atomic = (RpAtomic*)object;
	if(extraData == PDS_PS2_CustomBuilding_AtmPipeID ||
	   extraData == PDS_PS2_CustomBuildingEnvMap_AtmPipeID ||
	   extraData == PDS_PS2_CustomBuildingUVA_AtmPipeID)
		if(CCustomBuildingPipeline__ObjPipeline){
			atomic->pipeline = CCustomBuildingPipeline__ObjPipeline;
			SetPipelineID(atomic, RSPIPE_PC_CustomBuilding_PipeID);
		}
	if(extraData == PDS_PS2_CustomBuildingDN_AtmPipeID ||
	   extraData == PDS_PS2_CustomBuildingDNEnvMap_AtmPipeID ||
	   extraData == PDS_PS2_CustomBuildingDNUVA_AtmPipeID)
		if(CCustomBuildingDNPipeline__ObjPipeline){
			atomic->pipeline = CCustomBuildingDNPipeline__ObjPipeline;
			SetPipelineID(atomic, RSPIPE_PC_CustomBuildingDN_PipeID);
		}
	return TRUE;
}

static RwInt32 pdsAtmOffset;

// ---------------------------------------------------------------------------
// texdb.txt metadata layer (was src/extras/texdb.cpp)
// ---------------------------------------------------------------------------

static TexInfo faketexinfo;

static RwTexture *detailTextures[100];
static std::map<std::string, TexInfo*> texdb;

static int32 texdbOffset;

static RwTexDictionary *detailTxd;

static void
strtolower(std::string &s)
{
	int i, len;
	len = s.size();
	for(i = 0; i < len; i++)
		s[i] = tolower(s[i]);
}

static TexInfo*
findTexInfo(char *name)
{
	std::string s = name;
	strtolower(s);
	auto info = texdb.find(s);
	if(info != texdb.end())
		return info->second;
	else
		return nil;
}

// Insert (last-wins) with duplicate-key cleanup: overwriting texdb[s]
// directly would leak the previous TexInfo (name/affiliate strdup's +
// object). Safe to free here — affiliateTex links are resolved in the
// link-up pass AFTER every line is read, so no entry can still point at
// the replaced one.
static void
texdbInsert(const std::string &s, TexInfo *info)
{
	auto it = texdb.find(s);
	if(it != texdb.end() && it->second){
		if(it->second->name)
			free(it->second->name);
		if(it->second->affiliate)
			free(it->second->affiliate);
		delete it->second;
	}
	texdb[s] = info;
}

// ---------------------------------------------------------------------------
// NM pairing plugin slot (was src/render/normalmap.cpp)
// ---------------------------------------------------------------------------

struct NmPair {
	RwTexture *normTex;          // <diffuse>_n / _norm texture (same txd chain)
	IDirect3DTexture9 *d3dTex;   // extracted once, lazily (NULL until then)
	int32 hasAlpha;              // format carries a real gloss alpha channel
};

static int32 nm_pluginOffset = 0;

static void *nmPairCtor(void *object, RwInt32 offsetInObject, RwInt32)
{
	memset((RwUInt8 *)object + offsetInObject, 0, sizeof(NmPair));
	return object;
}

// The game's own texture-find callback. Captured from the 0x731FD5+1 push
// immediate in TexDefsInit() before the immediate is rewritten; initialised
// to the known address so the pairing probe can never call through NULL.
static RwTexture *(*gameGetTexture)(char*) = (RwTexture*(*)(char*))0x731720;

static bool
nmPairingEnabled(void)
{
	return nm_pluginOffset > 0 && config && config->normalMapEnable
	       && !gHasExternalNormalMapPlugin;
}

static void
nmProbePairing(RwTexture *diffuse, const char *name)
{
	if(!diffuse || !name)
		return;
	size_t len = strlen(name);
	if(len == 0 || len > 60)
		return;
	// Skip normal maps themselves — a requested "foo_n" must not probe "foo_n_n".
	if(len >= 2 && name[len - 2] == '_' && (name[len - 1] == 'n' || name[len - 1] == 'N'))
		return;
	if(len >= 5 && _stricmp(name + len - 5, "_norm") == 0)
		return;

	NmPair *pair = RWPLUGINOFFSET(NmPair, diffuse, nm_pluginOffset);
	if(pair->normTex)
		return;                                  // already paired

	char normName[80];
	// Probe 1: <name>_n (DK22Pac convention). Probe 2: <name>_norm fallback.
	// Both call the game GetTexture directly: pure current-tdx (+parent
	// chain) lookup, NULL when absent, no recursion.
	snprintf(normName, sizeof(normName), "%s_n", name);
	RwTexture *norm = gameGetTexture(normName);
	if(!norm){
		snprintf(normName, sizeof(normName), "%s_norm", name);
		norm = gameGetTexture(normName);
	}
	if(norm && norm != diffuse){
		pair->normTex = norm;
		// Load-time burst — log the first few, then sample.
		static int nm_pairLog = 0;
		if(nm_pairLog < 8 || (nm_pairLog % 256) == 0)
			dbglog("nm: pair '%s' -> '%s'", name, norm->name ? norm->name : "?");
		nm_pairLog++;
	}
}

// ---------------------------------------------------------------------------
// ONE texture-find callback: TexInfo attach + NM pairing probe.
// ---------------------------------------------------------------------------

static int txdslot;
static char *txdnames[65500];	//[5000]; UG needs a lot
static char currentTxdName[32];

void
TexDefsAssignInfo(RwTexture *tex, TexInfo *info)
{
	if(tex == nil || texdbOffset <= 0)
		return;
	*RWPLUGINOFFSET(TexInfo*, tex, texdbOffset) = info;
}

// ---------------------------------------------------------------------------
// N6 (FAM-T missing textures, 2026-11-08 retest): a FAILED texture lookup is
// the root of both observed symptoms — the bright-red fallback marker (a
// world sprite/corona near the HUD whose texture did not resolve) and the
// milky-white untextured sprite quads (missing texture → FFP white + vertex
// alpha). Two deliverables live here:
//   1. TexDefsNoteMiss — a NAMING receipt so the next gate can see WHICH
//      texture is missing, with the current TXD as context (first 32
//      distinct names one-shot, plus a throttled rate summary).
//   2. TexDefsTryNameVariant — the recoverable root cause this layer can
//      actually fix: name CASE and trailing-EXTENSION mismatches (modded /
//      converted TXDs ship both). Only runs on an ALREADY-FAILED lookup and
//      can only turn NULL into a found texture — never the other way round.
//   3. N8 miss-cache + cross-dict recovery (below): a name that misses is
//      cached — the entry either pins the TXD slot that resolves it or marks
//      it a known miss, so the cross-dict sweep runs at most once per name.
//      The cache is invalidated on TXD push (storeTxdName) so pins/misses
//      retry after a reload.
//
// DIAGNOSIS (2026-11-09, log `[texdefs] miss total=191 distinct=32`): the
// miss class is LOD-parent-chain, NOT case/extension. All 32 distinct names
// are FULL-DETAIL textures looked up while a LOD dictionary is current
// (lod2_las / lod2lae1 / lodhuge_lan2 / lod_countn2 / LODjetty_SFX /
// LOD_dem_SFXRF / lod_boxhses_SFSX / LODtnsfn / grass_pc). 31/32 exist in the
// install (gta3.img full-detail + modloader TXDs) but are unreachable because
// the game's find callback (0x731720) parent-walks through dict->parent (the
// RwTexDictionary plugin at *(uint32*)0xC88018) and that link is NULL/broken
// in the modded (ModLoader/ROSA) install. 1/32 ('fuzzygrass2') is genuinely
// absent. The case/extension retry is provably inert for this class
// (miss-recovered=0; the game's RwTexDictionaryFindNamedTexture (0x7F39F0) is
// already case-insensitive). FIX (in-TU): search the loaded dictionaries
// directly — the game's own CTxdStore pool — via TexDefsDictSweep.
// ---------------------------------------------------------------------------
#define TEXDEFS_MISS_LOG_MAX 32

static char s_missNames[TEXDEFS_MISS_LOG_MAX][32];
static int s_missLogged = 0;		// distinct names stored in s_missNames
static unsigned s_missTotal = 0;	// every failed lookup (any name)

// --- W1 (2026-11-27) DISTINCT-BEYOND-CAP state (additive, tag
// "texdefs_missbeyond" — FNV-1a 1024-bucket 152, collision-checked against
// every tag in-tree by a file-written %TEMP% script; the dispatch's original
// suggested tag "texdefs_misscap" hashed to bucket 377 where it would have
// shared its 2 s throttle window with pbrbuilding_frame = mutual starvation).
// A distinct name arriving when the 32-name `[texdefs] miss #N` cap is full
// is counted by s_missTotal but NAMED NOWHERE — the (b) half of the white-
// tree discriminator ("name past the 32-receipt cap"). Second BOUNDED table
// keeps the unlogged count genuinely DISTINCT up to saturation; past it only
// events are countable (never claimed as distinct). No allocation, no game
// call, read-only against existing miss state.
#define TEXDEFS_MISSBEYOND_MAX 32

static char s_missBeyondNames[TEXDEFS_MISSBEYOND_MAX][32];
static int s_missBeyondDistinct = 0;	// distinct unlogged names tracked above
static unsigned s_missBeyondSeen = 0;	// miss events for unlogged distinct names
static unsigned s_missBeyondOverflow = 0;	// events past BOTH caps (unnamed, distinct-unknown)

static void
TexDefsNoteMiss(const char *name)
{
	s_missTotal++;
	int fresh = 1;
	for(int i = 0; i < s_missLogged; i++){
		if(_stricmp(s_missNames[i], name) == 0){
			fresh = 0;
			break;
		}
	}
	// Captured BEFORE the store below: a name that fills the 32-slot table
	// on THIS call must not be miscounted as beyond-cap.
	const int missCapFull = (s_missLogged >= TEXDEFS_MISS_LOG_MAX);
	if(fresh && s_missLogged < TEXDEFS_MISS_LOG_MAX){
		strncpy(s_missNames[s_missLogged], name, sizeof(s_missNames[0]) - 1);
		s_missNames[s_missLogged][sizeof(s_missNames[0]) - 1] = '\0';
		s_missLogged++;
		dbglog("[texdefs] miss #%d '%s' txd='%s' — texture lookup failed "
		       "(draw falls back to untextured-white sprite or the red "
		       "fallback marker)",
		       s_missLogged, name,
		       currentTxdName[0] ? currentTxdName : "?");
	}
	// W1: fresh name while the naming cap was ALREADY full → it will never
	// appear in a `miss #N` line. Track/count/name it here (throttled).
	if(fresh && missCapFull){
		int b = -1;
		for(int i = 0; i < s_missBeyondDistinct; i++){
			if(_stricmp(s_missBeyondNames[i], name) == 0){
				b = i;
				break;
			}
		}
		if(b < 0){
			if(s_missBeyondDistinct < TEXDEFS_MISSBEYOND_MAX){
				strncpy(s_missBeyondNames[s_missBeyondDistinct], name,
				        sizeof(s_missBeyondNames[0]) - 1);
				s_missBeyondNames[s_missBeyondDistinct][sizeof(s_missBeyondNames[0]) - 1] = '\0';
				s_missBeyondDistinct++;
				dbglog("[texdefs] missbeyond +%d '%s' txd='%s' — distinct name "
				       "past the %d-receipt cap (never named by a `miss #N` line)",
				       s_missBeyondDistinct, name,
				       currentTxdName[0] ? currentTxdName : "?",
				       TEXDEFS_MISS_LOG_MAX);
			}else{
				// Past BOTH caps: event is counted, its name is not kept
				// (bounded over growth — determinism).
				s_missBeyondOverflow++;
			}
		}
		s_missBeyondSeen++;
		if(dbglog_throttle("texdefs_missbeyond"))
			dbglog("[texdefs] missbeyond distinct_unlogged=%d overflow_events=%u "
			       "seen=%u total=%u last='%s' txd='%s'",
			       s_missBeyondDistinct, s_missBeyondOverflow,
			       s_missBeyondSeen, s_missTotal, name,
			       currentTxdName[0] ? currentTxdName : "?");
	}
	// Rate visibility stays alive after the distinct-name cap fills (a
	// streamed draw loop can miss every frame for one texture).
	if(dbglog_throttle("texdefs_miss"))
		dbglog("[texdefs] miss total=%u distinct=%d last='%s' txd='%s'",
		       s_missTotal, s_missLogged, name,
		       currentTxdName[0] ? currentTxdName : "?");
}

// ---------------------------------------------------------------------------
// W1 WHITE-TREE DISCRIMINATOR (2026-11-27, ADDITIVE, tag "texdefs_nulltex" —
// FNV-1a 1024-bucket 671, collision-checked file-written vs all in-tree tags).
// White bare trees are material->texture == NULL -> the consumer's
// gpWhiteTexture (NOT the grey policy sentinel), so the miss/policy receipts
// never name them. This receipt fires where a PLAIN NULL is produced/observed:
//   site=findcb  — TexDefsFindCB's return path actually handed NULL back to
//     the game (only reachable with the miss policy off or a failed sentinel
//     create; stock policy never returns NULL, so a line here is itself a
//     finding). Has the REQUESTED name + the pointer.
//   site=getinfo — TexDefsGetInfo(NULL): a world/vegetation consumer read a
//     material whose texture pointer is NULL (the white-tree class as the
//     pipes observe it; buildingPipe cb's pass material->texture straight
//     in). No name exists on a NULL texture — "(nil)" + the current TXD.
// First TEXDEFS_NULLTEX_LOG_MAX distinct names one-shot, throttled summary
// past that (same discipline as the `miss` receipts; `distinct` beyond the
// cap stays visible as a count). Discriminator reading: a white-tree name
// that appears in `nulltex site=findcb` WAS looked up and passed through
// NULL; one appearing in `miss`/`missbeyond +` was looked up and got the
// policy sentinel; one appearing NOWHERE while missbeyond counts are 0 means
// the find-CB was never called for it (texture absent, plain NULL — case (a)).
// ---------------------------------------------------------------------------
#define TEXDEFS_NULLTEX_LOG_MAX 16

static char s_nullNames[TEXDEFS_NULLTEX_LOG_MAX][32];
static int s_nullLogged = 0;		// distinct names stored in s_nullNames
static unsigned s_nullTotal = 0;	// every NULL-producing/observing call

static void
TexDefsNoteNullTex(const char *name, void *ptr, const char *site)
{
	s_nullTotal++;
	const char *nm = (name && name[0]) ? name : "(nil)";
	// Hot-path bound (per-material per-draw via TexDefsGetInfo): the
	// distinct scan only feeds the store branch, so skip it once the table
	// can no longer accept a name — then this is a counter + throttle check.
	if(s_nullLogged < TEXDEFS_NULLTEX_LOG_MAX){
		int fresh = 1;
		for(int i = 0; i < s_nullLogged; i++){
			if(_stricmp(s_nullNames[i], nm) == 0){
				fresh = 0;
				break;
			}
		}
		if(fresh){
			strncpy(s_nullNames[s_nullLogged], nm, sizeof(s_nullNames[0]) - 1);
			s_nullNames[s_nullLogged][sizeof(s_nullNames[0]) - 1] = '\0';
			s_nullLogged++;
			dbglog("[texdefs] nulltex #%d '%s' ptr=%p site=%s txd='%s' — plain "
			       "NULL (no sentinel; consumer falls back to gpWhiteTexture)",
			       s_nullLogged, nm, ptr, site,
			       currentTxdName[0] ? currentTxdName : "?");
		}
	}
	if(dbglog_throttle("texdefs_nulltex"))
		dbglog("[texdefs] nulltex total=%u distinct=%d last='%s' site=%s",
		       s_nullTotal, s_nullLogged, nm, site);
}

// ---------------------------------------------------------------------------
// N8 cross-dict miss recovery (2026-11-09): the #1 visual defect (giant
// untextured slabs) is LOD models rendering NULL because the game's find
// callback (0x731720) walks dict->parent and that link is NULL in the modded
// install. The textures DO exist in already-loaded TXDs, so on a failed lookup
// we search the game's CTxdStore pool directly (the registry the game keeps)
// via RwTexDictionaryFindNamedTexture (0x7F39F0, case-insensitive). Pool =
// CPool<TxdDef> at *(uint32*)0xC8800C: m_pObjects = *(pool+0), byteMap =
// *(pool+4) ("free" = bit 0x80), m_nSize = *(pool+8); TxdDef is 12 bytes with
// the RwTexDictionary* at +0. A (name, slot) hit is pinned so the sweep runs
// at most once per name; TXD push (TexDefsMissCacheReset) clears pins so a
// better/first-loaded variant can win after a reload. First exact hit wins,
// except a hit whose dict name is a substring of the requesting TXD name is
// preferred (LOD dicts embed their parent's name: lod2lae1 -> lae1). The hit
// dict is always logged for diagnosability. Genuinely-absent names still miss
// (NULL -> consumer white fallback, unchanged).
// ---------------------------------------------------------------------------
// Bounded per-name pin/miss cache. SIZE RAISED 64 -> 192 (2026-11-27): 64
// SATURATED in the 11:42 session — 32 `[texdefs] miss #N` names + 32
// `missbeyond +N` names = 64 distinct misses, and past that TexDefsCacheUpsert
// is a silent no-op, so a RECOVERED name can never pin: every later material
// load of that texture re-enters the `ci < 0` first-miss branch and re-runs the
// WHOLE CTxdStore pool sweep at find time (O(pool) per ask, on the DFF-load
// path, during a streaming burst). 192 = 3x headroom over the observed working
// set; still a fixed static array (no allocation, determinism preserved).
#define TEXDEFS_MISS_CACHE_MAX 192

struct TexDefsMissEntry {
	char name[32];
	int  dictSlot;   // >=0 pinned TXD slot; -1 = known-miss (no dict holds it)
};

static TexDefsMissEntry s_missCache[TEXDEFS_MISS_CACHE_MAX];
static int s_missCacheCount = 0;

// game RwTexDictionaryFindNamedTexture (0x7F39F0): case-insensitive exact name
// match within ONE dictionary (disassembled — same lowercase-fold as 0x731720).
static RwTexture *(*RwTxdFindNamed)(RwTexDictionary*, const char*) =
	(RwTexture *(*)(RwTexDictionary*, const char*))0x7F39F0;

// true when `needle` is a case-insensitive substring of `haystack`.
static bool
TexDefsStrSubCI(const char *haystack, const char *needle)
{
	size_t hl = strlen(haystack), nl = strlen(needle);
	if(nl == 0 || nl > hl)
		return false;
	for(size_t i = 0; i + nl <= hl; i++){
		size_t j = 0;
		for(; j < nl; j++)
			if(tolower((unsigned char)haystack[i + j]) !=
			   tolower((unsigned char)needle[j]))
				break;
		if(j == nl)
			return true;
	}
	return false;
}

// Cache lookup by name (case-insensitive): index or -1.
static int
TexDefsCacheFind(const char *name)
{
	for(int i = 0; i < s_missCacheCount; i++)
		if(_stricmp(s_missCache[i].name, name) == 0)
			return i;
	return -1;
}

// Upsert a cache entry (pinned slot, or -1 for a known miss). Bounded: appends
// until full, then no-op — determinism over growth. Never allocates.
static void
TexDefsCacheUpsert(const char *name, int dictSlot)
{
	int i = TexDefsCacheFind(name);
	if(i >= 0){
		s_missCache[i].dictSlot = dictSlot;
		return;
	}
	if(s_missCacheCount >= TEXDEFS_MISS_CACHE_MAX)
		return;
	strncpy(s_missCache[s_missCacheCount].name, name,
	        sizeof(s_missCache[0].name) - 1);
	s_missCache[s_missCacheCount].name[sizeof(s_missCache[0].name) - 1] = '\0';
	s_missCache[s_missCacheCount].dictSlot = dictSlot;
	s_missCacheCount++;
}

// Read + validate the CTxdStore pool. False when the game hasn't initialised
// it yet (early boot) or a field is implausible.
static bool
TexDefsPoolInfo(uint32 *outObjects, uint8 **outByteMap, int *outSize)
{
	uint32 pool = *(uint32 *)0xC8800C;
	if(pool < 0x10000 || (pool & 3))
		return false;
	uint32 objects = *(uint32 *)pool;
	uint32 byteMap = *(uint32 *)(pool + 4);
	uint32 size = *(uint32 *)(pool + 8);
	if(objects < 0x10000 || (objects & 3))
		return false;
	if(byteMap < 0x10000)
		return false;
	if(size == 0 || size > 65500)
		return false;
	*outObjects = objects;
	*outByteMap = (uint8 *)byteMap;
	*outSize = (int)size;
	return true;
}

// Look up `name` in ONE pool slot's dictionary (re-resolves a pinned slot
// without re-sweeping). NULL when the slot is unused / dict invalid/unloaded
// / name absent from that dict.
static RwTexture *
TexDefsDictResolveSlot(int slot, const char *name, uint32 objects, uint8 *byteMap, int size)
{
	if(slot < 0 || slot >= size)
		return NULL;
	// CPool byteMap: bit 0x80 SET = FREE slot, CLEAR = USED (verified in the
	// game's GetFreeSlot / IsValidTxdSlot). Skip free slots — their TxdDef
	// dict pointer is stale and must never be dereferenced.
	if(byteMap[slot] & 0x80)
		return NULL;
	RwTexDictionary *dict = *(RwTexDictionary **)(objects + slot * 12);
	if(!dict || (uintptr_t)dict < 0x10000 || ((uintptr_t)dict & 3))
		return NULL;
	return RwTxdFindNamed(dict, name);
}

// Cross-dict sweep: search every loaded dictionary for `name`. First exact hit
// wins, EXCEPT a hit whose dict name is a substring of the requesting TXD name
// is preferred (deterministic, no fuzzy). On success fills *outSlot and
// *outDict (the hit TXD name, for the receipt).
static RwTexture *
TexDefsDictSweep(const char *name, int *outSlot, char *outDict,
                 uint32 objects, uint8 *byteMap, int size)
{
	*outSlot = -1;
	outDict[0] = '\0';
	const char *ctx = currentTxdName[0] ? currentTxdName : "";
	RwTexture *best = NULL;
	int bestSlot = -1;
	bool bestSub = false;
	for(int slot = 0; slot < size; slot++){
		RwTexture *tex = TexDefsDictResolveSlot(slot, name, objects, byteMap, size);
		if(!tex)
			continue;
		const char *dname = (slot < 65500 && txdnames[slot]) ? txdnames[slot] : "";
		bool sub = ctx[0] && dname[0] && TexDefsStrSubCI(ctx, dname);
		if(!best || (sub && !bestSub)){
			best = tex;
			bestSlot = slot;
			bestSub = sub;
			if(dname[0]){
				strncpy(outDict, dname, 31);
				outDict[31] = '\0';
			}
			if(sub)
				break;   // substring hit is the best possible — stop early
		}
	}
	*outSlot = bestSlot;
	return best;
}

// Invalidate the cache (a new TXD was pushed/registered). Clears pinned
// fallbacks AND known-miss records so names retry under the new dict set.
// (2026-11-27 review: deliberately NOT narrowed to the known-miss records —
// the wipe is what forces a re-SWEEP for a name already declared "no dict
// holds it", so a pin surviving a registration would suppress exactly the
// recovery this hook exists for. Saturation, not policy, was the defect; the
// fix is the raised TEXDEFS_MISS_CACHE_MAX above.)
static void
TexDefsMissCacheReset(void)
{
	s_missCacheCount = 0;
}

// ---------------------------------------------------------------------------
// N9 FAIL-CLOSED MISS POLICY (2026-11-09): the #1 visual defect family — giant
// untextured slabs / black bars / the red-yellow world-anchored marker quad —
// is the fallback DRAW a missing texture produces. TexDefsFindCB now binds a
// 1x1 SENTINEL texture instead of returning NULL for genuinely-absent names
// (post pin/variant/cross-dict sweep), classified by the CURRENT TXD:
//   world  (currentTxdName unresolved, or "lod*" ci) -> 1x1 OPAQUE dim grey
//          (0x606060, alpha FF, name "skygfx_missgrey"): geometry KEEPS
//          drawing — never hole the world for a data-side gap; alpha FF also
//          survives the building pipes' GREATEREQUAL@1 alpha test.
//   sprite (everything else) -> 1x1 FULLY TRANSPARENT magenta (FF 00 FF 00,
//          name "skygfx_missclear"): alpha-blended / alpha-tested decoration
//          (screen sprites, particles, the marker quad) renders invisible; an
//          unblended consumer shows magenta — loud, bounded, diagnosable.
// Both magenta R/B symmetry and the grey value are deliberate: they read
// identically under either BGRA/RGBA byte order of an RW 8888 raster.
// Sentinels are created lazily ONCE inside the find CB (RW is up whenever it
// runs), process-lifetime, never freed; RwTexture::name is an inline char[32]
// (rwcore.h, SA layout) filled directly so dumps identify them. The CB
// returns a sentinel BEFORE the TexInfo/NmPair attach block — no metadata, no
// pairing probe on it. Policy is keyed on LOOKUP-FAILURE ONLY: every
// legitimate gpWhiteTexture resolve (NULL-extraction consumers,
// pipeSetTexture NULL->white, the NULL-material fallback binds) is untouched.
// Creation failure degrades to the legacy NULL return (consumer white
// fallback). Full table + receipt contract: texdefs.h ("N9").
// ---------------------------------------------------------------------------
static bool s_texDefsMissPolicyOn = true;

void
TexDefsMissPolicyEnable(bool on)
{
	s_texDefsMissPolicyOn = on;
}

bool
TexDefsMissPolicyIsEnabled(void)
{
	return s_texDefsMissPolicyOn;
}

enum TexDefsMissClass {
	TEXDEFS_MISS_WORLD = 0,	// LOD-parent-chain geometry (lod* dict / unresolved)
	TEXDEFS_MISS_SPRITE	// decoration, particles, markers (any non-LOD dict)
};

static TexDefsMissClass
TexDefsMissClassify(void)
{
	// Unresolved dict name: unknown context — classify WORLD (conservative:
	// keeps geometry drawing, because a hole is worse than a dim slab, and
	// every such miss prints class=world in the policy receipt so it can be
	// retuned).
	if(!currentTxdName[0])
		return TEXDEFS_MISS_WORLD;
	// Every broken dict in the 32-name log carries the "lod" prefix
	// (lod2_las, lod2lae1, lodhuge_lan2, lod_countn2, LODjetty_SFX,
	// LOD_dem_SFXRF, lod_boxhses_SFSX, LODtnsfn). grass_pc — the one
	// non-"lod" broken dict — classifies sprite, where foliage/grass cards
	// skip cleanly.
	if(_strnicmp(currentTxdName, "lod", 3) == 0)
		return TEXDEFS_MISS_WORLD;
	return TEXDEFS_MISS_SPRITE;
}

static RwTexture *s_missSentinelGrey = NULL;	// world class
static RwTexture *s_missSentinelClear = NULL;	// sprite class

static RwTexture*
TexDefsMakeSentinel(const char *name, RwUInt8 r, RwUInt8 g, RwUInt8 b, RwUInt8 a)
{
	RwRaster *ras = RwRasterCreate(1, 1, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
	if(!ras)
		return NULL;
	// Level 0, lockFlags 1 (write) — same pattern as postfx.cpp
	// GetPipeChainEdgeFadeRaster. A 1x1 8888 raster holds one 4-byte texel.
	RwUInt8 *px = RwRasterLock(ras, 0, 1);
	if(px){
		px[0] = r;
		px[1] = g;
		px[2] = b;
		px[3] = a;
		RwRasterUnlock(ras);
	}
	RwTexture *tex = RwTextureCreate(ras);
	if(!tex){
		RwRasterDestroy(ras);
		return NULL;
	}
	// Fill the inline name array directly (rwcore.h RwTexture layout): game
	// consumers read tex->name[0] unguarded, and the name makes the sentinel
	// identifiable as policy output in log lines and dumps.
	strncpy(tex->name, name, sizeof(tex->name) - 1);
	tex->name[sizeof(tex->name) - 1] = '\0';
	return tex;
}

RwTexture*
TexDefsMissSentinelWorld(void)
{
	if(!s_missSentinelGrey)
		s_missSentinelGrey = TexDefsMakeSentinel("skygfx_missgrey", 0x60, 0x60, 0x60, 0xFF);
	return s_missSentinelGrey;
}

RwTexture*
TexDefsMissSentinelSprite(void)
{
	if(!s_missSentinelClear)
		s_missSentinelClear = TexDefsMakeSentinel("skygfx_missclear", 0xFF, 0x00, 0xFF, 0x00);
	return s_missSentinelClear;
}

// ---------------------------------------------------------------------------
// PER-NAME SENTINEL CLONES (2026-11-16, root-cause fix for the untextured
// world). The shared class sentinel overwrites its inline name with
// "skygfx_missgrey"/"skygfx_missclear", destroying the ORIGINAL requested
// name — once a material holds it, no later retry has anything to look up,
// and the material is poisoned forever (the game never re-requests). On a
// policy miss the FindCB therefore hands back a per-name CLONE that SHARES
// the class singleton's 1x1 raster (so TexDefsMissTextureKind's raster
// identity recognizes it) but carries the requested name. Clones live in no
// dictionary, so nothing destroys them: process-lifetime, <= one per
// distinct missed name.
// ---------------------------------------------------------------------------
static std::map<std::string, RwTexture*> s_namedSentinels;

static RwTexture *
TexDefsNamedSentinel(RwTexture *proto, const char *name)
{
	if(!proto || !name || !name[0])
		return proto;	// unnamed lookup — nothing a later retry could use
	std::map<std::string, RwTexture*>::iterator it = s_namedSentinels.find(name);
	if(it != s_namedSentinels.end())
		return it->second;
	RwTexture *clone = RwTextureCreate(proto->raster);	// shares the 1x1 raster
	if(!clone)
		return proto;	// allocation failure degrades to the shared sentinel
	strncpy(clone->name, name, sizeof(clone->name) - 1);
	clone->name[sizeof(clone->name) - 1] = '\0';
	s_namedSentinels[name] = clone;
	return clone;
}

// ---------------------------------------------------------------------------
// N9 CONSUMER-HANDOFF QUERY (2026-11-09): identity test against the two
// sentinels — the ONLY safe way for a consumer to tell "this pointer is
// POLICY OUTPUT (the texture lookup failed)" from "this material
// legitimately has NO texture" (NULL) or "this is a real texture".
//
// Why not `tex == TexDefsMissSentinelWorld()` in the consumer: those
// accessors CREATE the sentinel raster on demand, and a consumer on the draw
// path must never allocate. This query only COMPARES the already-created
// instances, so it is free and side-effect free.
//
// The discrimination the consumers rely on:
//   NULL  -> legitimate "no texture"    -> the consumer keeps gpWhiteTexture
//   WORLD -> action=sentinel (dim grey) -> draw / bind the grey 1x1
//   SPRITE-> action=skip (transparent)  -> skip the draw
// A pointer that is NONE is a real texture and must be used AS IS.
// ---------------------------------------------------------------------------
int
TexDefsMissTextureKind(const RwTexture *tex)
{
	if(!tex || !tex->raster)
		return TEXDEFS_MISS_TEX_NONE;
	// RASTER identity (2026-11-16): the per-name clones share the class
	// singleton's 1x1 raster, so pointer-identity on the texture struct no
	// longer covers them. Comparing the raster recognizes the singleton AND
	// every clone; real textures can never share our privately-created 1x1
	// rasters. Still compare-only — never creates.
	if(s_missSentinelGrey && tex->raster == s_missSentinelGrey->raster)
		return TEXDEFS_MISS_TEX_WORLD;
	if(s_missSentinelClear && tex->raster == s_missSentinelClear->raster)
		return TEXDEFS_MISS_TEX_SPRITE;
	return TEXDEFS_MISS_TEX_NONE;
}

// Throttled policy receipt, texdefs.h "N9". Its own tag keeps the quota
// independent of the naming/summary miss receipts.
static void
TexDefsNoteMissPolicy(const char *name, TexDefsMissClass cls, RwTexture *sent)
{
	if(!dbglog_throttle("texdefs_misspolicy"))
		return;
	if(sent){
		dbglog("[texdefs] miss-policy '%s' class=%s action=%s txd='%s' total=%u",
		       name,
		       cls == TEXDEFS_MISS_WORLD ? "world" : "sprite",
		       cls == TEXDEFS_MISS_WORLD ? "sentinel" : "skip",
		       currentTxdName[0] ? currentTxdName : "?",
		       s_missTotal);
	}else{
		dbglog("[texdefs] miss-policy '%s' INACTIVE (sentinel create failed "
		       "— legacy white fallback)", name);
	}
}

static RwTexture *
TexDefsTryNameVariant(const char *name)
{
	size_t len = strlen(name);
	if(len == 0 || len >= 60)
		return NULL;

	char lower[64];
	char stripped[64];
	char lowerStripped[64];
	const char *cand[3];
	int nCand = 0;

	// (a) lowercased name — case-mismatched lookups.
	for(size_t i = 0; i <= len; i++)
		lower[i] = (char)tolower((unsigned char)name[i]);
	if(strcmp(lower, name) != 0)
		cand[nCand++] = lower;

	// (b) trailing-extension stripped ("name.txd"/"name.dff"-style requests
	// leaking in from converted content) and (c) lowercased + stripped.
	const char *dot = strrchr(name, '.');
	size_t extLen = dot ? len - (size_t)(dot - name) : 0;
	size_t keep = (dot && dot != name && extLen >= 2 && extLen <= 5)
	              ? (size_t)(dot - name) : 0;
	if(keep){
		memcpy(stripped, name, keep);
		stripped[keep] = '\0';
		if(strcmp(stripped, name) != 0)
			cand[nCand++] = stripped;
		memcpy(lowerStripped, lower, keep);
		lowerStripped[keep] = '\0';
		if(strcmp(lowerStripped, name) != 0 &&
		   strcmp(lowerStripped, stripped) != 0)
			cand[nCand++] = lowerStripped;
	}

	for(int i = 0; i < nCand; i++){
		RwTexture *tex = gameGetTexture((char *)cand[i]);
		if(tex)
			return tex;
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// SPRITE -> WORLD REMAP, NAME-PRESERVING (2026-11-16). World consumers
// (building/vehicle/skin/default cb) must never bind the transparent-magenta
// sprite sentinel; they used to rebind the SHARED grey singleton, which drops
// the requested name and makes the remap unresolvable. Remap through a
// per-name world clone instead; real/world textures pass through as-is.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// NULL-DRAW RECOVERY (2026-11-27, A2 partial). When the find-CB returns NULL
// on a miss (A1 doctrine: game keeps NULL for streaming), material->texture
// is NULL and the name is gone from the draw path. Walk the missed-name log
// (s_missNames) once per call and return the first name that now resolves
// (parent TXD loaded since the miss, or a late registration). Throttled
// internally via TexDefsResolveAtDraw's 1 s retry; returns NULL if nothing
// resolves yet (caller applies the grey sentinel for visibility).
// ---------------------------------------------------------------------------
RwTexture *
TexDefsResolveNullDraw(void)
{
	// Walk the missed-name log (s_missNames) and return the first name that
	// now resolves via the CTxdStore pool sweep. Throttled per-name by the
	// existing 1 s retry so a miss-heavy frame cannot spam gameGetTexture.
	// Returns NULL if nothing resolves (caller applies the grey sentinel).
	if(s_missLogged == 0)
		return NULL;
	uint32 objects; uint8 *byteMap; int size;
	if(!TexDefsPoolInfo(&objects, &byteMap, &size))
		return NULL;
	for(int i = 0; i < s_missLogged; i++){
		int hitSlot = -1; char hitDict[32];
		RwTexture *real = TexDefsDictSweep(s_missNames[i], &hitSlot, hitDict,
		                                   objects, byteMap, size);
		if(real && TexDefsMissTextureKind(real) == TEXDEFS_MISS_TEX_NONE)
			return real;
	}
	return NULL;
}

RwTexture*
TexDefsRemapToWorld(RwTexture *tex)
{
	if(!tex)
		return tex;
	if(TexDefsMissTextureKind(tex) != TEXDEFS_MISS_TEX_SPRITE)
		return tex;	// real texture or already the world class
	return TexDefsNamedSentinel(TexDefsMissSentinelWorld(), tex->name);
}

// ---------------------------------------------------------------------------
// DRAW-TIME RESOLUTION (2026-11-16, THE root-cause fix).
//
// Why this exists: the game's find (0x731720) parent-walks dict->parent and
// that link is broken in this ModLoader/ROSA install, so full-detail textures
// looked up while a LOD dictionary is current MISS. TexDefsDictSweep recovers
// them only if the target TXD is ALREADY loaded — but streaming order loads
// LOD DFFs first, so the miss happens before the parent TXD arrives, the
// material binds the grey sentinel ONCE, and the game never re-asks: every
// later frame draws the building as grey x ambient (pale-blue slabs by day,
// black pyramids at dawn). Screenshots 2026-11-16 prove the class.
//
// This retries the lookup on the DRAW path (long after load, when the parent
// TXDs are in memory) for textures that are policy sentinels carrying their
// original name. Per-name state: a validated owning-dict pin (slot byteMap
// re-check = dict alive => texture alive, O(1) hot path) and a 1 s retry
// throttle for names still missing. Real textures take the early-out and
// never touch the map after their first sight. Receipts throttled.
// ---------------------------------------------------------------------------
struct TexDefsResolveState {
	RwTexture *tex;		// last real hit (NULL = never resolved)
	int dictSlot;		// owning CTxdStore slot when pinnable, else -1
	unsigned long lastTry;	// GetTickCount-style ms of the last full retry
};
static std::map<std::string, TexDefsResolveState> s_resolveState;

static unsigned long
TexDefsNowMs(void)
{
	return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

RwTexture*
TexDefsResolveAtDraw(RwTexture *tex)
{
	if(!tex)
		return tex;
	// ONE-SHOT (2026-11-16): proves this function is reached at all and with
	// what class — static local, no throttle key involved.
	static bool s_resolveFirst = false;
	if(!s_resolveFirst){
		s_resolveFirst = true;
		dbglog("[texdefs] resolve first-call tex=%p kind=%d name='%s'",
		       (void*)tex, TexDefsMissTextureKind(tex),
		       tex->name[0] ? tex->name : "");
	}
	if(TexDefsMissTextureKind(tex) == TEXDEFS_MISS_TEX_NONE)
		return tex;	// real texture (or NULL) — hot path, no map traffic
	if(!tex->name[0])
		return tex;	// unnamed shared sentinel — nothing to retry by
	const char *name = tex->name;

	TexDefsResolveState &st = s_resolveState[name];	// inserts once per name

	// Hot path: a pinned owning dict still allocated => cached texture alive
	// (RW destroys a dictionary's textures with the dictionary).
	if(st.tex && st.dictSlot >= 0){
		uint32 objects; uint8 *byteMap; int size;
		if(TexDefsPoolInfo(&objects, &byteMap, &size) &&
		   st.dictSlot < size && (byteMap[st.dictSlot] & 0x80) == 0)
			return st.tex;
		st.tex = NULL;		// owning dict unloaded — fall through to retry
		st.dictSlot = -1;
	}else if(st.tex){
		return st.tex;		// unpinned hit — vanilla-equivalent trust
	}

	// Rate-limited full retry: 1 s per name (first sight runs immediately).
	unsigned long now = TexDefsNowMs();
	if(st.lastTry && now - st.lastTry < 1000)
		return tex;		// keep the sentinel for this frame
	st.lastTry = now;

	char nameBuf[32];	// gameGetTexture takes char* (non-const signature)
	strncpy(nameBuf, name, sizeof(nameBuf) - 1);
	nameBuf[sizeof(nameBuf) - 1] = '\0';
	RwTexture *real = gameGetTexture(nameBuf);	// vanilla find — may now succeed
	if(!real)
		real = TexDefsTryNameVariant(name);	// case/extension mismatch
	// 2026-11-17 — THE gap behind the `resolve ... -> still-miss` 0/17 log
	// (E:\games\gtasa_skygfx_plus\skygfx_dbg.log 02:17-02:20). The CTxdStore
	// sweep below used to be reachable ONLY after a vanilla/variant HIT (to
	// PIN a texture we had already found) — the one case where a sweep is
	// useless. A still-missing name therefore never swept the pool here: the
	// vanilla find's dict->parent walk is broken in this install (0x731720
	// walks *(uint32*)0xC88018 + dict, written only once at child-TXD load)
	// and TexDefsTryNameVariant is provably inert for this class
	// (case/extension only), so every 1 s retry ended in `still-miss` no
	// matter which TXDs had loaded since. Sweep the pool as a RECOVERY
	// source first; the pin-only sweep further down then only has to handle
	// the (rare) vanilla/variant hit. Same receipts, same throttle.
	char hitDict[32] = {0};
	int hitSlot = -1;
	uint32 objects; uint8 *byteMap; int size;
	bool poolOk = TexDefsPoolInfo(&objects, &byteMap, &size);
	if(!real && poolOk)
		real = TexDefsDictSweep(name, &hitSlot, hitDict, objects, byteMap, size);
	if(real && TexDefsMissTextureKind(real) == TEXDEFS_MISS_TEX_NONE){
		// Pin the owning slot so the hot path can validate cheaply (the
		// sweep is authoritative for slots; a vanilla hit is in a loaded
		// dict, so the sweep finds it too).
		if(hitSlot < 0 && poolOk){
			RwTexture *swept = TexDefsDictSweep(name, &hitSlot, hitDict,
			                                   objects, byteMap, size);
			if(swept && TexDefsMissTextureKind(swept) == TEXDEFS_MISS_TEX_NONE)
				real = swept;
			else
				hitSlot = -1;
		}
		st.dictSlot = (hitSlot >= 0) ? hitSlot : -1;
		st.tex = real;
		// THROTTLE KEY (2026-11-27): "texdefs_resolve" -> "texdefs_drawres".
		// RECEIPT BYTES UNCHANGED — only the dbglog_throttle key moved. The
		// old key hashed (FNV-1a & 1023) to bucket 577, SHARED with
		// postfx.cpp's "pf_ibl" (a per-frame IBL gate) which claims the
		// bucket first every window: across the entire on-disk log history
		// exactly ONE `[texdefs] resolve` line ever printed (a single
		// `still-miss`, 2026-11-26 23:41:38) and ZERO `-> HIT` lines —
		// recovery was effectively unobservable while it was running.
		// Bucket 489 is unique across every in-tree throttle tag (checked
		// file-written with the %TEMP% fnv_check_tags script).
		if(dbglog_throttle("texdefs_drawres"))
			dbglog("[texdefs] resolve '%s' -> HIT (dict='%s' slot=%d) — world texture restored",
			       name, hitDict[0] ? hitDict : "?", st.dictSlot);
		return real;
	}
	if(dbglog_throttle("texdefs_drawres"))
		dbglog("[texdefs] resolve '%s' -> still-miss (keeping policy sentinel)", name);
	return tex;
}

// ---------------------------------------------------------------------------
// DEFERRED RETRY AT TXD LIFECYCLE (2026-11-17, addTxdSlot + setCurrentTxd).
//
// DIAGNOSIS this block is built on (grounded in the exe + this TU):
//   1. TexDefsResolveAtDraw could not recover a still-missing name because
//      its pool sweep sat INSIDE the hit branch (see the note above) — the
//      only path where a sweep cannot help. Hence 0 HIT / 17 still-miss.
//   2. The game's find (0x731720) = RwTexDictionaryGetCurrent ->
//      loop { RwTexDictionaryFindNamedTexture(dict,name);
//            dict = *(parentPluginOffset + dict) } with the parent plugin
//      OFFSET held at *(uint32*)0xC88018. That link is written by exactly
//      ONE game function (0x731D50 — 2 callers, both inside the child
//      TXD's own LoadTxd) as childDict[parentOffset] =
//      parentTxdDef->m_pTxd AT THAT INSTANT: an unloaded parent stores
//      NULL and is never re-linked, and the LoadTxd variant at 0x731930
//      (taken when byte 0x8E4A58 is set) never links at all. So the vanilla
//      find cannot see cross-dict names, ever.
//   3. TXD SLOT REGISTRATION IS NOT A LOAD EVENT. RE of addTxdSlot
//      (0x731C80): `mov dword ptr [esi], 0` (0x731C93) ZEROS
//      TxdDef.m_pTxd before our 0x731CCB hook runs, and none of the three
//      LoadTxd entry points (0x731930 / 0x731DD0 / 0x731E40) calls
//      AddTxdSlot — slots are registered up front (init/level/IMG paths),
//      so probing the just-added slot finds nothing BY CONSTRUCTION. The
//      live-dict event is setCurrentTxd (0x7319C0), which the game runs
//      right before reading a DFF with that dictionary current, and the
//      draw-time sweep in TexDefsResolveAtDraw is the reliable catch for
//      dicts that become current for no other reason.
// The retry below probes, from BOTH hooks: (a) the slot's own dictionary
// when it is alive (real work at setCurrentTxd, no-op at addTxdSlot), and
// (b) throttled, the WHOLE CTxdStore pool for every name still awaiting
// recovery — find-time distinct misses (s_missNames) plus draw-time
// sentinels that never resolved (s_resolveState with tex == NULL). A hit
// pins the slot in the miss cache (the next find-time ask resolves through
// the pin and never hands out another sentinel clone) and fills the
// resolve state (the next draw binds the REAL texture).
// ADDITIVE receipt only, throttled key "texdefs_deferretry":
//   [texdefs] defer-retry '<name>' -> HIT via dict '<dict>'
// Residual (documented): a TXD the game NEVER requests cannot be found from
// here — this layer has no name->TXD map and no safe load-forcing API, so
// such names stay on the policy sentinel until a parent-link repair or an
// explicit load-force phase.
// W1 (2026-11-27) PHASE DECISION — load-force NOT shipped, deliberately:
//   * The only in-repo TXD-load API is RwTexDictionaryStreamRead over a
//     FILE-path stream (TexDefsLoadDatabase's models\*.txd, neopipe's
//     neo.txd) — both run at boot before CGame::Initialise, read by path
//     from getpath(), not by TXD NAME from the IMG. There is no
//     non-reentrant "load TXD <name>" wrapper anywhere in-tree (grepped:
//     no CTxdStore::LoadTxd / CStreaming::Request binding exists), and
//     forcing one from inside these hooks risks re-entering the very
//     addTxdSlot/setCurrentTxd path we are running in (streaming pumps).
//   * Decisive: without a name->TXD map a preload has no target — it would
//     have to guess which dictionary holds a missing texture.
//   Shipped instead (the cheaper half, both additive + bounded):
//   (1) pending-name retry extended to the beyond-cap names below
//       (TexDefsDeferRetry set now includes s_missBeyondNames), so a hit
//       IS applied when the owning dict eventually loads;
//   (2) the distinct-beyond-cap histogram receipt `[texdefs] missbeyond`
//       in TexDefsNoteMiss (count of unlogged distinct names — the
//       visibility half).
// ---------------------------------------------------------------------------
static unsigned long s_deferLastSweep = 0;
#define TEXDEFS_DEFER_SWEEP_MS 1000

// Accept one deferred recovery: pin it in the miss cache, fill the resolve
// state, emit the throttled receipt. Never accepts policy output as a hit.
static void
TexDefsDeferAccept(const char *name, RwTexture *tex, int dictSlot, const char *dictName)
{
	if(!name || !name[0] || !tex)
		return;
	if(TexDefsMissTextureKind(tex) != TEXDEFS_MISS_TEX_NONE)
		return;		// a sentinel is not a recovery
	auto it = s_resolveState.find(name);
	if(it == s_resolveState.end()){
		// 2026-11-27: a lifecycle HIT routinely lands BEFORE the first draw
		// of that material (the owning TXD loads during streaming, the mesh
		// becomes visible later). The old code only wrote the state when the
		// name was ALREADY there, so the HIT was carried by the find-time
		// pin alone and the next draw still paid a full retry before it could
		// use it. Pre-create the entry so the very next draw takes the
		// pin/valid hot path. Bounded by construction: one entry per distinct
		// RECOVERED name (a name that never recovers never reaches here).
		TexDefsResolveState fresh;
		fresh.tex = NULL;
		fresh.dictSlot = -1;
		fresh.lastTry = 0;
		it = s_resolveState.insert(std::make_pair(std::string(name), fresh)).first;
	}
	it->second.tex = tex;		// next draw binds the real texture
	it->second.dictSlot = dictSlot;
	TexDefsCacheUpsert(name, dictSlot);	// next find-time ask hits the pin
	if(dbglog_throttle("texdefs_deferretry"))
		dbglog("[texdefs] defer-retry '%s' -> HIT via dict '%s'",
		       name, (dictName && dictName[0]) ? dictName : "?");
}

// Deferred retry for TXD lifecycle events (called from storeTxdName /
// setCurrentTxdName). Read-only against the game: pool reads +
// RwTexDictionaryFindNamedTexture + dbglog — no game call that could
// re-enter this hook.
static void
TexDefsDeferRetry(int slot)
{
	// 2026-11-27: the guard used to test only `s_missLogged` + the resolve
	// state, so a session whose 32-name `[texdefs] miss #N` cap never filled
	// but whose beyond-cap table DID (the `missbeyond +N` names) would early
	// out and never probe them.
	if(s_missLogged <= 0 && s_missBeyondDistinct <= 0 && s_resolveState.empty())
		return;			// nothing pending (boot / fully recovered)
	uint32 objects; uint8 *byteMap; int size;
	if(!TexDefsPoolInfo(&objects, &byteMap, &size))
		return;			// CTxdStore pool not initialised yet

	// Full-pool leg — throttled (TXD events arrive in bursts; scan the pool
	// at most once per TEXDEFS_DEFER_SWEEP_MS).
	bool fullSweep = true;
	unsigned long now = TexDefsNowMs();
	if(s_deferLastSweep && now - s_deferLastSweep < TEXDEFS_DEFER_SWEEP_MS)
		fullSweep = false;
	else
		s_deferLastSweep = now;

	// The slot's own dictionary: assigned at LoadTxd (m_pTxd written at
	// 0x731E13 / 0x731E72), NULLed at Remove (0x731F0F) and — per the RE
	// note above — already NULL at addTxdSlot, so this leg is a no-op there
	// by construction and the live one at setCurrentTxd.
	RwTexDictionary *slotDict = NULL;
	const char *slotDictName = "";
	if(slot >= 0 && slot < size && (byteMap[slot] & 0x80) == 0){
		RwTexDictionary *d = *(RwTexDictionary **)(objects + slot * 12);
		if(d && (uintptr_t)d >= 0x10000 && ((uintptr_t)d & 3) == 0){
			slotDict = d;
			if(slot < 65500 && txdnames[slot])
				slotDictName = txdnames[slot];
		}
	}
	if(!slotDict && !fullSweep)
		return;			// throttled out — nothing to probe this event

	// Pending name set: find-time distinct misses + unresolved draw-time
	// sentinels, deduped, bounded by the array (no allocation beyond it).
	// W1 (2026-11-27): plus the distinct-beyond-cap names — a name past the
	// 32-receipt cap never enters s_missNames, so it was invisible to this
	// retry set even though it holds a pending policy sentinel. Extending
	// the set is the shipped half of the never-load residual: when the
	// owning dict eventually loads, TexDefsDeferAccept now applies the hit
	// for these names too (they already reach s_resolveState if a consumer
	// drew their sentinel through TexDefsResolveAtDraw; this covers the
	// names that never did).
	const char *pending[TEXDEFS_MISS_LOG_MAX + TEXDEFS_MISS_CACHE_MAX
	                    + TEXDEFS_MISSBEYOND_MAX];
	int nPending = 0;
	int pendingCap = (int)(sizeof(pending) / sizeof(pending[0]));
	for(int i = 0; i < s_missLogged && nPending < pendingCap; i++)
		pending[nPending++] = s_missNames[i];
	for(int i = 0; i < s_missBeyondDistinct && nPending < pendingCap; i++)
		pending[nPending++] = s_missBeyondNames[i];
	for(auto const &e : s_resolveState){
		if(e.second.tex)
			continue;		// already recovered
		if(nPending >= pendingCap)
			break;
		bool dup = false;
		for(int j = 0; j < nPending; j++)
			if(_stricmp(pending[j], e.first.c_str()) == 0){
				dup = true;
				break;
			}
		if(!dup)
			pending[nPending++] = e.first.c_str();
	}

	for(int i = 0; i < nPending; i++){
		const char *nm = pending[i];
		// Already recovered AND still pinned -> nothing to do (the pin is
		// re-validated by TexDefsDictResolveSlot above).
		int ci = TexDefsCacheFind(nm);
		if(ci >= 0 && s_missCache[ci].dictSlot >= 0 &&
		   TexDefsDictResolveSlot(s_missCache[ci].dictSlot, nm,
		                          objects, byteMap, size))
			continue;
		int hitSlot = -1;
		char hitDict[32] = {0};
		RwTexture *tex = NULL;
		if(slotDict){
			tex = RwTxdFindNamed(slotDict, nm);
			if(tex){
				hitSlot = slot;
				strncpy(hitDict, slotDictName, sizeof(hitDict) - 1);
				hitDict[sizeof(hitDict) - 1] = '\0';
			}
		}
		if(!tex && fullSweep)
			tex = TexDefsDictSweep(nm, &hitSlot, hitDict,
			                       objects, byteMap, size);
		if(tex)
			TexDefsDeferAccept(nm, tex, hitSlot,
			                   hitDict[0] ? hitDict : slotDictName);
	}
}

static RwTexture*
TexDefsFindCB(char *name)
{
	RwTexture *tex = gameGetTexture(name);
	// N6: a lookup MISS must be NAMED and the recoverable root cause
	// (case/extension mismatch) retried before declaring it. Runs only on
	// an already-failed lookup — the hot success path above is untouched.
	if(!tex && name && name[0]){
		// N8: cross-dict fallback. A pinned slot re-resolves cheaply; the full
		// sweep runs once per name; known-miss names only count. The miss
		// receipt (TexDefsNoteMiss) still counts EVERY failed lookup, so the
		// `[texdefs] miss` tokens stay semantically stable.
		int ci = TexDefsCacheFind(name);
		bool tryResolve = false;
		if(ci >= 0 && s_missCache[ci].dictSlot >= 0){
			uint32 objects; uint8 *byteMap; int size;
			if(TexDefsPoolInfo(&objects, &byteMap, &size))
				tex = TexDefsDictResolveSlot(s_missCache[ci].dictSlot, name,
				                             objects, byteMap, size);
			if(!tex)
				tryResolve = true;   // pinned dict unloaded -> re-sweep
		}else if(ci < 0){
			tryResolve = true;       // first miss for this name
		}else{
			TexDefsNoteMiss(name);   // known miss (dictSlot == -1): count only
		}
		if(tryResolve){
			tex = TexDefsTryNameVariant(name);
			if(!tex){
				uint32 objects; uint8 *byteMap; int size;
				int hitSlot = -1;
				char hitDict[32] = {0};
				if(TexDefsPoolInfo(&objects, &byteMap, &size))
					tex = TexDefsDictSweep(name, &hitSlot, hitDict,
					                       objects, byteMap, size);
				if(tex){
					TexDefsCacheUpsert(name, hitSlot);
					if(dbglog_throttle("texdefs_dictfallback"))
						dbglog("[texdefs] dictfallback '%s' txd='%s' found-in='%s' "
						       "(cross-dict recovery — broken LOD parent)",
						       name,
						       currentTxdName[0] ? currentTxdName : "?",
						       hitDict[0] ? hitDict : "?");
				}else{
					TexDefsNoteMiss(name);
					TexDefsCacheUpsert(name, -1);
				}
			}else{
				dbglog("[texdefs] miss-recovered '%s' -> '%s' txd='%s' "
				       "(case/extension mismatch — fix the request name)",
				       name, tex->name[0] ? tex->name : "?",
				       currentTxdName[0] ? currentTxdName : "?");
			}
		}
	}
	// N9 POLICY (2026-11-27 refactor, per hard requirement "zeroed settings =
	// vanilla"): the game receives NULL on a miss so its OWN texture
	// streaming / recovery can still fire — the grey sentinel is applied
	// ONLY at draw time by our pipes (bldPolicyTexture / vehPolicyTexture /
	// SkinStage0Resync / render_callbacks), never stored in material->texture.
	// This restores the game's stock recovery path: with a Vanilla pipe (no
	// custom pipe attached) the game binds NULL exactly as before the policy
	// existed, and our pipes still show the grey sentinel as the "missing
	// texture" signal. The policy is off-switchable
	// (TexDefsMissPolicyEnable(false)) — then the legacy NULL return stands.
	if(!tex && s_texDefsMissPolicyOn){
		TexDefsMissClass cls = TexDefsMissClassify();
		// Draw-time sentinel is applied by our pipes (bldPolicyTexture /
		// vehPolicyTexture / SkinStage0Resync / render_callbacks), NOT by the
		// game's material->texture — so the game receives NULL and keeps its
		// own texture streaming / recovery path alive (faithful-descendant
		// doctrine: zeroed settings = vanilla, features added on top).
		// The sentinel pointer is only needed for the miss-policy receipt's
		// action field; use the class singleton (no clone allocated) since
		// nothing is returned.
		RwTexture *sent = (cls == TEXDEFS_MISS_WORLD)
		                  ? s_missSentinelGrey
		                  : s_missSentinelClear;
		TexDefsNoteMissPolicy(name ? name : "", cls, sent);
		return NULL;
	}
	if(tex){
		TexInfo *info = findTexInfo(name);
		if(info){
			bool sourceNoiseDetail = info->noiseDetail;
			if(info->affiliateTex)
				info = info->affiliateTex;
			if(info->hassibling){
				static char sibling[200];
				snprintf(sibling, sizeof(sibling), "%s_%s", name, currentTxdName);
				TexInfo *sib = findTexInfo(sibling);
				if (sib) 
					info = sib;
			}
			if(info->affiliateTex)
				info = info->affiliateTex;
			if (sourceNoiseDetail) info->noiseDetail = true;
			TexDefsAssignInfo(tex, info);
		}
		if(nmPairingEnabled())
			nmProbePairing(tex, name);
	}
	// W1 white-tree discriminator: this is the ONE point where the find-CB
	// hands a plain NULL back to the game (miss policy off / failed sentinel
	// create — stock policy returns a sentinel instead). Name + pointer,
	// first 16 distinct one-shot, throttled after. Absence of any
	// `nulltex`/`miss`/`missbeyond +` line for a white-tree texture name
	// is the case-(a) signature (lookup never reached this callback).
	if(!tex)
		TexDefsNoteNullTex(name, tex, "findcb");
	return tex;
}

// ---------------------------------------------------------------------------
// Engine-init plugin registration (before any texture/atomic exists).
// SINGLE SkyGfx RW plugin registry, called from myPluginAttach (0x53D903).
// NO code patch is involved: pure registration API calls, in this order:
// EDED atomic slot, PDS atomic slot, texdb texture slot, NmPair texture slot.
// ---------------------------------------------------------------------------

static void*
texdbCtor(void *object, RwInt32 offsetInObject, RwInt32)
{
	*RWPLUGINOFFSET(TexInfo*, object, offsetInObject) = nil;
	return object;
}

// RwTextureRegisterPlugin lives in this TU (the only definition) — static so
// the merged module owns its only external-linkage use.
static WRAPPER RwInt32 RwTextureRegisterPlugin(RwInt32, RwUInt32, RwPluginObjectConstructor, RwPluginObjectDestructor, RwPluginObjectCopy) { EAXJMP(0x7F3BB0); }

int
TexDefsPluginAttach(void)
{
	// 1. EDED: per-atomic DefinedVertexShader byte.
	if(ededAtmOffset <= 0){
		ededAtmOffset = RpAtomicRegisterPlugin(4, 0xEDED, ededConstruct, NULL, ededCopy);
		if(ededAtmOffset <= 0)
			dbglog("EDED: RpAtomicRegisterPlugin FAILED (offset=%d) — all atomics degrade to DEFAULT", ededAtmOffset);
		else if(RpAtomicRegisterPluginStream(0xEDED, EDEDPluginRead, NULL, EDEDPluginGetSize) < 0)
			dbglog("EDED: stream registration FAILED (offset=%d)", ededAtmOffset);
	}

	// 2. PDS: atomic stream-rights slot (0x131) -> PS2 building pipelines.
	if(pdsAtmOffset <= 0){
		pdsAtmOffset = RpAtomicRegisterPlugin(0, 0x131, NULL, NULL, NULL);
		RpAtomicSetStreamRightsCallBack(0x131, pdsAtmCB);
	}

	// 3. texdb: TexInfo* slot.
	if(texdbOffset <= 0)
		texdbOffset = RwTextureRegisterPlugin(sizeof(TexInfo*), 0xbadeaffe, texdbCtor, nil, nil);

	// 4. NmPair: diffuse->normal pairing slot.
	if(nm_pluginOffset <= 0){
		nm_pluginOffset = RwTextureRegisterPlugin(sizeof(NmPair), 0x4E4D3137,
			nmPairCtor, nil, nil);
		if(nm_pluginOffset <= 0)
			dbglog("nm: texture plugin registration FAILED (%d) — pairing disabled", nm_pluginOffset);
	}

	dbglog("[texdefs] plugin registry: eded=%d pds=%d texdb=%d nmpair=%d",
		ededAtmOffset, pdsAtmOffset, texdbOffset, nm_pluginOffset);
	return 1;                                // never fail the game's plugin chain
}

// ---------------------------------------------------------------------------
// TXD-name hooks + the ONE find-CB patch.
// ---------------------------------------------------------------------------

static int
storeTxdName(int ret/*return address on stack*/, char *txdname)
{
	// txdslot comes straight from the game (eax at the hook site) and
	// txdname from the hooked function's stack args: both must be
	// validated before indexing txdnames[65500] / strdup.
	if(txdslot < 0 || txdslot >= 65500)
		return txdslot;
	// N7: a new TXD slot is registered — invalidate the miss cache so a name
	// that missed earlier is retried under the new context (self-heal for a
	// transient load-order miss). Bounded, deterministic.
	TexDefsMissCacheReset();
	// CLASS 4: Free existing entry before overwriting (prevents memory leak)
	if(txdnames[txdslot]) {
		free(txdnames[txdslot]);
		txdnames[txdslot] = NULL;
	}
	if(txdname)
		txdnames[txdslot] = strdup(txdname);
	// DEFERRED RETRY (2026-11-17): a slot was registered — re-check every
	// name still awaiting recovery against the loaded dictionaries. The new
	// slot's own dict is NULL by construction here (see the RE note on
	// TexDefsDeferRetry), so this leg is the throttled pool sweep; the live
	// slot probe runs from setCurrentTxd. Runs AFTER the cache reset above
	// so recovered names are re-pinned under the fresh cache.
	TexDefsDeferRetry(txdslot);
	return txdslot;
}

static void
setCurrentTxdName(void)
{
	// txdnames[txdslot] can be NULL (slot never stored / strdup failure)
	// and txdslot out of range on a foreign caller; strncpy(dst, NULL) is UB.
	if(txdslot < 0 || txdslot >= 65500 || !txdnames[txdslot]){
		currentTxdName[0] = '\0';
	}else{
		strncpy(currentTxdName, txdnames[txdslot], sizeof(currentTxdName) - 1);
		currentTxdName[sizeof(currentTxdName) - 1] = '\0';
	}
	// DEFERRED RETRY (2026-11-17): the dictionary being made current is
	// loaded and populated (the game reads DFFs against it right after), so
	// probe it — and throttled, the whole pool — for every pending miss name
	// before the next material read can bind another sentinel clone.
	TexDefsDeferRetry(txdslot);
}

static void __declspec(naked)
addTxdSlot_hook(void)
{
	_asm{
		mov	txdslot,eax
		call	storeTxdName
		retn
	}
}

static void __declspec(naked)
setCurrentTxd_hook(void)
{
	_asm{
		mov	ecx,[esp+4]
		mov	txdslot,ecx
		mov	[esp+4],edx
		push	eax	// return value, is it even used?
		call	setCurrentTxdName
		pop	eax	// we overwrote the original code but there's another jump closeby
		push	0x7319DB
		retn
	}
}

void
TexDefsInit(void)
{
	static bool installed = false;
	if(installed)
		return;
	installed = true;

	// Byte-verify both txd-name hook sites before writing the 5-byte jump
	// (patch_verify/corona_install discipline): on a different exe build, or
	// if another lane already hooked the entry, skip that install once
	// instead of corrupting the instruction stream.
	{
		const uint8 *site = (const uint8 *)0x731CCB;
		if(site[0] == 0xC3){
			InjectHook(0x731CCB, addTxdSlot_hook, PATCH_JUMP);
		}else{
			dbglog("[texdefs] 0x731CCB MISMATCH -> skipped (op=%02X, expect C3 ret)",
			       (unsigned)site[0]);
		}
	}
	{
		const uint8 *site = (const uint8 *)0x7319EB;
		if(site[0] == 0x89 && site[1] == 0x54 && site[2] == 0x24 && site[3] == 0x04 && site[4] == 0xE9){
			InjectHook(0x7319EB, setCurrentTxd_hook, PATCH_JUMP);
		}else{
			dbglog("[texdefs] 0x7319EB MISMATCH -> skipped (%02X %02X %02X %02X %02X, expect 89 54 24 04 E9)",
			       site[0], site[1], site[2], site[3], site[4]);
		}
	}

	// ONE find-callback patch. Verify the registration site is still
	// `push imm32` before rewriting its immediate, capture the previous
	// callback through that immediate, then install TexDefsFindCB.
	const uint8 op = *(uint8 *)0x731FD5;
	if(op != 0x68){
		dbglog("[texdefs] 0x731FD5 MISMATCH -> skipped (op=%02X, expect 68 push imm32)",
		       (unsigned)op);
		return;
	}
	gameGetTexture = *(RwTexture*(**)(char*))(0x731FD5+1);
	if(!gameGetTexture){
		dbglog("[texdefs] no find CB at 0x731FD6 — find chain NOT installed");
		return;
	}
	Patch(0x731FD5+1, TexDefsFindCB);

	dbglog("[texdefs] find chain installed (get=%p) pluginA=%d pluginB=%d",
	       (void*)gameGetTexture, texdbOffset, nm_pluginOffset);
}

// ---------------------------------------------------------------------------
// texdb.txt load (InitialiseGame_hook).
// ---------------------------------------------------------------------------

static void
readTexDbTxt(void)
{
	char buf[200], *start, *end, *p;
	TexInfo *info;

	char *path = getpath("models\\texdb.txt");
	if(path == nil)
		return;
	FILE *f = fopen(path, "r");
	if(f == nil)
		return;

	while(fgets(buf, 200, f)){
		char *filename = nil;
		char *affiliate = nil;
		int alphamode = 0;
		int isdetail = 0;
		int hasdetail = 0;
		int detailtile = 80;
		int hassibling = 0;
		int noiseDetail = 0;
		int dualPass = 1;
		int zwriteThreshold = 0;
		start = end = buf;
		while(end){
			if(start[0] == '"'){
				end = strchr(++start, '\"');
				// Unterminated quote: strchr(NULL, ...) below would be UB
				// (and *p = '\0' a null deref). Skip the rest of the line.
				if(end == nil)
					break;
				p = end;
				end = strchr(end, ' ');
				*p = '\0';
			}else
				end = strchr(start, ' ');
			if(end)
				*end = '\0';

			if(strchr(start, '=') == nil)
				filename = start;
			else if(strncmp(start, "cat=", 4) == 0)
				goto nextline;
			else if(strncmp(start, "alphamode=", 10) == 0)
				alphamode = atoi(start+10);
			else if(strncmp(start, "isdetail=", 9) == 0)
				isdetail = atoi(start+9);
			else if(strncmp(start, "hasdetail=", 10) == 0)
				hasdetail = atoi(start+10);
			else if(strncmp(start, "detailtile=", 11) == 0)
				detailtile = atoi(start+11);
			else if(strncmp(start, "hassibling=", 11) == 0)
				hassibling = atoi(start+11) != 0;
			else if(strncmp(start, "affiliate=", 10) == 0)
				affiliate = start+10;
			else if(strncmp(start, "noiseDetail=", 12) == 0)
				noiseDetail = atoi(start+12);
			// Deprecated alias retained for backwards compatibility: pre-rename
			// texdb.txt files used this legacy spelling. Kept so existing user files still load.
			else if(strncmp(start, "stochastic=", 11) == 0)
				noiseDetail = atoi(start+11);
			else if(strncmp(start, "dualPass=", 9) == 0)
				dualPass = atoi(start+9);
			else if (strncmp(start, "zwriteThreshold=", 16) == 0)
				zwriteThreshold = atoi(start + 16);

			start = end+1;
		}
		if(filename){
			if(isdetail){
				// detailTextures[] has 100 slots; a negative isdetail would
				// write before the array.
				if(isdetail >= 0 && isdetail < 100){
					RwTexture *dtex = RwTextureRead(filename, nil);
					// N9 CONSUMER HONOR (inside texdefs.cpp): the detail
					// texture is a MULTIPLY layer bound on stage 2
					// (buildingPipe.cpp bldBindDiffuse3L), not a draw of
					// its own. A policy sentinel (the 1x1 dim grey) bound
					// there would multiply every detailed surface by
					// 0x60/0xFF — a global darkening, i.e. the policy
					// CREATING a new defect instead of closing one. A
					// failed detail lookup therefore stays NULL: the
					// detail layer is simply absent (legacy behavior).
					if(TexDefsMissTextureKind(dtex) != TEXDEFS_MISS_TEX_NONE){
						if(dbglog_once("texdefs_detail_miss"))
							dbglog("[texdefs] detail '%s' unresolved — policy sentinel ignored (detail layer stays off)",
							       filename);
						dtex = NULL;
					}
					detailTextures[isdetail] = dtex;
				}else
					dbglog("texdb: isdetail=%d out of bounds (max 99), skipping", isdetail);
			}else if(hasdetail || alphamode || hassibling || affiliate || noiseDetail){
				std::string s = filename;
				strtolower(s);
				// SAFETY: raw new — ownership held by global texdb map; needs RAII refactor
				info = new TexInfo;
				memset(info, 0, sizeof(TexInfo));

				info->name = strdup(filename);
				if(affiliate)
					info->affiliate = strdup(affiliate);
				if(hasdetail){
					// detailnum indexes detailTextures[100] in the link-up
					// pass below; clamp out-of-range values here so the
					// read stays in bounds.
					if(hasdetail >= 0 && hasdetail < 100){
						info->detailnum = hasdetail;
						info->detailtile = detailtile;
					}
				}
				if(hassibling)
					info->hassibling = 1;
				if(alphamode)
					info->alphamode = alphamode;
				if (noiseDetail)
					info->noiseDetail = 1;
				if (dualPass)
					info->dualPass = dualPass;
				if (zwriteThreshold > 0)
					info->zwriteThreshold = zwriteThreshold;

				texdbInsert(s, info);
			}
		}
	nextline:;
	}

	// Link up stuff
	for(auto const &e : texdb){
		info = e.second;
		if(info->affiliate)
			info->affiliateTex = findTexInfo(info->affiliate);
		// detailnum is clamped at parse time; belt-and-braces guard the
		// array read anyway (a stale hand-edited database must not read
		// past detailTextures[100]). NOTE: detailnum is uint8 and memset
		// to 0, so "unset" is 0 — the old `>= 0` leg was dead and made
		// EVERY entry (alphamode/hassibling/affiliate-only) pick up
		// detailTextures[0] whenever an isdetail=0 line existed. Original
		// texdb.cpp semantics: 0 = no detail, skip.
		if(info->detailnum > 0 && info->detailnum < 100)
			info->detail = detailTextures[info->detailnum];
	}

	fclose(f);
}

// ---------------------------------------------------------------------------
// N9 MASTER LEVER (2026-11-27): `texDefsMissPolicy` in skygfx.ini, 0/1,
// default 1 (absent key == today's behaviour, a plain skygfx.ini is
// byte-identical to the pre-lever build).
//
// WHY THIS EXISTS: TexDefsMissPolicyEnable() has been public since
// 2026-11-09 with ZERO CALLERS — the fail-closed miss policy was therefore
// unconditional and no amount of "turn everything off" could reach the
// pre-N9 behaviour. This wires the existing lever, reading the SAME file with
// the SAME parser (linb::ini) the config lane uses, so there is one INI
// grammar in the tree. Probed from InitialiseGame_hook BEFORE the original
// CGame::Initialise, so it is in force before the first world DFF can miss.
// PATH: the legacy `skygfx.ini` (config slot 0) — the numbered `skygfxN.ini`
// slots are not probed from here (IniPathForSlot lives in config.cpp, out of
// this lane's write scope), so author the key in the base INI.
//
// OFF PATH (verified in TexDefsFindCB): the `if(!tex && s_texDefsMissPolicyOn)`
// block is skipped, the lookup falls through to the legacy NULL return, the
// `nulltex site=findcb` receipt names it (that line IS the proof the off-path
// ran), and every consumer keeps its pre-N9 fallback — buildingPipe's
// bldPolicyTexture(NULL) -> gpWhiteTexture, particles skip, vehiclePipe maps
// to the world action. No sentinel is ever created or bound.
//
// SCOPE OF "OFF": it restores the LEGACY CONSUMER FALLBACK, not the missing
// TEXTURES. The root defect (the game's dict->parent link is broken in this
// install, so the full-detail TXD for an LOD dictionary never resolves) is
// untouched by the policy either way — OFF trades the dim-grey 1x1 slab for
// the pre-N9 white one. Recovery (TexDefsResolveAtDraw / TexDefsDeferRetry)
// is independent of the lever and keeps running. Reported honestly in the
// receipt so nobody reads OFF as "textures restored".
// ---------------------------------------------------------------------------
static void
TexDefsReadMissPolicyLever(void)
{
	char *path = getpath("skygfx.ini");
	if(path == nil)
		return;
	linb::ini cfg;
	if(!cfg.load_file(path))
		return;
	std::string v = cfg.get("SkyGfx", "texDefsMissPolicy", "");
	if(v.empty())
		return;	// absent -> default ON (behaviour unchanged)
	int on = atoi(v.c_str());
	TexDefsMissPolicyEnable(on != 0);
	dbglog("[texdefs] miss-policy lever texDefsMissPolicy=%d -> %s", on,
	       on ? "ON (fail-closed: missing name binds the 1x1 sentinel)"
	          : "OFF (legacy: NULL -> gpWhiteTexture; NOT a texture restore)");
}

void
TexDefsLoadDatabase(void)
{
	// Master lever FIRST: it must be in force before the very first DFF load
	// can miss, and it must run even if the Mobile_details.txd guard below
	// aborts the rest of this function on a broken install.
	TexDefsReadMissPolicyLever();

	char *path = getpath("models\\Mobile_details.txd");
	if(path == nil)
		return;
	RwStream *stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
	// A failed open must not reach RwStreamFindChunk(NULL) — guard and fall
	// through to the detailTxd == nil error path below.
	if(stream && RwStreamFindChunk(stream, rwID_TEXDICTIONARY, nil, nil))
		detailTxd = RwTexDictionaryStreamRead(stream);
	if(stream)
		RwStreamClose(stream, nil);
	if(detailTxd == nil){
		MessageBox(nil, "Couldn't find Tex Dictionary inside 'models\\Mobile_details.txd'", "Error", MB_ICONERROR | MB_OK);
		exit(0);
	}
	// we can just set this to current because we're executing before CGame::Initialise
	// which sets up "generic" as the current TXD
	RwTexDictionarySetCurrent(detailTxd);

	readTexDbTxt();

	//default
	memset(&faketexinfo, 0, sizeof(TexInfo));
	faketexinfo.dualPass = 1;

	// for roadsign texture
	// SAFETY: raw new — ownership held by global texdb map; needs RAII refactor
	TexInfo* info = new TexInfo;
	memset(info, 0, sizeof(TexInfo));
	info->dualPass = 1;
	info->zwriteThreshold = 100;
	texdbInsert("roadsign", info);
}

// ---------------------------------------------------------------------------
// Read side (pipe callbacks).
// ---------------------------------------------------------------------------

TexInfo*
TexDefsGetInfo(RwTexture *tex)
{
	if(tex == nil || texdbOffset <= 0){
		// W1 white-tree discriminator (consumer observation point): a
		// world/vegetation consumer is reading a material whose texture
		// pointer is NULL — the class that renders via gpWhiteTexture
		// (white bare trees), never via the grey policy sentinel. Bounded
		// distinct table + throttle; fires for the legitimate "no diffuse
		// map" materials too, so read `total` as a rate, `site=findcb`
		// lines as the per-name proof.
		if(tex == nil)
			TexDefsNoteNullTex(nil, tex, "getinfo");
		return &faketexinfo;
	}
	TexInfo **plg = RWPLUGINOFFSET(TexInfo*, tex, texdbOffset);
	if(*plg)
		return *plg;
	else
		return &faketexinfo;
}

TexInfo*
TexDefsFindInfo(char *name)
{
	if(name == nil)
		return nil;
	return findTexInfo(name);
}

// ---------------------------------------------------------------------------
// THE D3D-texture accessor (game RwD3D9SetTexture internals, 0x7FDCD0):
//   ext = raster + *(DWORD*)0xB4E9E0; ext+0 = IDirect3DTexture9*,
//   ext+8 = valid byte flag. Full guard chain (NULL tex/raster, extOff
//   sanity 0 / >0x100000 / unaligned, plausible-pointer check) with the
//   texalpha acceptance rule: accept when EITHER the ext+8 flag is set OR
//   ext+0 is a plausible pointer (the flag can be stale at 0 while ext+0
//   still holds a live pointer). Borrowed pointer: NO AddRef, NO Release,
//   no caching here (callers cache). outWhy/outExtOff are optional; see
//   texdefs.h (outExtOff is written only when the offset word is read).
// ---------------------------------------------------------------------------
IDirect3DTexture9 *
TexDefsGetD3DTexture(RwTexture *tex, int *outWhy, uint32 *outExtOff)
{
	if(outWhy)
		*outWhy = TEXDEFS_D3D_WHY_NO_TEXTURE;
	if(!tex)
		return NULL;
	if(!tex->raster){
		if(outWhy)
			*outWhy = TEXDEFS_D3D_WHY_NO_RASTER;
		return NULL;
	}
	uint32 extOff = *(uint32 *)0xB4E9E0;
	if(outExtOff)
		*outExtOff = extOff;
	if(extOff == 0 || extOff > 0x100000u || (extOff & 3u)){
		if(outWhy)
			*outWhy = TEXDEFS_D3D_WHY_BAD_EXT_OFFSET;
		return NULL;
	}
	const uint8 *ext = (const uint8 *)tex->raster + extOff;
	uint8 validFlag = ext[8];	// ext+8 = "valid" byte (may be stale)
	IDirect3DTexture9 *d3d = *(IDirect3DTexture9 * const *)ext;
	bool ptrPlausible = d3d != NULL
	                 && (uintptr_t)d3d >= 0x10000u
	                 && (((uintptr_t)d3d & 3u) == 0);

	// Accept when EITHER signal says the extension is usable. Reject only
	// when the flag is clear AND the pointer is junk.
	if(!validFlag && !ptrPlausible){
		if(outWhy)
			*outWhy = TEXDEFS_D3D_WHY_NO_D3D_TEX;
		return NULL;
	}
	if(!d3d){
		if(outWhy)
			*outWhy = TEXDEFS_D3D_WHY_NO_D3D_TEX;
		return NULL;
	}
	if(!ptrPlausible){
		// Flag claims the extension is live but ext+0 is not a plausible
		// D3D9 texture pointer; never call through it (would fault).
		if(outWhy)
			*outWhy = TEXDEFS_D3D_WHY_BAD_EXT_PTR;
		return NULL;
	}
	if(outWhy)
		*outWhy = TEXDEFS_D3D_WHY_OK;
	return d3d;
}

// Render-side lookup + D3D texture extraction.
IDirect3DTexture9 *TexDefsGetNormalMap(RwTexture *diffuse, bool *outHasAlpha)
{
	if(outHasAlpha)
		*outHasAlpha = false;
	if(!nm_pluginOffset || nm_pluginOffset < 0 || !diffuse)
		return NULL;
	if(!config || !config->normalMapEnable)
		return NULL;
	if(gHasExternalNormalMapPlugin)
		return NULL;                             // DK22Pac's plugin owns the path

	NmPair *pair = RWPLUGINOFFSET(NmPair, diffuse, nm_pluginOffset);
	if(!pair->normTex)
		return NULL;

	if(!pair->d3dTex){
		// Game RwD3D9SetTexture formula (0x7FDCD0): ext = raster +
		// *(DWORD*)0xB4E9E0; ext+0 = IDirect3DTexture9*, ext+8 = valid flag.
		// The full guard chain lives in TexDefsGetD3DTexture above (borrowed
		// pointer; also accepts a plausible ext+0 with a stale ext+8 flag).
		IDirect3DTexture9 *d3d = TexDefsGetD3DTexture(pair->normTex);
		if(!d3d)
			return NULL;
		pair->d3dTex = d3d;

		// Gloss alpha channel: only formats with a real 8-bit+ alpha. DXT1's
		// 1-bit alpha is not gloss data (decodes all-opaque → mirror shine).
		D3DSURFACE_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Format = D3DFMT_UNKNOWN;
		if(SUCCEEDED(d3d->GetLevelDesc(0, &desc))){
			switch(desc.Format){
			case D3DFMT_DXT5:
			case D3DFMT_A8R8G8B8:
			case D3DFMT_A8B8G8R8:
				pair->hasAlpha = 1;
				break;
			default:
				break;
			}
		}
		static int nm_fmtLog = 0;
		if(nm_fmtLog < 8)
			dbglog("nm: extract '%s' d3d=%p fmt=%d alpha=%d",
				pair->normTex->name ? pair->normTex->name : "?",
				d3d, (int)desc.Format, (int)pair->hasAlpha);
		nm_fmtLog++;
	}

	if(outHasAlpha)
		*outHasAlpha = pair->hasAlpha != 0;
	return pair->d3dTex;
}

// ---------------------------------------------------------------------------
// Player-vehicle scoping (vehicles only; buildings are never scoped).
// ---------------------------------------------------------------------------

static void *(__cdecl *nm_FindPlayerVehicle)(int, int) = (void *(__cdecl *)(int, int))0x56E0D0;

bool TexDefsVehicleAllowed(const void *atomic)
{
	if(!config || !config->normalMapPlayerOnly)
		return true;                             // scope off — all vehicles
	if(!atomic)
		return false;

	// Per-frame cache: one FindPlayerVehicle poll per render frame.
	static uint32 s_frame = 0;
	static RpClump *s_clump = NULL;
	uint32 frame = RWSRCGLOBAL(renderFrame);
	if(s_frame != frame){
		s_frame = frame;
		s_clump = NULL;
		void *veh = nm_FindPlayerVehicle(-1, 0);
		if(veh){
			// CEntity::m_pRWObject (+0x18, gta.h CPlaceable layout). For
			// vehicles this IS the RpClump* (RwObject-typed header field).
			RwObject *rwObj = ((const CEntity *)veh)->m_pRWObject;
			if(rwObj)
				s_clump = (RpClump *)rwObj;
		}
	}
	if(!s_clump)
		return false;
	return RpAtomicGetClump((RpAtomic *)atomic) == s_clump;
}

// ==== DK22Pac external normal-map plugin lane (was src/rw/normalmap.cpp) ====
// Based on DK22Pac's Normal Map Plugin for GTA SA.
// Gated on the separate INI key [SkyGfx] enableNormalMaps (default "0").

// NOTE: RwEngineInstance (void*) is declared by rwplcore.h (via rpnormmap.h).
// _RwD3DDevice (LPDIRECT3DDEVICE9) is declared by rwcore.h only when _D3D9_H_
// is defined; declare it explicitly here to be safe. rpnormmap.lib resolves
// both to the stubs in normmap_stubs.cpp (default 0); they MUST be pointed at
// the game's real engine + device before the plugin attaches and before any
// normalmap rendering (DK22Pac does the same).
extern "C" {
    extern LPDIRECT3DDEVICE9 _RwD3DDevice;
}

// Game function wrappers (by address, since we don't link game symbols)
// RpAtomicGetGeometry is INLINED in the SA exe — 0x749AB0 is mid-function
// game code (byte-verified 2026-11-02), NOT an RW entry; calling it ran
// arbitrary float-math. The geometry pointer lives at atomic+0x18 (verified
// via RpAtomicDestroy 0x749DC0 handing it to RpGeometryDestroy 0x74CCC0),
// so read it directly.
static RpGeometry *game_RpAtomicGetGeometry(void *atomic) {
	return atomic ? *(RpGeometry**)((uintptr_t)atomic + 0x18) : nullptr;
}
static int(__cdecl *game_GetPipelineID)(void *atomic) = (int(__cdecl*)(void *))0x72FC40;
static int(__cdecl *game_RpMatFXAtomicQueryEffects)(void *atomic) = (int(__cdecl*)(void *))0x811C30;
static int(__cdecl *game_RpMatFXMaterialGetEffects)(void *material) = (int(__cdecl*)(void *))0x812140;
static void(__cdecl *game_RpGeometryForAllMaterials)(void *geom, void *cb, void *data) = (void(__cdecl*)(void *, void *, void *))0x74C790;

// Pipeline IDs
#define RSPIPE_PC_CustomBuilding_PipeID    0x53F20098
#define RSPIPE_PC_CustomBuildingDN_PipeID  0x53F2009C

// Game globals
#define gfNightState (*(float *)0x8D12C0)
#define numExtraDirectionalLights (*(int *)0xC88708)
#define extraDirectionalLights ((RpLight **)0xC886F0)
#define extraDirectionalLightIntensities ((float *)0xC8867C)
#define VectorToSun ((RwV3d *)0xB7CA50)
#define CurrentTimecycValue (*(unsigned int *)0xB79FD0)
#define AmbientR ((float)*(unsigned __int16 *)0xB7C4D0 / 255.0f)
#define AmbientG ((float)*(unsigned __int16 *)0xB7C4D2 / 255.0f)
#define AmbientB ((float)*(unsigned __int16 *)0xB7C4D4 / 255.0f)

// Normal map globals (file-static: only this TU consumes them)
static RxPipeline *gNormalMapAtomicPipelines[2] = {NULL, NULL};
static RpLight *gDirectionalLight = NULL;
static RwRGBAReal gAmbClr;

// Settings
static RwRGBAReal ambientMult = {1.0f, 1.0f, 1.0f, 0.0f};
static RwRGBAReal directionalMult = {0.5f, 0.5f, 0.5f, 0.0f};
static unsigned int useSunColors = 1;
static float sunColorsContribution = 0.3f;
static float moonIntensity = 0.2f;

// Game function wrappers for light/frame
#define rpLIGHTDIRECTIONAL 1
static RpLight *(__cdecl *game_RpLightCreate)(int type) = (RpLight *(__cdecl*)(int))0x752110;
static void(__cdecl *game_RpLightDestroy)(RpLight *light) = (void(__cdecl*)(RpLight *))0x7520D0;
static void(__cdecl *game_RpLightSetColor)(void *light, RwRGBAReal *color) = (void(__cdecl*)(void *, RwRGBAReal *))0x751A90;
static void(__cdecl *game_RpLightSetRadius)(void *light, float r) = (void(__cdecl*)(void *, float))0x751A70;
static RwFrame *(__cdecl *game_RwFrameCreate)() = (RwFrame *(__cdecl*)())0x7F0410;
static void(__cdecl *game_RwFrameDestroy)(void *frame) = (void(__cdecl*)(void *))0x7F05A0;
static void(__cdecl *game_RwObjectHasFrameSetFrame)(void *obj, void *frame) = (void(__cdecl*)(void *, void *))0x804EF0;
static void(__cdecl *game_RwFrameUpdateObjects)(void *frame) = (void(__cdecl*)(void *))0x7F0910;

static RpLight *GetAmbientLight() { return *(RpLight **)0xC886E8; }

// Helper: get light flags (RwObject.flags at offset 2)
static unsigned char &LightFlags(RpLight *light) { return ((unsigned char *)light)[2]; }
// Helper: get light parent frame (RwObject.parent at offset 4)
static void *&LightParent(RpLight *light) { return *(void **)((unsigned char *)light + 4); }

// ===== CustomPipeAtomicSetup (replaces 0x5DA610) =====

static RpMaterial *MaterialHasDefaultMatFXEffect_cb(RpMaterial *material, int *hasDefaultEffect)
{
    int effect = game_RpMatFXMaterialGetEffects(material);
    if(effect)
    {
        if(effect == 2)
        {
            int matfx_offset = *(int *)0x8D12C4;
            if(*(int *)((int)material + matfx_offset))
                return material;
        }
        *hasDefaultEffect = 1;
        return NULL;
    }
    return material;
}

static RpAtomic *__cdecl CustomPipeAtomicSetup_hook(RpAtomic *atomic)
{
    if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
        return atomic;
    if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
        return atomic;

    if(game_RpMatFXAtomicQueryEffects(atomic))
    {
        int hasDefaultEffect = 0;
        game_RpGeometryForAllMaterials(atomic->geometry, (void *)MaterialHasDefaultMatFXEffect_cb, &hasDefaultEffect);
        if(hasDefaultEffect)
            return atomic;
    }

    game_RpGeometryForAllMaterials(atomic->geometry, (void *)0x5DA560, 0);
    atomic->pipeline = *(RxPipeline **)0xC02D24;
    ((void(__cdecl *)(void *, int))0x72FC50)(atomic, 0x53F2009A);
    return atomic;
}

// ===== IsCBPCPipelineAttached (replaces 0x5D7F40) =====

extern int explicitBuildingPipe;

static int __cdecl IsCBPCPipelineAttached_hook(RpAtomic *atomic)
{
    if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
        return 0;
    if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
        return 0;

    if(game_RpMatFXAtomicQueryEffects(atomic))
    {
        int hasDefaultEffect = 0;
        game_RpGeometryForAllMaterials(atomic->geometry, (void *)MaterialHasDefaultMatFXEffect_cb, &hasDefaultEffect);
        if(hasDefaultEffect)
            return 0;
    }

    int pipelineId = game_GetPipelineID(atomic);
    void *geom = game_RpAtomicGetGeometry(atomic);
    // NULL-geom guard: with geom == NULL the original expression faulted
    // either inside the 0x5D6E90 helper or at the preLitLum deref — there is
    // no behavior where it worked, so the guard only converts a crash into
    // the correct "not attached" answer.
    return pipelineId == RSPIPE_PC_CustomBuildingDN_PipeID || pipelineId == RSPIPE_PC_CustomBuilding_PipeID ||
           (geom && ((int(__cdecl *)(void *))0x5D6E90)(geom) && ((RpGeometry *)geom)->preLitLum);
}

// ===== IsCCPCPipelineAttached (replaces 0x5D5B80) =====

static int __cdecl IsCCPCPipelineAttached_hook(RpAtomic *atomic)
{
    if(gNormalMapAtomicPipelines[0] && atomic->pipeline == gNormalMapAtomicPipelines[0])
        return 0;
    if(gNormalMapAtomicPipelines[1] && atomic->pipeline == gNormalMapAtomicPipelines[1])
        return 0;

    if(game_RpMatFXAtomicQueryEffects(atomic))
    {
        int hasDefaultEffect = 0;
        game_RpGeometryForAllMaterials(atomic->geometry, (void *)MaterialHasDefaultMatFXEffect_cb, &hasDefaultEffect);
        if(hasDefaultEffect)
            return 0;
    }

    return game_GetPipelineID(atomic) == 0x53F2009A;
}

// ===== Lighting =====

static void CreateLight()
{
    gDirectionalLight = game_RpLightCreate(rpLIGHTDIRECTIONAL);
    if(gDirectionalLight)
    {
        RwRGBAReal color = {1.0f, 1.0f, 1.0f, 0.0f};
        LightFlags(gDirectionalLight) = 0;
        game_RpLightSetColor(gDirectionalLight, &color);
        game_RpLightSetRadius(gDirectionalLight, 2.0f);
        RwFrame *frame = game_RwFrameCreate();
        if(frame)
            game_RwObjectHasFrameSetFrame((void *)((unsigned char *)gDirectionalLight + 4), frame);
        else
        {
            game_RpLightDestroy(gDirectionalLight);
            gDirectionalLight = NULL;
        }
    }
}

static void DestroyLight()
{
    if(gDirectionalLight)
    {
        void *&parent = LightParent(gDirectionalLight);
        if(parent)
            game_RwFrameDestroy(parent);
        game_RpLightDestroy(gDirectionalLight);
        gDirectionalLight = NULL;
    }
}

static void SetupDirectionalLight()
{
    if(!gDirectionalLight)
        return;

    LightFlags(gDirectionalLight) = 1;

	if(numExtraDirectionalLights > 0)
	{
		float bestIntensity = 0.0f;
		RpLight *best = NULL;
		for(int i = 0; i < numExtraDirectionalLights; i++)
		{
			if(extraDirectionalLights[i] &&
			   extraDirectionalLightIntensities[i] > bestIntensity)
			{
				bestIntensity = extraDirectionalLightIntensities[i];
				best = extraDirectionalLights[i];
			}
		}
		if(best)
		{
			game_RpLightSetColor(gDirectionalLight, &best->color);
			void *ourFrame = LightParent(gDirectionalLight);
			void *theirFrame = LightParent(best);
			if(ourFrame && theirFrame)
				memcpy((unsigned char *)ourFrame + 0x8, (unsigned char *)theirFrame + 0x8, sizeof(RwMatrix));
		}
	}
    else
    {
        if(gfNightState <= 0.99f)
        {
            float dayState = 1.0f - gfNightState;
            if(useSunColors)
            {
                RwRGBAReal color;
                color.red = dayState * (AmbientR * sunColorsContribution + directionalMult.red * (1.0f - sunColorsContribution));
                color.green = dayState * (AmbientG * sunColorsContribution + directionalMult.green * (1.0f - sunColorsContribution));
                color.blue = dayState * (AmbientB * sunColorsContribution + directionalMult.blue * (1.0f - sunColorsContribution));
                game_RpLightSetColor(gDirectionalLight, &color);
            }
            else
            {
                RwRGBAReal color = {directionalMult.red * dayState, directionalMult.green * dayState, directionalMult.blue * dayState, 1.0f};
                game_RpLightSetColor(gDirectionalLight, &color);
            }
            void *ourFrame = LightParent(gDirectionalLight);
            if(ourFrame)
            {
                RwMatrix *ltm = (RwMatrix *)((unsigned char *)ourFrame + 0x8);
                // VectorToSun (0xB7CA50) is the game's 16-entry sun-direction
                // array; the game's own accessor (gta.cpp GetSunDirection)
                // masks the 0xB79FD0 index with & 15. Mask here too so an
                // out-of-range timecycle value can never read past the array.
                int sunIdx = (int)(CurrentTimecycValue & 15u);
                ltm->at.x = -VectorToSun[sunIdx].x;
                ltm->at.y = -VectorToSun[sunIdx].y;
                ltm->at.z = -VectorToSun[sunIdx].z;
            }
        }
        else
        {
            float value = directionalMult.red * gfNightState * moonIntensity;
            RwRGBAReal color = {value, value, value, 1.0f};
            game_RpLightSetColor(gDirectionalLight, &color);
            void *ourFrame = LightParent(gDirectionalLight);
            if(ourFrame)
            {
                RwMatrix *ltm = (RwMatrix *)((unsigned char *)ourFrame + 0x8);
                ltm->at.x = 0.0f;
                ltm->at.y = 0.98893635f;
                ltm->at.z = -0.14834045f;
            }
        }
    }
    void *ourFrame = LightParent(gDirectionalLight);
    if(ourFrame)
        game_RwFrameUpdateObjects(ourFrame);
}

static void UnsetDirectionalLight()
{
    if(gDirectionalLight)
        LightFlags(gDirectionalLight) = 0;
}

static void SetupAmbientLight()
{
    RpLight *ambient = GetAmbientLight();
    if(!ambient)
        return;
    gAmbClr = ambient->color;
    RwRGBAReal clr = {gAmbClr.red * ambientMult.red, gAmbClr.green * ambientMult.green, gAmbClr.blue * ambientMult.blue, 1.0f};
    game_RpLightSetColor(ambient, &clr);
}

static void UnsetAmbientLight()
{
    RpLight *ambient = GetAmbientLight();
    if(ambient)
        game_RpLightSetColor(ambient, &gAmbClr);
}

// ===== Init / Shutdown =====

static bool externalNmInitialized = false;

void TexDefsTryAttachExternal(void)
{
    if(externalNmInitialized)
        return;

    // Skip if normalmaps disabled in config
    {
        static int checked = 0, enabled = 1;
        if(!checked){
            checked = 1;
            char buf[16] = {0};
            GetPrivateProfileStringA("SkyGfx", "enableNormalMaps", "0", buf, sizeof(buf), ".\\skygfx.ini");
            enabled = atoi(buf);
        }
        if(!enabled)
            return;
    }

    // The RW engine must be fully initialized before the plugin can attach.
    // TexDefsExternalInit() runs from DllMain (too early), so poll the game's
    // engine-instance pointer and defer until it is valid. This replicates
    // DK22Pac's FUNC_REGISTER_RW_PLUGIN timing. Called each frame from
    // RenderScene_hook until it succeeds.
    int engine = *(int *)0xC97B24;
    if(!engine)
        return;

    // Point rpnormmap.lib's stub engine globals at the game's real values.
    RwEngineInstance = (void *)engine;
    _RwD3DDevice = *(LPDIRECT3DDEVICE9 *)0xC97C28;

    dbglog("normalmap: deferred attach - engine=%08X device=%08X", engine, (unsigned int)_RwD3DDevice);
    if(!RpNormMapPluginAttach())
    {
        dbglog("normalmap: deferred RpNormMapPluginAttach FAILED");
        return;
    }
    dbglog("normalmap: RpNormMapPluginAttach OK (deferred)");

    gHasExternalNormalMapPlugin = true;

    CreateLight();
    dbglog("normalmap: light created");

    // Capture pipelines created by rpnormmap.lib
    gNormalMapAtomicPipelines[0] = RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSTATICPIPELINE);
    gNormalMapAtomicPipelines[1] = RpNormMapGetAtomicPipeline(rpNORMMAPATOMICSKINNEDPIPELINE);
    dbglog("normalmap: pipelines %p %p", gNormalMapAtomicPipelines[0], gNormalMapAtomicPipelines[1]);

    // Hook pipe setup functions.
    //
    // CRIT: 0x5D7F40 (IsCBPCPipelineAttached, CCustomBuildingDN) — this module
    // is the sole owner of that site (buildingPipe.cpp no longer patches it).
    // Installed gta_sa.exe 1.0 US prologue at 0x5D7F40 is `56 8B 74 24 08`
    // (push esi; mov esi,[esp+8]). Byte-verify it BEFORE writing the 5-byte
    // PATCH_JUMP: on a different exe build, or if another lane already hooked
    // the entry, a blind JMP corrupts the building pipeline. Mismatch => skip
    // the install once (normalmap feature degrades, game stays intact).
    {
        const unsigned char *site = (const unsigned char *)0x5D7F40;
        bool prologueOk =
            (site[0] == 0x56 && site[1] == 0x8B && site[2] == 0x74 && site[3] == 0x24) ||
            (site[0] == 0x55 && site[1] == 0x8B && site[2] == 0xEC);
        if(prologueOk)
        {
            InjectHook(0x5D7F40, IsCBPCPipelineAttached_hook, PATCH_JUMP);
        }
        else
        {
            dbglog("[nm_install] 0x5D7F40 MISMATCH -> skipped (%02X %02X %02X %02X %02X %02X %02X %02X)",
                   site[0], site[1], site[2], site[3], site[4], site[5], site[6], site[7]);
        }
    }
    // 0x5DA610 (CustomPipeAtomicSetup): gta_sa.exe 1.0 US prologue
    // `56 8B 74 24 08` (push esi; mov esi,[esp+8]). Verify before the
    // 5-byte PATCH_JUMP; MISMATCH => skip once, vanilla path stays intact.
    {
        const unsigned char *site = (const unsigned char *)0x5DA610;
        if(site[0] == 0x56 && site[1] == 0x8B && site[2] == 0x74 && site[3] == 0x24 && site[4] == 0x08)
        {
            InjectHook(0x5DA610, CustomPipeAtomicSetup_hook, PATCH_JUMP);
        }
        else
        {
            dbglog("[nm_install] 0x5DA610 MISMATCH -> skipped (%02X %02X %02X %02X %02X, expect 56 8B 74 24 08)",
                   site[0], site[1], site[2], site[3], site[4]);
        }
    }
    // 0x5D5B80 (IsCCPCPipelineAttached): gta_sa.exe 1.0 US prologue
    // `8B 44 24 04 50` (mov eax,[esp+4]; push eax). Same discipline.
    {
        const unsigned char *site = (const unsigned char *)0x5D5B80;
        if(site[0] == 0x8B && site[1] == 0x44 && site[2] == 0x24 && site[3] == 0x04 && site[4] == 0x50)
        {
            InjectHook(0x5D5B80, IsCCPCPipelineAttached_hook, PATCH_JUMP);
        }
        else
        {
            dbglog("[nm_install] 0x5D5B80 MISMATCH -> skipped (%02X %02X %02X %02X %02X, expect 8B 44 24 04 50)",
                   site[0], site[1], site[2], site[3], site[4]);
        }
    }
    dbglog("normalmap: hooks installed");

    externalNmInitialized = true;
    dbglog("normalmap: initialization complete");
}

void TexDefsExternalInit(void)
{
    // Runs from DllMain. The RW engine is not ready yet, so the actual
    // attach is deferred to TexDefsTryAttachExternal() (polled each frame
    // from RenderScene_hook until the engine is up). Attempt once here in
    // case the engine happens to be ready already.
    TexDefsTryAttachExternal();
}

void TexDefsExternalShutdown(void)
{
    if(!externalNmInitialized)
        return;

    DestroyLight();
    externalNmInitialized = false;
}

// ---------------------------------------------------------------------------
// Shutdown (DLL_PROCESS_DETACH): no RW/D3D9 calls.
// ---------------------------------------------------------------------------

void
TexDefsShutdown(void)
{
	// CLASS 4: Free all txdnames entries
	for(int i = 0; i < 65500; i++) {
		if(txdnames[i]) {
			free(txdnames[i]);
			txdnames[i] = NULL;
		}
	}

	for (auto &entry : texdb)
	{
		if (entry.second)
		{
			if (entry.second->name)
				free(entry.second->name);
			if (entry.second->affiliate)
				free(entry.second->affiliate);
			delete entry.second;
		}
	}
	texdb.clear();
}
