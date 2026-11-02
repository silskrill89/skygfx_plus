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
#include <string.h>
#include <ctype.h>
#include <rpnormmap.h>

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

static RwTexture*
TexDefsFindCB(char *name)
{
	RwTexture *tex = gameGetTexture(name);
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
	// CLASS 4: Free existing entry before overwriting (prevents memory leak)
	if(txdnames[txdslot]) {
		free(txdnames[txdslot]);
		txdnames[txdslot] = NULL;
	}
	if(txdname)
		txdnames[txdslot] = strdup(txdname);
	return txdslot;
}

static void
setCurrentTxdName(void)
{
	// txdnames[txdslot] can be NULL (slot never stored / strdup failure)
	// and txdslot out of range on a foreign caller; strncpy(dst, NULL) is UB.
	if(txdslot < 0 || txdslot >= 65500 || !txdnames[txdslot]){
		currentTxdName[0] = '\0';
		return;
	}
	strncpy(currentTxdName, txdnames[txdslot], sizeof(currentTxdName) - 1);
	currentTxdName[sizeof(currentTxdName) - 1] = '\0';
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
			// Deprecated alias: texdb.txt spelled this key `stochastic=` before
			// the feature was renamed. Kept so existing user files still load.
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
				if(isdetail >= 0 && isdetail < 100)
					detailTextures[isdetail] = RwTextureRead(filename, nil);
				else
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

void
TexDefsLoadDatabase(void)
{
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
	if(tex == nil || texdbOffset <= 0)
		return &faketexinfo;
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
