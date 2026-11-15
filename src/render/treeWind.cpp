// =============================================================================
// treeWind.cpp — FF tree lighting/alpha (TreeAlpha)
//
// PROVENANCE (2026-11-09 motion-family restructure): this TU used to house
// the wind sway math + animation as well. Those halves moved to the motion_fx
// family — motion_fx.cpp (PARENT: animation application + randomness/physics
// family integration, declares the family in motion_fx.h) and wind.cpp (CHILD:
// the CEntity::ModifyMatrixForTreeInWind field math, byte-identical). The
// public TreeWind_* API is unchanged (treeWind.h declarations, motion-family
// definitions) so buildingPipe.cpp / main.cpp callers are untouched.
//
// What REMAINS here is the unrelated TreeAlpha system, byte-identical to the
// W8-2 checkpoint:
//
//   FusionFix's OverrideTreeAlpha / TreeFxText profiles (shaders.ixx:509-520)
//   + the [N5] one-shot lighting-input receipt (emitter stays here, inside
//   TreeAlpha_ApplyAlphaRef — receipts never split from their function).
//
// REGISTER CONTRACT (authoritative — mirrored in treeWind.h; the VS wind rows
// c21/c22/c23 belong to the motion_fx family now, see motion_fx.h):
//
//   PS c1  treeAlpha = { alphamul, alphaRef, profile, enable }       (this TU)
//   PS c24 ambient   = buildingAmbient                               reused here
//
//   PS c22/c23 stay owned by pipeUploadPBR (PS constant space — no overlap).
// =============================================================================
#include "skygfx.h"
#include "treeWind.h"

// ---------------------------------------------------------------------------
// Cross-TU externs
// ---------------------------------------------------------------------------
extern RwRGBAReal buildingAmbient;	// buildingPipe.cpp — ambient for PS c24

// ---------------------------------------------------------------------------
// Handles / knobs
// ---------------------------------------------------------------------------
// Loaded ONLY by the deferred CreateShaders line (pipelinecommon.cpp + the
// resource IDs — see the checkpoint's DEFERRED DIFFS). NULL until then, which
// makes every TreeAlpha_Active() consumer fall through to today's behaviour.
void *treeAlphaPS;

// FF profiles (FusionFix shaders.ixx:509-520). Defaults are the exact
// no-op state: alphamul 1.0 leaves ALPHAREF untouched and profile 0 leaves the
// PS output alone, so the feature is inert even after the CSO load lands.
float TreeAlpha_alphamul = 1.0f;	// OverrideTreeAlpha / TreeFxText multiplier
int   TreeAlpha_profile  = 0;		// 0=PC (ePC), 1=PC+ (ePCPlus), 2=Console (eConsole)
bool  TreeAlpha_enable   = true;	// master switch (INI wiring deferred)

// ---------------------------------------------------------------------------
// Tree alpha — FF OverrideTreeAlpha / TreeFxText profiles
// ---------------------------------------------------------------------------
static bool s_alphaApplied = false;
static int  s_alphaBaseRef = 1;

static bool
treeAlpha_isAlphaActive(RpAtomic *atomic)
{
	if(!TreeAlpha_enable) return false;
	if(atomic == NULL) return false;
	return TreeWind_IsTreeAtomic(atomic);
}

bool
TreeAlpha_Active(RpAtomic *atomic)
{
	// bind gate: alpha-profile gate PLUS the CSO actually being loaded.
	// AUDIT REFINEMENT: plus "the profile actually needs the PS".
	//   profile 0/1 (PC/PC+) differ only by TreeAlpha_alphamul, which the
	//   CPU side (TreeAlpha_ApplyAlphaRef) implements alone, in EVERY pipe,
	//   with no PS at all. Binding the PS there would only duplicate the
	//   clip and cost the tree its detail/stochastic PS
	//   (buildingPipe_selectPS). So the PS is bound for Console (profile 2,
	//   lighting) only; PC/PC+ stay CPU-only. Same effect, zero regression
	//   on the default profile (0 => never binds => inert).
	if(!treeAlphaPS) return false;
	if(TreeAlpha_profile < 2) return false;
	return treeAlpha_isAlphaActive(atomic);
}

// INI/profile setter (wiring deferred to main.cpp — see the checkpoint's
// DEFERRED DIFFS). fx: 0=Off 1=PC 2=PC+ 3=Console. Values mirror FusionFix's
// TreeFxText (shaders.ixx:509-520): PC alphamul 0.625, PC+ 1.0, Console 4.0.
// Off/defaults keep {1.0, 0} = exact no-op: ALPHAREF untouched, PS never
// bound, lighting pass-through — inert until an INI/menu writes a real value.
void
TreeAlpha_SetProfile(int fx)
{
	switch(fx){
	case 1:	// PC — stricter cutout (FF ePC)
		TreeAlpha_profile  = 0;
		TreeAlpha_alphamul = 0.625f;
		break;
	case 2:	// PC+ — today's threshold (FF ePCPlus)
		TreeAlpha_profile  = 1;
		TreeAlpha_alphamul = 1.0f;
		break;
	case 3:	// Console — dense canopy, flat/bright lighting (FF eConsole)
		TreeAlpha_profile  = 2;
		TreeAlpha_alphamul = 4.0f;
		break;
	default:// Off (and anything unknown)
		TreeAlpha_profile  = 0;
		TreeAlpha_alphamul = 1.0f;
		break;
	}
}

// main.cpp grassRenderCallback convention (main.cpp:747-777): pre-seeded
// snapshot, sanitised range, restored on exit. The caller has already snapshotted
// `baseRef` through buildingPipe_saveRenderState, so the restore path here only
// has to undo what THIS function pushed.
//
// Scaling direction: FF gates on `saturate(tex.a * alphamul) >= ref`. The PS
// emits the unscaled alpha (so the FFP blend path keeps today's translucency)
// and the CPU moves the fixed-function ref instead — same inequality, one
// threshold, both halves agreeing (see TreeAlpha.hlsl).
void
TreeAlpha_ApplyAlphaRef(RpAtomic *atomic, int baseRef)
{
	s_alphaApplied = false;
	if(!treeAlpha_isAlphaActive(atomic)) return;

	// N5 one-shot lighting-input receipt (first tree atomic of the session).
	// This is the ONE treeWind entry point that runs per tree atomic in EVERY
	// building cb (PBR included — buildingPipe PBR :3010), so it is the
	// cheapest place to discriminate the three near-black-foliage candidates
	// AT THE DRAW SITE instead of inferring them from postfx proxies:
	//   (a) missing/black prelight  -> prelit=0, or pre0 rgb == 0
	//       (PBR VS is prelight-dominant: buildingPBRVS kPrelightDominance=1.0,
	//        so a zero prelight leaves only ambient*surfAmb*0.10 ~= 0.005)
	//   (b) dark material colour    -> mat rgb small + mod=1 (pipeUploadMatCol
	//       multiplies the whole VS output)
	//   (c) crushed ambient/surface -> amb=(...) from buildingAmbient
	//       (GetTimecycleAmbientPBR, floor applied) and surf.x (surfAmb)
	// Read-only, one-shot (symptom is persistent), so the hot path pays a
	// single predictable branch after the first tree.
	static bool s_n5ReceiptDone = false;
	if(!s_n5ReceiptDone){
		s_n5ReceiptDone = true;
		RpGeometry *g = atomic->geometry;
		const RwRGBA *pre = (g && (g->flags & rpGEOMETRYPRELIT))
			? RpGeometryGetPreLightColors(g) : NULL;
		RpMaterial *m = (g && g->matList.numMaterials > 0)
			? g->matList.materials[0] : NULL;
		dbglog("[N5] tree lighting inputs: geom=%p prelit=%d pre0=(%u,%u,%u) "
		       "mat=(%u,%u,%u,%u) mod=%d light=%d surf=(%.2f,%.2f,%.2f) "
		       "amb=(%.3f,%.3f,%.3f)",
			(void*)g, pre ? 1 : 0,
			pre ? pre[0].red : 0u, pre ? pre[0].green : 0u, pre ? pre[0].blue : 0u,
			m ? m->color.red : 0u, m ? m->color.green : 0u,
			m ? m->color.blue : 0u, m ? m->color.alpha : 0u,
			(g && (g->flags & rpGEOMETRYMODULATEMATERIALCOLOR)) ? 1 : 0,
			(g && (g->flags & rpGEOMETRYLIGHT)) ? 1 : 0,
			m ? m->surfaceProps.ambient : 0.0f,
			m ? m->surfaceProps.specular : 0.0f,
			m ? m->surfaceProps.diffuse : 0.0f,
			buildingAmbient.red, buildingAmbient.green, buildingAmbient.blue);
	}

	float mul = TreeAlpha_alphamul;
	if(!(mul > 0.0f)) mul = 1.0f;
	if(mul == 1.0f){				// exact no-op: nothing to push, nothing to undo
		s_alphaBaseRef = baseRef;
		return;
	}

	float scaled = (float)baseRef / mul;
	int ref = (int)(scaled + 0.5f);
	if(ref < 1) ref = 1;			// ref==0 + GREATEREQUAL accepts EVERY texel
	if(ref > 255) ref = 255;
	// AUDIT FIX: record the base ref BEFORE the "nothing to push" early-out
	// (the surviving code returned first, leaving s_alphaBaseRef stale —
	// TreeAlpha_Upload would then ship the PREVIOUS base ref into PS c1.y
	// and the clip gate would compare against the wrong threshold; hit when
	// rounding lands ref == baseRef, e.g. baseRef 1 with any alphamul).
	s_alphaBaseRef = baseRef;
	if(ref == baseRef) return;		// rounded back to the base: nothing to push

	s_alphaApplied = true;

	// L1 (rw cache) -> L2 (driver cache) -> L3 (raw device).
	// AUDIT FIX: the surviving comment claimed "no raw L3 write here" while
	// the code below DID raw-write D3DRS_ALPHAREF — the comment and the code
	// disagreed. The code is the correct half: the L2 flush is applied[]-
	// gated, so a device sitting behind an unchanged cache would keep the
	// base ref for the whole callback without this write. The raw write is
	// safe ONLY because the pairing is unconditional — every callback that
	// calls Apply also calls Restore (see treeWind.h), so the device always
	// lands back on baseRef even when no draw ran in between.
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)ref);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)ref);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ALPHAREF, (DWORD)ref);
}

void
TreeAlpha_RestoreAlphaRef(void)
{
	if(!s_alphaApplied) return;
	s_alphaApplied = false;
	int ref = s_alphaBaseRef;
	if(ref < 1) ref = 1;
	if(ref > 255) ref = 255;
	// Symmetric push — the raw L3 write is safe ONLY because this restore is
	// unconditional (every callback that applied also restores), so a device
	// whose applied[] was never advanced still gets the correct value.
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)ref);
	RwD3D9SetRenderState(D3DRS_ALPHAREF, (RwUInt32)ref);
	if(d3d9device)
		d3d9device->SetRenderState(D3DRS_ALPHAREF, (DWORD)ref);
}

void
TreeAlpha_Upload(void)
{
	if(!treeAlphaPS) return;

	float c1[4];
	c1[0] = TreeAlpha_alphamul > 0.0f ? TreeAlpha_alphamul : 1.0f;
	// The PS clip gate uses the ORIGINAL ref: it scales the alpha, the CPU
	// scaled the FFP ref, both must compare against the same threshold.
	c1[1] = (float)s_alphaBaseRef * (1.0f/255.0f);
	c1[2] = (float)TreeAlpha_profile;
	c1[3] = 1.0f;
	RwD3D9SetPixelShaderConstant(1, c1, 1);

	float amb[4] = { buildingAmbient.red, buildingAmbient.green,
	                 buildingAmbient.blue, 0.0f };
	RwD3D9SetPixelShaderConstant(24, amb, 1);
}
