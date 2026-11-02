// postfx_motionblur.cpp - velocity buffer + DrawVelocityBuffer (m0060 split).
// Tier 0 (2026-10-29 motionblur research doc): MotionBlurDtScale() framerate
// normalisation helper (a) + camera-cut reset of the velocity history (b).
#include "skygfx.h"
#include "debug_handl.h"
#include "postfx.h"
#include "depthhook.h"
#include "profiler.h"

// Velocity buffer resources
IDirect3DTexture9 *g_velocityTex = NULL;
static IDirect3DSurface9 *g_velocitySurf = NULL;
static int s_velocityW = 0, s_velocityH = 0;  // cached dims for res-aware recreate
static D3DMATRIX g_prevVPMatrix;
static bool g_prevVPValid = false;

extern float &CTimer__ms_fTimeStep;	// game per-frame timestep, seconds*50 (local extern, grasssys.cpp style)

// Tier 0 (b) cut-reset: previous-frame camera pose (world position + forward
// vector), extracted from the D3D view matrix. Latched at every site where
// g_prevVPMatrix is written, so the cut detector always diffs like-for-like.
static float s_prevCamX = 0, s_prevCamY = 0, s_prevCamZ = 0;
static float s_prevFwdX = 0, s_prevFwdY = 1, s_prevFwdZ = 0;
// R4 fix (perf-check 2026-11-02): camera identity latched with the pose, so a
// skipped secondary-camera update is visible in the throttle receipt.
static RwCamera *s_prevCamPtr = NULL;

void ReleaseVelocityBufferResources(void)
{
	if(g_velocityTex){ g_velocityTex->Release(); g_velocityTex = NULL; }
	if(g_velocitySurf){ g_velocitySurf->Release(); g_velocitySurf = NULL; }
	s_velocityW = 0; s_velocityH = 0;
	g_prevVPValid = false;
}

// Tier 0 (b): clear the velocity texture to the zero-velocity value
// (128,128,0 RGB packed, A=0) WITHOUT running the pass. Used by the
// lazy-create path and the camera-cut reset so consumers (MB s1, SMAA edge
// s3 / temporal s2, SSAO temporal s3) never read stale pre-cut vectors.
// Saves/restores RT0; SetRenderTarget re-homes the viewport to the target
// size and restoring origRT re-homes it back (same pattern as lazy-init).
static void ClearVelocityToZero(IDirect3DDevice9 *dev)
{
	if(!g_velocitySurf)
		return;
	IDirect3DSurface9 *rt0 = NULL;
	if(SUCCEEDED(dev->GetRenderTarget(0, &rt0))){
		dev->SetRenderTarget(0, g_velocitySurf);
		dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 128, 128, 0), 1.0f, 0);
		dev->SetRenderTarget(0, rt0);
		rt0->Release();
	}
}

// Tier 0 (a): framerate normalisation for motion blur. CTimer__ms_fTimeStep
// is the game's per-frame timestep in seconds*50 (1.0 at 50 FPS — softbody.cpp
// and the main.cpp ragdoll update divide it by 50 for seconds). Returns the
// multiplier that converts a PER-FRAME delta into its 60 FPS-equivalent:
// (1/60)/dt — exactly 1.0 at 60 FPS, 0.5 at 30 FPS, 2.4 at 144 FPS. Callers
// with quadratic deltas (1-cos(dtheta) rotation) must square it. dt is
// epsilon-guarded and clamped to [1e-4, 0.1] s per the Tier 0 spec; outside
// that range the scale neutralises to 1.0 (paused reads 0 / first frame) or
// bottoms out at the 0.1 s hitch clamp instead of exploding.
float MotionBlurDtScale(void)
{
	float ts = CTimer__ms_fTimeStep;
	// One-shot units breadcrumb: ~0.83 at 60 FPS confirms the seconds*50
	// convention; ~16.7 would mean milliseconds (and the /50 below — shared
	// with the softbody/ragdoll physics — would need revisiting everywhere).
	static bool s_dtUnitsLogged = false;
	if(!s_dtUnitsLogged){
		s_dtUnitsLogged = true;
		dbglog("[PostFX] MotionBlurDtScale: raw CTimer__ms_fTimeStep=%.4f (expect ~0.833 at 60 FPS for seconds*50 units)", ts);
	}
	float dt = ts * (1.0f/50.0f);
	if(dt < 1e-4f)
		return 1.0f;		// paused / timescale ~0 / first frame: neutral, no scaling
	dt = min(dt, 0.1f);		// hitch clamp: scale bottoms out at ~1/6, never 0
	return (1.0f/60.0f) / dt;
}

// Tier 0 (b): single latch point for the velocity history — prev VP matrix +
// the camera pose the cut detector diffs against. Called from exactly the
// three sites where the history resets or advances: first frame, cut reset,
// end of a rendered pass.
static void VelocityHistoryLatch(const D3DMATRIX &vp, RwCamera *cam, float cx, float cy, float cz, float fx, float fy, float fz)
{
	memcpy(&g_prevVPMatrix, &vp, sizeof(D3DMATRIX));
	s_prevCamX = cx; s_prevCamY = cy; s_prevCamZ = cz;
	s_prevFwdX = fx; s_prevFwdY = fy; s_prevFwdZ = fz;
	s_prevCamPtr = cam;
	g_prevVPValid = true;
}

// R4 fix (perf-check 2026-11-02 findings, R4): secondary-camera view-matrix
// leak guard for the cut detector. The D3D view matrix the pose at :198-201
// is derived from can still hold a MIRROR / vehicle-cinematic camera's
// transform when DrawVelocityBuffer runs (secondary passes leave their device
// transforms behind). Latching that pose poisons the NEXT real frame: it
// diffs the main camera against the secondary pose -> huge posDelta -> false
// CUT -> velocity clear = rotor/vehicle smear (11.8k false cuts/session;
// posDelta clusters at fixed 19.5/6.0/5.3 with fwdDot 0.98 = pure translation
// jumps between fixed secondary-camera rigs). Validate the derived position
// against the MAIN render camera's LTM position (Scene.camera): RW builds the
// D3D view matrix from that same frame, so a main-camera matrix matches to
// float precision, while a leaked secondary matrix lands units away (smallest
// observed false cluster = 4.3 units). tol=0.5 sits far below that and far
// above float rounding. Genuine cuts (teleport / interior switch) keep
// detecting: the derived pose still matches the (moved) main camera LTM.
// refX/Y/Z optionally receive the reference LTM position for the receipt.
static bool PoseIsMainCamera(float px, float py, float pz, float tol,
                             float *refX, float *refY, float *refZ)
{
	if(!Scene.camera)
		return false;
	RwFrame *camFrame = RwCameraGetFrame(Scene.camera);
	if(!camFrame)
		return false;
	RwMatrix *ltm = RwFrameGetLTM(camFrame);	// WRAPPER 0x7F0990 (gta.cpp:73)
	if(!ltm)
		return false;
	if(refX) *refX = ltm->pos.x;
	if(refY) *refY = ltm->pos.y;
	if(refZ) *refZ = ltm->pos.z;
	float dx = px - ltm->pos.x, dy = py - ltm->pos.y, dz = pz - ltm->pos.z;
	return (dx*dx + dy*dy + dz*dz) <= tol*tol;
}

void DrawVelocityBuffer(void)
{
	// No PF_SCOPE here (__try in function -> C2712); instrumentation lives in
	// the throttled dbglog lines below + the caller's VelocityBuffer scope.
	if(dbglog_throttle("pf_vel_gate"))
		dbglog("[PF] DrawVelocityBuffer gate: velEnable=%d ps=%p fb=%p depth=%p menu=%d -> %s",
			config ? config->velocityBufferEnable : -1, VelocityReconstruct,
			(void*)CPostEffects::pRasterFrontBuffer, (void*)g_ssaoDepthTex,
			(int)IsGameInMenuOrPaused(),
			(config && config->velocityBufferEnable && VelocityReconstruct &&
			 CPostEffects::pRasterFrontBuffer && !IsGameInMenuOrPaused()) ? "RUN" : "SKIP");
	if(!config->velocityBufferEnable || !VelocityReconstruct)
		return;
	if(!CPostEffects::pRasterFrontBuffer)
		return;
	if(IsGameInMenuOrPaused())
		return;

	IDirect3DDevice9 *dev = d3d9device;
	if(!dev)
		return;
	if(!Scene.camera)
		return;
	RwRaster *camRas = RwCameraGetRaster(Scene.camera);
	if(!camRas)
		return;
	int w = camRas->width;
	int h = camRas->height;
	if(w < 1 || h < 1)
		return;

	// Lazy-init velocity texture (res-aware: recreate if camera resolution changed)
	if(!g_velocityTex || w != s_velocityW || h != s_velocityH){
		if(g_velocityTex){
			if(g_velocitySurf){ g_velocitySurf->Release(); g_velocitySurf = NULL; }
			g_velocityTex->Release();
			g_velocityTex = NULL;
		}
		if(FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_velocityTex, NULL))){
			dbglog("[PostFX] DrawVelocityBuffer: CreateTexture velocity failed");
			config->velocityBufferEnable = 0;
			return;
		}
		g_velocityTex->GetSurfaceLevel(0, &g_velocitySurf);
		if(!g_velocitySurf){
			// GetSurfaceLevel failed: SetRenderTarget(0, NULL) is illegal in
			// D3D9 and the pass's Clear() would then hit the CURRENTLY bound
			// RT — the frame buffer — painting the screen grey. Drop the
			// texture and disable the feature (same treatment as a failed
			// CreateTexture above).
			dbglog("[PostFX] DrawVelocityBuffer: GetSurfaceLevel velocity failed");
			g_velocityTex->Release();
			g_velocityTex = NULL;
			config->velocityBufferEnable = 0;
			return;
		}
		// Define the first-frame content NOW: the VP-latch frame below
		// returns WITHOUT running the render section (its Clear only happens
		// in the full-pass path), so the fresh D3DPOOL_DEFAULT RT would
		// otherwise hold undefined driver garbage while same-frame consumers
		// (SMAA edge s3, SSAO_Temporal s3) sample it. Clear to the zero-
		// velocity value (128,128,0) so the latch breadcrumb below is true.
		ClearVelocityToZero(dev);
		s_velocityW = w;
		s_velocityH = h;
		// One-shot handle proof (log dig 2026-10-17): the NULL velTex=...
		// lines in the dig were all from BEFORE this lazy-create ran — the
		// pass is gated off in menus/pause/cutscenes by design, so the
		// rasters-ready / smaa_vel diags legitimately print NULL until the
		// first eligible frame. Print the handles so the next dig can match
		// them against the smaa_vel / rasters-ready lines directly.
		dbglog("[PostFX] DrawVelocityBuffer: texture created %dx%d tex=%p surf=%p (lazy: first frame past menu/cutscene gate)",
			w, h, (void*)g_velocityTex, (void*)g_velocitySurf);
	}

	// Bail if depth texture is not available — velocity reconstruction needs depth
	if(!g_ssaoDepthTex){
		if(dbglog_throttle("vel_no_depth"))
			dbglog("[PostFX] DrawVelocityBuffer: g_ssaoDepthTex NULL, skipping");
		return;
	}

	// Store current VP matrix for next frame
	D3DMATRIX curView, curProj;
	dev->GetTransform(D3DTS_VIEW, &curView);
	dev->GetTransform(D3DTS_PROJECTION, &curProj);

	// Compute current VP = View * Proj (D3D9 convention)
	D3DMATRIX curVP;
	{
		// Manual 4x4 matrix multiply (avoids D3DX dependency)
		for(int r = 0; r < 4; r++)
			for(int c = 0; c < 4; c++){
				float sum = 0;
				for(int k = 0; k < 4; k++)
					sum += curView.m[r][k] * curProj.m[k][c];
				curVP.m[r][c] = sum;
			}
	}

	// Tier 0 (b): camera pose for the cut detector, from the D3D view matrix
	// (rows 0-2 = right/up/at in world space, row 3 = -R*pos => pos =
	// -transpose(R)*t). Self-consistent with the VP history this file latches,
	// and deliberately VIEW-space rather than a reprojection test: a pure FOV
	// change (sniper scope zoom) reprojects far corners far across the screen
	// but is NOT a cut — reprojection handles FOV correctly via the matrices,
	// so resetting on it would only shimmer SMAA/SSAO temporal history.
	float camPosX = -(curView.m[0][0]*curView.m[3][0] + curView.m[1][0]*curView.m[3][1] + curView.m[2][0]*curView.m[3][2]);
	float camPosY = -(curView.m[0][1]*curView.m[3][0] + curView.m[1][1]*curView.m[3][1] + curView.m[2][1]*curView.m[3][2]);
	float camPosZ = -(curView.m[0][2]*curView.m[3][0] + curView.m[1][2]*curView.m[3][1] + curView.m[2][2]*curView.m[3][2]);
	float fwdX = curView.m[2][0], fwdY = curView.m[2][1], fwdZ = curView.m[2][2];

	// R4 fix: camera-identity gate for everything below that diffs or latches
	// this pose (cut detector, first-frame latch, end-of-pass latch). See the
	// PoseIsMainCamera comment above.
	float refPosX = 0, refPosY = 0, refPosZ = 0;
	bool poseMainCam = PoseIsMainCamera(camPosX, camPosY, camPosZ, 0.5f, &refPosX, &refPosY, &refPosZ);
	if(!poseMainCam && dbglog_throttle("vel_seccam"))
		dbglog("[PF] DrawVelocityBuffer: secondary-camera view matrix on device (derived pos %.2f,%.2f,%.2f vs Scene.camera %p LTM %.2f,%.2f,%.2f; latched cam %p) — cut detector + pose latch SKIPPED this frame",
			camPosX, camPosY, camPosZ, (void*)Scene.camera, refPosX, refPosY, refPosZ, (void*)s_prevCamPtr);

	// Skip first frame (no previous VP yet)
	if(!g_prevVPValid){
		if(poseMainCam){
			VelocityHistoryLatch(curVP, Scene.camera, camPosX, camPosY, camPosZ, fwdX, fwdY, fwdZ);
			// One-time breadcrumb (log dig 2026-10-17): this silent return used to
			// be invisible — the texture EXISTS at this point (created above), but
			// its content is still the Clear() value (zero velocity) for this
			// frame. Next dig: one "VP latched" line per session is expected.
			dbglog("[PostFX] DrawVelocityBuffer: first frame, VP latched + camera pose (velocity content = clear value this frame)");
		}
		// else (R4 fix): secondary-camera matrix on the device — do NOT latch a
		// poisoned first pose; g_prevVPValid stays false and the next
		// main-camera frame takes the first-frame path again.
		return;
	}

	// Tier 0 (b) cut reset — camera cut / teleport / interior switch detector
	// (research doc 2026-10-29 Q5, producer half; the consumer CPU-history
	// half lives in postfx.cpp DrawMotionBlur mbCutReset). Diffs the current
	// camera pose against the pose latched with g_prevVPMatrix: a position
	// jump or a forward-vector swing beyond the dt-scaled thresholds is a
	// CUT, not motion. On a cut: clear the velocity texture to the zero-
	// velocity value (so every consumer reads ZERO this frame instead of
	// stale pre-cut vectors), re-latch VP+pose to current (GTA V "cut ->
	// reset prev matrix", PostProcessFX.cpp:7485-7506) and skip the pass.
	// This also covers re-entry after the menu/pause/cutscene gate: the pose
	// across the gated stretch is stale, so the first frame back trips the
	// detector and starts clean instead of smearing.
	// Thresholds scale with dt (MotionBlurDtScale: 1.0 at 60 FPS) so real
	// motion at low FPS (hitch to ~10 FPS while driving: ~4 units/frame)
	// doesn't false-cut: threshold = 60FPS-value / scale keeps the speed
	// headroom constant across framerates.
	float mbDtScale = MotionBlurDtScale();
	float dtScaleG = max(mbDtScale, 0.1f);
	float posDelta = sqrtf((camPosX-s_prevCamX)*(camPosX-s_prevCamX) +
	                       (camPosY-s_prevCamY)*(camPosY-s_prevCamY) +
	                       (camPosZ-s_prevCamZ)*(camPosZ-s_prevCamZ));
	float fwdDot = fwdX*s_prevFwdX + fwdY*s_prevFwdY + fwdZ*s_prevFwdZ;
	fwdDot = max(-1.0f, min(1.0f, fwdDot));
	float posCut = 2.5f / dtScaleG;		// units/frame @60FPS ref (driving telemetry max 0.487)
	float angCutRad = (25.0f * 3.14159265f / 180.0f) / dtScaleG;	// deg/frame @60FPS ref (hard flicks ~6)
	float fwdCut = cosf(angCutRad);
	// R4 fix: the detector only fires when the pose validated as the MAIN
	// render camera (poseMainCam above). A leaked secondary-camera matrix
	// must neither trip the cut (false velocity clear = rotor/vehicle smear)
	// nor re-latch (would poison the next real frame's diff).
	if(poseMainCam && (posDelta > posCut || fwdDot < fwdCut)){
		ClearVelocityToZero(dev);
		VelocityHistoryLatch(curVP, Scene.camera, camPosX, camPosY, camPosZ, fwdX, fwdY, fwdZ);
		dbglog("[PostFX] DrawVelocityBuffer: camera CUT reset (posDelta=%.2f/%.2f fwdDot=%.4f/%.4f) — velocity cleared, VP+pose re-latched",
			posDelta, posCut, fwdDot, fwdCut);
		return;
	}

	// Compute inverse of current VP (manual 4x4 inverse via cofactors)
	D3DMATRIX invCurVP;
	{
		// Compute 2x2 subdeterminants of last two rows
		float s0 = curVP.m[0][0] * curVP.m[1][1] - curVP.m[1][0] * curVP.m[0][1];
		float s1 = curVP.m[0][0] * curVP.m[1][2] - curVP.m[1][0] * curVP.m[0][2];
		float s2 = curVP.m[0][0] * curVP.m[1][3] - curVP.m[1][0] * curVP.m[0][3];
		float s3 = curVP.m[0][1] * curVP.m[1][2] - curVP.m[1][1] * curVP.m[0][2];
		float s4 = curVP.m[0][1] * curVP.m[1][3] - curVP.m[1][1] * curVP.m[0][3];
		float s5 = curVP.m[0][2] * curVP.m[1][3] - curVP.m[1][2] * curVP.m[0][3];

		float c5 = curVP.m[2][2] * curVP.m[3][3] - curVP.m[3][2] * curVP.m[2][3];
		float c4 = curVP.m[2][1] * curVP.m[3][3] - curVP.m[3][1] * curVP.m[2][3];
		float c3 = curVP.m[2][1] * curVP.m[3][2] - curVP.m[3][1] * curVP.m[2][2];
		float c2 = curVP.m[2][0] * curVP.m[3][3] - curVP.m[3][0] * curVP.m[2][3];
		float c1 = curVP.m[2][0] * curVP.m[3][2] - curVP.m[3][0] * curVP.m[2][2];
		float c0 = curVP.m[2][0] * curVP.m[3][1] - curVP.m[3][0] * curVP.m[2][1];

		float det = s0*c5 - s1*c4 + s2*c3 + s3*c2 - s4*c1 + s5*c0;
		float invDet = 1.0f / max(fabs(det), 1e-7f);

		invCurVP.m[0][0] = ( curVP.m[1][1]*c5 - curVP.m[1][2]*c4 + curVP.m[1][3]*c3) * invDet;
		invCurVP.m[0][1] = (-curVP.m[0][1]*c5 + curVP.m[0][2]*c4 - curVP.m[0][3]*c3) * invDet;
		invCurVP.m[0][2] = ( curVP.m[3][1]*s5 - curVP.m[3][2]*s4 + curVP.m[3][3]*s3) * invDet;
		invCurVP.m[0][3] = (-curVP.m[2][1]*s5 + curVP.m[2][2]*s4 - curVP.m[2][3]*s3) * invDet;
		invCurVP.m[1][0] = (-curVP.m[1][0]*c5 + curVP.m[1][2]*c2 - curVP.m[1][3]*c1) * invDet;
		invCurVP.m[1][1] = ( curVP.m[0][0]*c5 - curVP.m[0][2]*c2 + curVP.m[0][3]*c1) * invDet;
		invCurVP.m[1][2] = (-curVP.m[3][0]*s5 + curVP.m[3][2]*s2 - curVP.m[3][3]*s1) * invDet;
		invCurVP.m[1][3] = ( curVP.m[2][0]*s5 - curVP.m[2][2]*s2 + curVP.m[2][3]*s1) * invDet;
		invCurVP.m[2][0] = ( curVP.m[1][0]*c4 - curVP.m[1][1]*c2 + curVP.m[1][3]*c0) * invDet;
		invCurVP.m[2][1] = (-curVP.m[0][0]*c4 + curVP.m[0][1]*c2 - curVP.m[0][3]*c0) * invDet;
		invCurVP.m[2][2] = ( curVP.m[3][0]*s4 - curVP.m[3][1]*s2 + curVP.m[3][3]*s0) * invDet;
		invCurVP.m[2][3] = (-curVP.m[2][0]*s4 + curVP.m[2][1]*s2 - curVP.m[2][3]*s0) * invDet;
		invCurVP.m[3][0] = (-curVP.m[1][0]*c3 + curVP.m[1][1]*c1 - curVP.m[1][2]*c0) * invDet;
		invCurVP.m[3][1] = ( curVP.m[0][0]*c3 - curVP.m[0][1]*c1 + curVP.m[0][2]*c0) * invDet;
		invCurVP.m[3][2] = (-curVP.m[3][0]*s3 + curVP.m[3][1]*s1 - curVP.m[3][2]*s0) * invDet;
		invCurVP.m[3][3] = ( curVP.m[2][0]*s3 - curVP.m[2][1]*s1 + curVP.m[2][2]*s0) * invDet;
	}

	// Capture RT0 BEFORE any state mutation: if the Get fails (lost device),
	// the bail below returns with the pipeline untouched. SetRenderTarget(0,
	// NULL) is illegal in D3D9, so a NULL here must not reach the restore.
	IDirect3DSurface9 *origRT = NULL;
	dev->GetRenderTarget(0, &origRT);
	if(!origRT)
		return;

	// Render velocity buffer
	DWORD rawGeom[9];
	bool rawGeomSaved = SaveRawGeomStates(rawGeom); // BEFORE Store (DepthHook below writes raw ZENABLE)
	// Raw sampler CLAMP/POINT below bypasses the rw cache (no L2 twin);
	// capture the pre-pass state per touched stage so later WRAP/LINEAR
	// consumers are not left clamped/point-filtered. Restored in the shared
	// cleanup after the __try/__except, so both paths are covered.
	DWORD s0AddrU = D3DTADDRESS_WRAP, s0AddrV = D3DTADDRESS_WRAP;
	DWORD s0Min = D3DTEXF_LINEAR, s0Mag = D3DTEXF_LINEAR;
	DWORD s1AddrU = D3DTADDRESS_WRAP, s1AddrV = D3DTADDRESS_WRAP;
	DWORD s1Min = D3DTEXF_LINEAR, s1Mag = D3DTEXF_LINEAR;
	dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &s0AddrU);
	dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &s0AddrV);
	dev->GetSamplerState(0, D3DSAMP_MINFILTER, &s0Min);
	dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &s0Mag);
	dev->GetSamplerState(1, D3DSAMP_ADDRESSU, &s1AddrU);
	dev->GetSamplerState(1, D3DSAMP_ADDRESSV, &s1AddrV);
	dev->GetSamplerState(1, D3DSAMP_MINFILTER, &s1Min);
	dev->GetSamplerState(1, D3DSAMP_MAGFILTER, &s1Mag);
	if((s0AddrU != D3DTADDRESS_WRAP || s0AddrV != D3DTADDRESS_WRAP ||
	    s0Min != D3DTEXF_LINEAR || s0Mag != D3DTEXF_LINEAR ||
	    s1AddrU != D3DTADDRESS_WRAP || s1AddrV != D3DTADDRESS_WRAP ||
	    s1Min != D3DTEXF_LINEAR || s1Mag != D3DTEXF_LINEAR) &&
	   dbglog_throttle("pf_vel_samp"))
		dbglog("[PF] VelocityBuffer sampler capture != WRAP/LINEAR (restored): s0 u=%lu v=%lu min=%lu mag=%lu | s1 u=%lu v=%lu min=%lu mag=%lu",
			s0AddrU, s0AddrV, s0Min, s0Mag, s1AddrU, s1AddrV, s1Min, s1Mag);
	CPostEffects::ImmediateModeRenderStatesStore();
	CPostEffects::ImmediateModeRenderStatesSet();
	RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

	// Set velocity render target
	dev->SetRenderTarget(0, g_velocitySurf);
	dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 128, 128, 0), 1.0f, 0);

	// Entry device PS (AddRef via GetPixelShader) — captured for the paired
	// PS exit after the __except below. Declared here so both the park (in
	// __try) and the release (shared cleanup) see the same slot.
	IDirect3DPixelShader9 *velEntryPS = NULL;

	// Suspend depth hook: unbind INTZ as DS so it can be sampled on s0
	__try {
		DepthHook_Suspend();

		// Bind depth texture on s0
		dev->SetTexture(0, g_ssaoDepthTex);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		// Wave-2: previous-frame depth on s1 (POINT/CLAMP, NULL-safe)
		dev->SetTexture(1, g_prevDepthValid ? g_prevDepthTex : NULL);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

		// Upload constants
		RwD3D9SetPixelShaderConstant(0, &invCurVP, 4); // c0-c3: inverse current VP
		RwD3D9SetPixelShaderConstant(4, &g_prevVPMatrix, 4); // c4-c7: previous VP
		float screenParams[4] = { (float)w, (float)h, 1.0f/max((float)w, 1e-7f), 1.0f/max((float)h, 1e-7f) };
		RwD3D9SetPixelShaderConstant(8, screenParams, 1); // c8: screen params
		// c9: Wave-2 prev-depth {prevDepthValid, farPlane, 0, 0}
		float c9PrevDepth[4] = { (float)g_prevDepthValid, Scene.camera->farPlane, 0.0f, 0.0f };
		RwD3D9SetPixelShaderConstant(9, c9PrevDepth, 1);

		// Render fullscreen quad — camera-sized with UV 0..1, NOT
		// colorfilterVerts (0..2048 positions): the velocity RT is w x h, so
		// the game's quad clipped at the screen only wrote UV 0..0.9375 x
		// 0..0.527 — the right edge and the WHOLE BOTTOM HALF of the velocity
		// buffer kept the Clear() value (zero velocity) and every reprojection
		// consumer (SMAA temporal history, SSAO temporal, motion blur) was
		// unshifted there -> trailing/ghosting on the lower half of the screen.
		SetupFullscreenQuad((float)w, (float)h);
		// PAIRED PS BIND (velocity-trails fix, mirrors DrawMotionBlur test-8,
		// postfx.cpp:4592-4623): RwD3D9SetPixelShader value-compares against
		// the exe-side _rwD3D9LastPixelShaderUsed cache (0x8E244C). That cache
		// persists ACROSS FRAMES, so at this point it still holds
		// VelocityReconstruct from the PREVIOUS velocity draw — while raw
		// device-PS writers in the scene phase (waterPipe.cpp:2621
		// g_d3dDevice->SetPixelShader, mirrorPass.cpp:184, forwardplus) can
		// leave a FOREIGN shader on the device. The im2d hook's Set
		// (Im2dSetPixelShader_hook, postfx.cpp:509) then ELIDES, and this
		// quad rasterises with the foreign PS sampling s0=depth s1=prevDepth
		// under wrong semantics — velocity-buffer content painted to the
		// screen, or garbage vectors fed to every reprojection consumer
		// (SMAA temporal, SSAO temporal, motion blur). Fix: park-and-set.
		// Raw SetPixelShader(NULL) forces the device to a known state (always
		// a real write); RwD3D9SetPixelShader(NULL) re-seeds the RW cache to
		// NULL; the hook's Set(VelocityReconstruct) is then a guaranteed
		// cache miss -> real device write.
		if(SUCCEEDED(dev->GetPixelShader(&velEntryPS))){
			// Log-hygiene (2026-11-02): recurring receipt -> g_diagLevel>=1
			// only; the first hit is always logged (one-shot).
			if(velEntryPS != (IDirect3DPixelShader9*)VelocityReconstruct &&
			   (dbglog_once("vel_pssplit_first") ||
			    (g_diagLevel >= 1 && dbglog_throttle("vel_pssplit"))))
				dbglog("[vel_pssplit] DrawVelocityBuffer: device PS %p != VelocityReconstruct %p BEFORE bind "
					"(raw scene-phase writer left a foreign shader; un-parked bind would have elided)",
					(void*)velEntryPS, VelocityReconstruct);
		}
		dev->SetPixelShader(NULL);		// raw park: device = known NULL
		RwD3D9SetPixelShader(NULL);		// cache park: RW cache = NULL
		overrideIm2dPixelShader = VelocityReconstruct;
		if(dbglog_throttle("pf_vel_bind"))
			dbglog("[PF] VelocityBuffer bind: ps=%p(IDR_VELOCITYRECONSTRUCT 252) s0=depth s1=prevDepth out=g_velocitySurf %dx%d",
				VelocityReconstruct, w, h);
		PfBindPS(VelocityReconstruct, "IDR_VELOCITYRECONSTRUCT(252)");
		PfDraw(4, 2);
		RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, s_ffQuad, 4, s_ffQuadIdx, 6);
		overrideIm2dPixelShader = nil;

		// Unbind INTZ from s0 before the hook re-binds it as DS (feedback-lock
		// guard, mirrors DrawSSAO/DrawHeightFog); the cleanup below stays as-is.
		dev->SetTexture(0, NULL);
		// Restore depth hook (re-binds INTZ as DS)
		DepthHook_Restore();
	} __except(EXCEPTION_EXECUTE_HANDLER){
		overrideIm2dPixelShader = nil;	// SEH: the `= nil` reset in the __try above is skipped on fault

		dbglog("[PostFX] DrawVelocityBuffer CRASHED in depth pass exception=0x%08X", GetExceptionCode());
		dev->SetTexture(0, NULL); // same unbind before the bail-out Restore
		DepthHook_Restore(); // Restore on bail to keep Suspend/Restore balanced
	}

	// PS exit (mirrors DrawMotionBlur postfx.cpp:4655-4668): the draw's Im2D
	// hook set VelocityReconstruct through RwD3D9SetPixelShader, so BOTH the
	// RW PS cache and the device hold it when this pass ends. Force both
	// layers back to NULL — a later cache-gated Set must never elide against
	// a stale VelocityReconstruct entry, and no downstream consumer inherits
	// a postfx shader. Runs on the normal AND __except paths (sits after the
	// handler). The captured entry PS is deliberately NOT restored.
	dev->SetPixelShader(NULL);		// L3 raw: always a real write
	RwD3D9SetPixelShader(NULL);		// L2 cache re-seed (+ device transition)
	if(velEntryPS){
		velEntryPS->Release();
		velEntryPS = NULL;
	}

	// Restore render target
	dev->SetRenderTarget(0, origRT);
	if(origRT) origRT->Release();

	// Cleanup — the captured pre-pass sampler state is restored below, after
	// ImmediateModeRenderStatesReStore (which re-emits the rw-cached texture
	// filter through RwD3D9 and would clobber a raw min/mag restore).
	dev->SetTexture(0, NULL);
	dev->SetTexture(1, NULL);

	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
RwD3D9SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

	CPostEffects::ImmediateModeRenderStatesReStore();
	// Raw L3 sampler restore — normal path and __except bail-out alike.
	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, s0AddrU);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, s0AddrV);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, s0Min);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, s0Mag);
	dev->SetSamplerState(1, D3DSAMP_ADDRESSU, s1AddrU);
	dev->SetSamplerState(1, D3DSAMP_ADDRESSV, s1AddrV);
	dev->SetSamplerState(1, D3DSAMP_MINFILTER, s1Min);
	dev->SetSamplerState(1, D3DSAMP_MAGFILTER, s1Mag);
	if(rawGeomSaved){
		RestoreRawGeomStates(rawGeom);
		rawGeomSaved = false;
	}
	// Store VP + camera pose for next frame (cut detector diffs like-for-like).
	// R4 fix: only when the pose validated as the MAIN camera — a leaked
	// secondary-camera matrix (mirror / vehicle-cinematic rig) must not
	// overwrite the main-camera history; the previous main-camera VP+pose is
	// kept and the next real frame diffs against it cleanly.
	if(poseMainCam)
		VelocityHistoryLatch(curVP, Scene.camera, camPosX, camPosY, camPosZ, fwdX, fwdY, fwdZ);

	if(dbglog_throttle("pf_vel_done"))
		dbglog("[PF] DrawVelocityBuffer done: origRT restored, s0/s1 unbound, filters LINEAR, states restored");

	// NOTE: CopyDepthToPrev() moved to ColourFilter_switch, after DrawSSAO_Overhaul,
	// so SSAO temporal reprojection reads the actual previous-frame depth.
}
