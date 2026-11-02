// postfx_underwater.cpp — W7-D lane of the 8-lane burst: underwater volume
// fog + subsurface scatter (Ultra tier). Recipe: docs/plans/water-refs.md:122
//     fog = exp2(-fogDensity.w · volDepth / max(V·Front, 1e-7))
//     SSS = scale 2.0 · EdotL² · height
//
// NEW, self-contained module. It owns no existing file and edits none: the
// only wiring it needs is deferred to the orchestrator (exact hunks in
// .serena/agent-reports/w78-underwater-fog.md):
//   1. postfx.cpp — #include "postfx_underwater.h" + one call
//      PostFX_UnderwaterTick() in ColourFilter_switch, right after the
//      pre-tonemap fx group's UpdateFrontBuffer() (before the
//      colorFilterEnable bypass toggle ~line 1384 — so the pass applies to
//      BOTH the bypass path and the colour-filter switch path).
//   2. resources/resource.h + Resource.rc — IDR_WATER_UNDERWATER 276 ->
//      cso/Water_Underwater.cso (fast_build.py auto-compiles
//      shaders/ps/Water_Underwater.hlsl, ps_3_0, entry "main"; no build-tool
//      diff needed — verified NOT in any exclusion list).
//   3. build/skygfx.vcxproj (+ .filters) — ClCompile entry
//      ..\src\render\postfx_underwater.cpp after the postfx_utils.cpp entry
//      (~line 191). Without it this TU never compiles, so item 1 becomes an
//      unresolved external at link time.
// Until all three land this translation unit is dead code (nothing calls it).
//
// DEFAULT-OFF SAFETY (objective 4): the tick's first statements are the
// Config gate — `config->waterUnderwaterFog > 0`, NaN-safe through the
// `!(x > 0)` form — then Scene.camera/water-level tests. Until those pass the
// pass touches ZERO render state, allocates nothing, makes zero D3D calls and
// zero game-function calls. The INI key already exists (main.cpp:3537,
// debugmenu_ui.cpp:989) and had ZERO consumers before this module; set it to
// 0 to keep the feature off entirely.
//
// UNDERWATER DETECTION (objective 1): camera world position from
// Scene.camera's LTM is compared against the water surface height looked up
// in the game's own data/water.dat (parsed once: 301 quads + 6 triangles,
// format verified against the live file — per vertex: x y z flowX flowY
// bigWaves smallWaves, then a trailing flags int). No unverified game
// address is called: CWeather::UnderWaterness (0xC8132C) is deliberately NOT
// the trigger — it is a smoothed blend factor with unknown writers; the task
// requires a camera-vs-water-level test, which this implements literally and
// crash-free.
//
// DEPTH SOURCE: g_ssaoDepthTex (postfx_ssao.cpp — aliases depthhook's live
// INTZ texture, available whether or not SSAO is enabled) sampled on s1 after
// DepthHook_Suspend(), exactly like DrawHeightFog (postfx.cpp:3028). No new
// globals are added to postfx_ssao.cpp; only its existing externs are read.
//
// RENDER-STATE DOCTRINE (three layers, mirrors DrawHeightFog verbatim):
//   layer 1  SaveRawGeomStates / RestoreRawGeomStates  (BEFORE Suspend,
//            restored AFTER ImmediateModeRenderStatesReStore so the raw
//            restore wins on any overlap — skygfx.h:1266)
//   layer 2  ImmediateModeRenderStatesStore / Set / ReStore (RW cache + the
//            raw D3D9 mirror inside the vanilla helper)
//   layer 3  PS constant snapshot c0-c7 (shared by the whole postfx chain)
// plus the DepthHook_Suspend/Restore pairing, the overrideIm2dPixelShader
// reset, and an SEH __except path that restores everything the __try set.

#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "depthhook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Shared shader loader (pipelinecommon.cpp) — the same chokepoint
// CreateShaders uses for every embedded shader (postfx_newfx.cpp forward-declares
// it identically).
void makePS(int res, void **sh);

// Resource IDs 260-275 are reserved for the post-effect shader packs; 276 is
// the W7-D reservation. resources/resource.h + Resource.rc are NOT owned by
// this lane (deferred diff), so fall back to the reserved numeric — the file
// compiles and binds whether or not the canonical define has landed.
#ifndef IDR_WATER_UNDERWATER
#define IDR_WATER_UNDERWATER      276
#endif

// ---------------------------------------------------------------------------
// Tunables (docs/plans/water-refs.md:122 recipe; debug-menu slider range for
// waterUnderwaterFog is 0..4, default 1.0 from main.cpp:3537)
// ---------------------------------------------------------------------------
// Beer-Lambert extinction at waterUnderwaterFog == 1.0: 0.085/m ~= 11 m of
// visibility (fog ~0.56 at 10 m) — SA's vanilla murky-green underwater read.
static const float kUWDensityPerUnit = 0.085f;
static const float kUWDensityMax     = 0.6f;    // hard cap: never a full wall
// SSS: the recipe's scale 2.0, damped by a gain so full forward-scatter peaks
// at ~60% of the light colour. The old 0.08 gain capped it at ~16%, which read
// as flat green fog instead of in-scattered light (still short of a white-out).
static const float kUWSSSScale       = 2.0f;
static const float kUWSSSGain        = 0.30f;
static const float kUWSSSDepthRef    = 10.0f;   // metres: SSS height ramp
static const float kUWMaxFog         = 0.98f;   // keep a hint of scene at distance
static const float kUWSurfaceEps     = 0.25f;   // camera must sit 25 cm under the surface
// False-positive guard: the ONLY water.dat plane above normal world height is
// the single interior-cell plane (line 295: 20x12 m patch, XY 1270..1290 x
// -780..-768, z=1082.73). An OUTDOOR camera in that XY patch would compute
// camDepth ~= 1040 m and full-screen fog. Legit dives top out far lower
// (deepest ocean floor ~ -80 m, highest lake surface 112 m — water-refs:166),
// so reject any depth beyond 200 m. Interior-cell dives still pass (their
// camDepth is a few metres below 1082.73).
static const float kUWMaxDetectDepth = 200.0f;
static const float kUWColorDepth     = 20.0f;   // deep->shallow colour lerp ref depth (m):
                                                // 20 keeps the deep palette over real dive
                                                // depths; 12 hit the shallow green end far
                                                // too early and read as flat fog
static const float kUWOceanLevel     = 0.0f;    // general ocean outside ±3000 (water-refs:167)
static const float kUWWorldExtent    = 3000.0f; // SA water.dat coverage (water-refs:167)

// INI defaults (main.cpp:3540-3545) used when the keys are all-zero/unset.
static const float kUWDeepDefault[3]    = { 0.01f, 0.03f, 0.08f };
static const float kUWShallowDefault[3] = { 0.13f, 0.45f, 0.55f };

// ---------------------------------------------------------------------------
// Water surface probe — data/water.dat, parsed once (lazy).
// Format (verified against E:\games\gtasa_skygfx_plus\data\water.dat):
//   line 1              "processed"
//   301 lines, 29 ints  4 vertices × (x y z flowX flowY bigWaves smallWaves) + flags
//   6 lines,   22 ints  3 vertices × (…) + flags
// Vertices are stored axis-ordered (x1,y1),(x2,y1),(x1,y2),(x2,y2) for quads,
// but the lookup uses a convex fan (0,i,i+1) of barycentric triangles so any
// convex winding works, and interpolated Z handles sloped planes.
// ---------------------------------------------------------------------------
#define UW_MAX_PLANES 512

struct UWPlane
{
	float x[4], y[4], z[4];
	int   nv;                 // 3 or 4
};

static UWPlane s_uwPlanes[UW_MAX_PLANES];
static int     s_uwNumPlanes   = 0;
static bool    s_uwProbeTried  = false;   // one-shot
static bool    s_uwProbeOk     = false;
static float   s_uwMinX = 0.0f, s_uwMaxX = 0.0f, s_uwMinY = 0.0f, s_uwMaxY = 0.0f;

static void *s_uwPS          = NULL;
static bool  s_uwLoadTried   = false;     // makePS logs on every failure
static bool  s_uwActive      = false;     // last tick actually drew

// Point-in-triangle with barycentric Z interpolation. Degenerate triangles
// are rejected BEFORE the divide (never divide unguarded).
static bool
UWTriLevel(const float *tx, const float *ty, const float *tz,
           float px, float py, float *outLevel)
{
	float d = (ty[1]-ty[2])*(tx[0]-tx[2]) + (tx[2]-tx[1])*(ty[0]-ty[2]);
	if(fabsf(d) < 1e-7f)
		return false;
	float a = ((ty[1]-ty[2])*(px-tx[2]) + (tx[2]-tx[1])*(py-ty[2])) / d;
	float b = ((ty[2]-ty[0])*(px-tx[2]) + (tx[0]-tx[2])*(py-ty[2])) / d;
	float c = 1.0f - a - b;
	const float tol = -1e-4f;   // edge-inclusive: shared edges must not fall through
	if(a < tol || b < tol || c < tol)
		return false;
	*outLevel = a*tz[0] + b*tz[1] + c*tz[2];
	return true;
}

// Bbox reject + convex fan; writes the interpolated surface height.
static bool
UWPlaneLevel(const UWPlane &pl, float px, float py, float *outLevel)
{
	float minX = pl.x[0], maxX = pl.x[0], minY = pl.y[0], maxY = pl.y[0];
	for(int i = 1; i < pl.nv; i++){
		if(pl.x[i] < minX) minX = pl.x[i];
		if(pl.x[i] > maxX) maxX = pl.x[i];
		if(pl.y[i] < minY) minY = pl.y[i];
		if(pl.y[i] > maxY) maxY = pl.y[i];
	}
	if(px < minX || px > maxX || py < minY || py > maxY)
		return false;

	float triX[3], triY[3], triZ[3];
	for(int i = 1; i + 1 < pl.nv; i++){
		triX[0] = pl.x[0]; triY[0] = pl.y[0]; triZ[0] = pl.z[0];
		triX[1] = pl.x[i]; triY[1] = pl.y[i]; triZ[1] = pl.z[i];
		triX[2] = pl.x[i+1]; triY[2] = pl.y[i+1]; triZ[2] = pl.z[i+1];
		if(UWTriLevel(triX, triY, triZ, px, py, outLevel))
			return true;
	}
	return false;
}

// One-shot lazy parse. Failure = probe inert (the whole pass stays off, which
// is the safe default: no water.dat, no effect — never a false trigger).
static void
UWLoadWaterProbe(void)
{
	if(s_uwProbeTried)
		return;
	s_uwProbeTried = true;

	FILE *f = fopen("data/water.dat", "r");   // CWD = game dir (same assumption as main.cpp:103)
	if(!f){
		dbglog("[UW] water.dat open FAILED — underwater fog stays inert");
		return;
	}

	char line[2048];
	bool firstLine = true;
	while(fgets(line, sizeof(line), f)){
		if(firstLine){          // "processed" marker
			firstLine = false;
			continue;
		}
		// Parse up to 29 numeric fields with strtof (flowX/Y, wave amps and
		// the trailing flags int all parse as numbers; only vertex x/y/z are kept).
		float nums[32];
		int   cnt = 0;
		const char *p = line;
		while(cnt < 32){
			while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
			if(*p == '\0' || *p == '#')
				break;
			char *end = NULL;
			nums[cnt] = strtof(p, &end);
			if(end == p)
				break;
			cnt++; p = end;
		}
		if(cnt < 22)
			continue;                                   // 3 verts × 7 + flags minimum
		int nv = (cnt - 1) / 7;                        // trailing int = flags
		if(nv != 3 && nv != 4)
			continue;
		if(s_uwNumPlanes >= UW_MAX_PLANES)
			break;
		UWPlane &pl = s_uwPlanes[s_uwNumPlanes];
		for(int i = 0; i < nv; i++){
			pl.x[i] = nums[i*7 + 0];
			pl.y[i] = nums[i*7 + 1];
			pl.z[i] = nums[i*7 + 2];
		}
		for(int i = nv; i < 4; i++)
			pl.x[i] = pl.y[i] = pl.z[i] = 0.0f;
		pl.nv = nv;
		if(s_uwNumPlanes == 0){
			s_uwMinX = s_uwMaxX = pl.x[0];
			s_uwMinY = s_uwMaxY = pl.y[0];
		}
		for(int i = 0; i < nv; i++){
			if(pl.x[i] < s_uwMinX) s_uwMinX = pl.x[i];
			if(pl.x[i] > s_uwMaxX) s_uwMaxX = pl.x[i];
			if(pl.y[i] < s_uwMinY) s_uwMinY = pl.y[i];
			if(pl.y[i] > s_uwMaxY) s_uwMaxY = pl.y[i];
		}
		s_uwNumPlanes++;
	}
	fclose(f);

	s_uwProbeOk = s_uwNumPlanes > 0;
	dbglog("[UW] water.dat: %d planes (ok=%d bbox %.0f..%.0f / %.0f..%.0f)",
		s_uwNumPlanes, s_uwProbeOk, s_uwMinX, s_uwMaxX, s_uwMinY, s_uwMaxY);
}

// Water surface height at a world XY. False = no water here.
static bool
UWGetWaterLevel(float px, float py, float *outLevel)
{
	if(!s_uwProbeOk)
		return false;
	// General ocean beyond the mapped quads (water-refs.md:167: the game
	// renders an infinite ocean plane outside ±3000).
	if(px < -kUWWorldExtent || px > kUWWorldExtent ||
	   py < -kUWWorldExtent || py > kUWWorldExtent){
		*outLevel = kUWOceanLevel;
		return true;
	}
	// Whole-map bbox reject before the per-plane loop.
	if(px < s_uwMinX || px > s_uwMaxX || py < s_uwMinY || py > s_uwMaxY)
		return false;
	for(int i = 0; i < s_uwNumPlanes; i++)
		if(UWPlaneLevel(s_uwPlanes[i], px, py, outLevel))
			return true;
	return false;
}

// Lazy one-shot shader load (postfx_newfx.cpp pattern).
static void
UWLoadShader(void)
{
	if(s_uwLoadTried)
		return;
	s_uwLoadTried = true;
	makePS(IDR_WATER_UNDERWATER, &s_uwPS);
	dbglog("[UW] Water_Underwater PS res=%d sh=%p", IDR_WATER_UNDERWATER, s_uwPS);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool
PostFX_UnderwaterActive(void)
{
	return s_uwActive;
}

void
PostFX_UnderwaterInit(void)
{
	UWLoadWaterProbe();
	UWLoadShader();
}

// Device-Reset seam (depthhook.cpp's post-Reset block calls this via a
// function-local extern): drop the cached PS handle and the one-shot load
// latch so the next tick re-creates it through UWLoadShader/makePS. A failed
// reload leaves s_uwPS NULL and the tick hits its existing NULL skip —
// fail-open, no crash. The water.dat probe is NOT device state and keeps its
// one-shot.
void
UWResetShaderLatches(void)
{
	s_uwPS = NULL;
	s_uwLoadTried = false;
}

void
PostFX_UnderwaterTick(void)
{
	s_uwActive = false;

	// ---- Gate 1: Config (default-off; INI key waterUnderwaterFog, 0..4) ----
	// `!(x > 0)` also rejects NaN. Zero cost from here down when disabled.
	if(config == NULL)
		return;
	float strength = config->waterUnderwaterFog;
	if(!(strength > 0.0f))
		return;
	// Clamp to the debug-menu slider range (debugmenu_ui.cpp:989, 0..4):
	// rejects NaN (already), +Inf and hand-edited outliers before they can
	// reach any shader constant.
	if(strength > 4.0f)
		strength = 4.0f;
	if(IsGameInMenuOrPaused())
		return;

	UWLoadWaterProbe();
	UWLoadShader();
	if(s_uwPS == NULL)
		return;                       // shader missing (resource diff not landed)

	// ---- Gate 2: camera vs water level ----
	RwCamera *cam = Scene.camera;
	if(!cam)
		return;
	RwRaster *camRas = RwCameraGetRaster(cam);
	if(!camRas)
		return;
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1)
		return;
	RwFrame *camFrame = RwCameraGetFrame(cam);
	if(!camFrame)
		return;
	RwMatrix *camLTM = RwFrameGetLTM(camFrame);
	if(!camLTM)
		return;

	float camX = camLTM->pos.x;
	float camY = camLTM->pos.y;
	float camZ = camLTM->pos.z;

	float waterLevel = 0.0f;
	if(!UWGetWaterLevel(camX, camY, &waterLevel))
		return;                       // no water above this point
	float camDepth = waterLevel - camZ;
	if(camDepth <= kUWSurfaceEps)
		return;                       // camera at/above the surface — inert
	if(camDepth > kUWMaxDetectDepth)
		return;                       // overhead-interior-plane false positive

	if(!CPostEffects::pRasterFrontBuffer)
		return;
	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	if(!g_ssaoDepthTex)
		return;                       // no live INTZ depth — fall back to no fog

	// ---- Effect parameters (all sanitized; nothing unbounded reaches the PS) ----
	float density = kUWDensityPerUnit * strength;
	if(density > kUWDensityMax)
		density = kUWDensityMax;
	float sssScale = kUWSSSScale * kUWSSSGain * strength;

	// Fog colour: config waterDeep* -> waterShallow* lerped by camera depth
	// (both keys exist today: main.cpp:3540-3545, docs/Water Rendering.md).
	float deep[3]    = { config->waterDeepR,    config->waterDeepG,    config->waterDeepB    };
	float shallow[3] = { config->waterShallowR, config->waterShallowG, config->waterShallowB };
	if(deep[0]+deep[1]+deep[2] < 0.001f){
		deep[0] = kUWDeepDefault[0]; deep[1] = kUWDeepDefault[1]; deep[2] = kUWDeepDefault[2];
	}
	if(shallow[0]+shallow[1]+shallow[2] < 0.001f){
		shallow[0] = kUWShallowDefault[0]; shallow[1] = kUWShallowDefault[1]; shallow[2] = kUWShallowDefault[2];
	}
	float mix = camDepth / kUWColorDepth;
	if(mix > 1.0f) mix = 1.0f;
	if(mix < 0.0f) mix = 0.0f;
	float fogR = deep[0] + (shallow[0]-deep[0])*mix;
	float fogG = deep[1] + (shallow[1]-deep[1])*mix;
	float fogB = deep[2] + (shallow[2]-deep[2])*mix;

	// SSS in-scatter tint: timecycle directional light (already 0..1 in
	// CColourSet — waterPipe.cpp:476). Zero at night = no forward scatter.
	CColourSet &tc = CTimeCycle__m_CurrentColours;
	float lightR = tc.directionalR;
	float lightG = tc.directionalG;
	float lightB = tc.directionalB;

	if(dbglog_throttle("uw_tick"))
		dbglog("[UW] underwater ON: cam=(%.1f,%.1f,%.1f) level=%.2f depth=%.2f str=%.2f "
		       "density=%.4f fog=(%.3f,%.3f,%.3f) light=(%.2f,%.2f,%.2f)",
			camX, camY, camZ, waterLevel, camDepth, strength,
			density, fogR, fogG, fogB, lightR, lightG, lightB);

	// PS constants c0-c7 are shared by the whole postfx chain — snapshot them
	// so this pass cannot leak state into later same-frame/next-frame passes
	// (DrawHeightFog does exactly this).
	float savedPSConsts[4 * 8];
	bool savedPSConstsValid = false;

	// Declared before __try so the __except path can restore them too.
	DWORD rawGeom[9];
	bool rawGeomSaved = false;
	float sunD[3] = { 0.0f, 0.0f, 1.0f };
	float sunView[4];
	float fbU = 1.0f, fbV = 1.0f;

	__try {
		// Sun direction mapped into VIEW space (dot is basis-invariant, so the
		// shader never needs the world basis).
		GetSunDirection(sunD[0], sunD[1], sunD[2]);
		sunView[0] = sunD[0]*camLTM->right.x + sunD[1]*camLTM->right.y + sunD[2]*camLTM->right.z;
		sunView[1] = sunD[0]*camLTM->up.x    + sunD[1]*camLTM->up.y    + sunD[2]*camLTM->up.z;
		sunView[2] = sunD[0]*camLTM->at.x    + sunD[1]*camLTM->at.y    + sunD[2]*camLTM->at.z;
		sunView[3] = waterLevel;

		// s0 is the padded 2048² front buffer while the camera raster (and the
		// INTZ depth on s1) are screen-sized — scale the scene fetch only.
		fbU = (float)w / max((float)CPostEffects::pRasterFrontBuffer->width, 1.0f);
		fbV = (float)h / max((float)CPostEffects::pRasterFrontBuffer->height, 1.0f);

		// Save raw geometry states BEFORE Suspend (it writes raw ZENABLE)
		rawGeomSaved = SaveRawGeomStates(rawGeom);

		// Suspend depth hook so g_ssaoDepthTex can be sampled on s1
		DepthHook_Suspend();
		savedPSConstsValid = SUCCEEDED(dev->GetPixelShaderConstantF(0, savedPSConsts, 8));

		CPostEffects::ImmediateModeRenderStatesStore();
		CPostEffects::ImmediateModeRenderStatesSet();
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

		// s0: scene colour (front buffer), s1: live INTZ depth
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)CPostEffects::pRasterFrontBuffer);
		dev->SetTexture(1, g_ssaoDepthTex);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		// c0: fogParams (density, sssScale, sssDepthRef, maxFog)
		float c0[4] = { density, sssScale, kUWSSSDepthRef, kUWMaxFog };
		RwD3D9SetPixelShaderConstant(0, c0, 1);

		// c1: absorption colour (deep -> shallow lerp above)
		float c1[4] = { fogR, fogG, fogB, 0.0f };
		RwD3D9SetPixelShaderConstant(1, c1, 1);

		// c2: projInfo (SSAO/HeightFog convention)
		RwCamera *cam2 = Scene.camera;
		float n = cam2->nearPlane;
		float f = cam2->farPlane;
		float c2[4] = {
			cam2->recipViewWindow.x,
			cam2->recipViewWindow.y,
			-n * f / max(f - n, 1e-7f),
			f / max(f - n, 1e-7f)
		};
		RwD3D9SetPixelShaderConstant(2, c2, 1);

		// c3: front-buffer UV scale
		float c3[4] = { fbU, fbV, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(3, c3, 1);

		// c4: camera world position + depth below the surface (> 0 here)
		float c4[4] = { camX, camY, camZ, camDepth };
		RwD3D9SetPixelShaderConstant(4, c4, 1);

		// c5: camera axes' world-Z row — doubles as world-up in view space (V·Front)
		float c5[4] = { camLTM->right.z, camLTM->up.z, camLTM->at.z, 0.0f };
		RwD3D9SetPixelShaderConstant(5, c5, 1);

		// c6: sun direction (view space) + water surface level
		RwD3D9SetPixelShaderConstant(6, sunView, 1);

		// c7: SSS in-scatter tint (timecycle directional light)
		float c7[4] = { lightR, lightG, lightB, 0.0f };
		RwD3D9SetPixelShaderConstant(7, c7, 1);

		// Fullscreen quad — RT-sized with UV 0..1 (HeightFog convention; the
		// hardcoded 2048² colorfilterVerts misalign screen-sized buffers).
		SetupFullscreenQuad((float)w, (float)h);
		overrideIm2dPixelShader = s_uwPS;
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Restore PS constants c0-c7 saved before the pass
		if(savedPSConstsValid)
			dev->SetPixelShaderConstantF(0, savedPSConsts, 8);

		// Cleanup (mirror DrawHeightFog exactly)
		dev->SetTexture(1, NULL);
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
		RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
		RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
		RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
		RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

		// Restore depth hook (re-binds INTZ as DS, re-enables Z)
		DepthHook_Restore();

		CPostEffects::ImmediateModeRenderStatesReStore();
		if(rawGeomSaved){
			RestoreRawGeomStates(rawGeom);
			rawGeomSaved = false;
		}
		s_uwActive = true;
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset above is skipped on fault

		dbglog("[UW] PostFX_UnderwaterTick CRASHED exception=0x%08X", GetExceptionCode());
		// Nested guard: a device-lost re-fault inside this restore path would
		// otherwise terminate the process (unhandled exception in the handler).
		__try {
			if(savedPSConstsValid)
				dev->SetPixelShaderConstantF(0, savedPSConsts, 8);
			dev->SetTexture(1, NULL);
			DepthHook_Restore(); // restore on bail to keep Suspend/Restore balanced
			CPostEffects::ImmediateModeRenderStatesReStore();
			if(rawGeomSaved)
				RestoreRawGeomStates(rawGeom);
		} __except(EXCEPTION_EXECUTE_HANDLER){
			if(dbglog_throttle("uw_crash2"))
				dbglog("[UW] crash-handler restore fault code=0x%08X", GetExceptionCode());
		}
	}

	// Sync front buffer so whatever runs after this in the chain reads the
	// fogged frame (the call site sits just before the colour-filter switch).
	CPostEffects::UpdateFrontBuffer();
}
