#include "skygfx.h"
#include "render/postfx.h"
#include <d3d9.h>
#include <math.h>

// ============================================================
// Water Pipe - Xbox/IV/V water styles (shading-only waves)
// VS: Water_VS.hlsl  (shared by all styles)
// PS: Water_Parallax.hlsl — three style entry points, one CSO each:
//     main_xbox -> Water_Parallax.cso (Xbox/PS2-style core, default)
//     main_iv   -> Water_IV.cso       (GTA IV look)
//     main_v    -> Water_V.cso        (GTA V / RAGE look)
// INI waterStyle selects the shader entry + constant preset (NOT a separate
// draw path); INI waterQuality 0-3 gates cost inside every style (0 = one
// normal layer + timecyc colour + vertex-alpha only, 1 = + dual normals +
// depth ramp, 2 = + refraction/depth copy + foam + shore blend, 3 = + sun
// pierce + fine layer x3.7 + wide shore sampling). The scene/depth copies
// only run at tier >= 2; tiers 0/1 fall back to hardware SRCALPHA blending
// against the framebuffer (live translucency, zero texture cost).
//
// REWRITE core (kept): the water plane geometry is NEVER displaced — wave
// motion lives entirely in shading (scrolling mip-mapped detail normals).
// Colour, alpha and reflection tint come from SA's timecycle (CTimeCycle
// m_CurrentColours) with the INI waterDeep*/waterShallow* keys as optional
// overrides (waterUseTimecycle=0). Scene depth (INTZ copy) drives
// shallow->deep colour, translucency and soft shore/water blending.
// ============================================================

bool g_waterParallaxActive = false;
static float g_waterTime = 0.0f;

// INI `SkyGfx/waterFresnelSplit` (read in main.cpp next to the other water
// keys, default 1): 1 = fresnel-split composite (reflection + refraction both
// live every view direction), 0 = the legacy lerp(scene, surf, opacity)
// composite as a per-user rollback. Kept OUT of the config struct on purpose —
// it is a water-pipe-local switch, not a shared setting.
int g_waterFresnelSplit = 1;

extern void *Water_Parallax;
extern void *Water_IV;
extern void *Water_V;
extern void *Water_VS;

// Style-selecting pixel shader lookup. Missing entries fall back to the
// Xbox core so the draw never binds a stale shader; the hook gate calls this
// too so a style whose CSO failed to load degrades instead of garbage-draws.
void *waterPipe_pixelShader(void)
{
	switch(config->waterStyle){
	case 1:  if(Water_IV) return Water_IV; break;
	case 2:  if(Water_V)  return Water_V;  break;
	default: if(Water_Parallax) return Water_Parallax; break;
	}
	if(Water_Parallax) return Water_Parallax;
	if(Water_IV) return Water_IV;
	return Water_V;
}

// Naked thunk defined in main.cpp. rwcore.h's declaration sits behind the
// inline-driver guard (#ifdef _D3D9_H_ block, skipped in this TU) and the
// definition in main.cpp is a bare one — plain C++ linkage, matched here.
void _rwD3D9RenderStateFlushCache(void);

static RwCamera *&ccamera = *(RwCamera**)0xC170C4;
static IDirect3DDevice9 *g_d3dDevice = nullptr;
static IDirect3DTexture9 *g_sceneTexture = nullptr;
static int g_sceneWidth = 0, g_sceneHeight = 0;

// Scene depth copy (raw window-space z, R32F) for shore blending.
static IDirect3DTexture9 *g_depthTexture = nullptr;
static int g_depthWidth = 0, g_depthHeight = 0;

// Detail normal map (RGB = tangent-space normal, A = height). A real texture
// with a hardware mip chain — the old procedural hash noise had no mips,
// which is exactly what aliased into the sub-pixel sparkle at distance.
static IDirect3DTexture9 *g_detailTex = nullptr;
static bool g_detailTexHasMips = false;

// 0xC170C4 normally holds NULL or a real RwCamera*. Corrupted runs leave float

// 0.0004f bits there (pointer value 0x39D1B717, (p & 3) == 3) which passes a
// plain null check and AVs on the first field read (mov eax,[eax+4] ->
// [0x39D1B71B], symbolized waterPipe_setRenderState+0x432). A real RwCamera*
// is always 4-byte aligned and never inside the first page; no module
// range-check — the camera lives in game/heap memory.
static bool waterCamUsable(const RwCamera *p)
{
    return p != NULL && ((uintptr_t)p & 3) == 0 && (uintptr_t)p >= 0x10000;
}

// Cull mode in effect before the water draw — the water plane renders
// double-sided (single unified pass, no front/backface split), and whatever
// the frame had set goes back afterwards. Pre-initialised so a failed Get can
// never leave it as garbage (grassRenderCallback gotcha).
static RwUInt32 s_savedCull = rwCULLMODECULLBACK;

void waterPipe_init(void)
{
    g_waterParallaxActive = false;
    g_waterTime = 0.0f;
}

void waterPipe_shutdown(void)
{
    g_waterParallaxActive = false;
    // D3DPOOL_DEFAULT resource — caller (DLL_PROCESS_DETACH) wraps this in
    // __try/__except since the device may already be torn down.
    if(g_sceneTexture){ g_sceneTexture->Release(); g_sceneTexture = nullptr; }
    g_sceneWidth = g_sceneHeight = 0;
    if(g_depthTexture){ g_depthTexture->Release(); g_depthTexture = nullptr; }
    g_depthWidth = g_depthHeight = 0;
    if(g_detailTex){ g_detailTex->Release(); g_detailTex = nullptr; }
    g_detailTexHasMips = false;
}

// Re-acquire the D3D9 device and drop stale D3DPOOL_DEFAULT refraction RTs.
// Water-local device-loss handling (postfx OnDeviceReset/ReleaseDefaultPoolResources
// is a separate lane) — recreate lazily when the device is lost or replaced.
static void SyncDevice(void)
{
    IDirect3DDevice9 *dev = *(IDirect3DDevice9**)0xC97C28;
    if(!dev) return;
    if(dev != g_d3dDevice){
        // Device was recreated — the old DEFAULT-pool textures died with the
        // old device; do NOT Release() the dangling pointers, just forget them.
        g_sceneTexture = nullptr;
        g_sceneWidth = g_sceneHeight = 0;
        g_depthTexture = nullptr;
        g_depthWidth = g_depthHeight = 0;
        g_detailTex = nullptr;
        g_detailTexHasMips = false;
        g_d3dDevice = dev;
        return;
    }
    // Device lost (alt-tab / mode switch before Reset): drop our RTs now so the
    // game's upcoming Reset() can succeed, and skip custom water this frame.
    if(g_sceneTexture && g_d3dDevice->TestCooperativeLevel() != D3D_OK){
        g_sceneTexture->Release();
        g_sceneTexture = nullptr;
        g_sceneWidth = g_sceneHeight = 0;
        if(g_depthTexture){ g_depthTexture->Release(); g_depthTexture = nullptr; }
        g_depthWidth = g_depthHeight = 0;
        if(dbglog_throttle("water_lost"))
            dbglog("[water] device lost, released refraction/depth RTs");
    }
}

static void EnsureSceneTexture(int w, int h)
{
    if(g_sceneTexture && g_sceneWidth == w && g_sceneHeight == h) return;
    if(g_sceneTexture){ g_sceneTexture->Release(); g_sceneTexture = nullptr; }
    if(w < 1 || h < 1) return;
    HRESULT hr = g_d3dDevice->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_sceneTexture, nullptr);
    if(FAILED(hr) || !g_sceneTexture){ g_sceneTexture = nullptr; return; }
    g_sceneWidth = w; g_sceneHeight = h;
}

// R32F depth-copy RT (raw window-space z copied from the INTZ depth-stencil).
static void EnsureDepthTexture(int w, int h)
{
    if(g_depthTexture && g_depthWidth == w && g_depthHeight == h) return;
    if(g_depthTexture){ g_depthTexture->Release(); g_depthTexture = nullptr; }
    g_depthWidth = g_depthHeight = 0;
    if(w < 1 || h < 1) return;
    HRESULT hr = g_d3dDevice->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_R32F, D3DPOOL_DEFAULT, &g_depthTexture, nullptr);
    if(FAILED(hr) || !g_depthTexture){
        g_depthTexture = nullptr;
        if(dbglog_throttle("water_nodepthrt"))
            dbglog("[water] R32F depth RT unavailable (%dx%d hr=0x%08X)", w, h, hr);
        return;
    }
    g_depthWidth = w; g_depthHeight = h;
}

static void CopySceneToTexture(void)
{
    if(!g_d3dDevice || !g_sceneTexture) return;
    IDirect3DSurface9 *backBuf = nullptr, *sceneSurf = nullptr;
    g_d3dDevice->GetRenderTarget(0, &backBuf);
    g_sceneTexture->GetSurfaceLevel(0, &sceneSurf);
    if(backBuf && sceneSurf)
        g_d3dDevice->StretchRect(backBuf, nullptr, sceneSurf, nullptr, D3DTEXF_POINT);
    if(backBuf) backBuf->Release();
    if(sceneSurf) sceneSurf->Release();
}

// ============================================================
// Detail normal map generation (CPU, tileable; GPU builds the mip chain).
// Periodic value noise + two seam-free directional ripple trains -> looks
// like capillary wave detail; central-difference normals with wraparound.
// ============================================================
static inline float DetailHash(int x, int y)
{
    unsigned int n = (unsigned int)(x * 374761393 + y * 668265263);
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= n >> 16;
    return (float)(n & 0xFFFFFF) / 16777215.0f;
}

// Periodic value noise: lattice wraps at `period` cells so the tile is seamless.
static float DetailValueNoise(float x, float y, int period)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - (float)xi, fy = y - (float)yi;
    int x0 = ((xi % period) + period) % period;
    int y0 = ((yi % period) + period) % period;
    int x1 = (x0 + 1) % period, y1 = (y0 + 1) % period;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float a = DetailHash(x0, y0), b = DetailHash(x1, y0);
    float c = DetailHash(x0, y1), d = DetailHash(x1, y1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

#define DETAIL_TEX_N 256

static void EnsureDetailNormalTex(void)
{
    if(g_detailTex) return;
    if(!g_d3dDevice) return;

    // AUTOGENMIPMAP gives a real hardware mip chain (req: mip-correct LOD).
    HRESULT hr = g_d3dDevice->CreateTexture(DETAIL_TEX_N, DETAIL_TEX_N, 0,
        D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
        &g_detailTex, nullptr);
    g_detailTexHasMips = SUCCEEDED(hr) && g_detailTex != nullptr;
    if(!g_detailTex){
        // Fallback: single-level texture (MIPFILTER off at bind time).
        hr = g_d3dDevice->CreateTexture(DETAIL_TEX_N, DETAIL_TEX_N, 1,
            0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_detailTex, nullptr);
        g_detailTexHasMips = false;
        if(FAILED(hr) || !g_detailTex){
            g_detailTex = nullptr;
            if(dbglog_throttle("water_nodetail"))
                dbglog("[water] detail normal texture create failed hr=0x%08X", hr);
            return;
        }
    }

    // ---- tileable heightfield ----
    static float s_h[DETAIL_TEX_N * DETAIL_TEX_N];
    const float hNorm = 1.0f / (0.500f + 0.250f + 0.125f + 0.16f + 0.10f);
    for(int y = 0; y < DETAIL_TEX_N; y++){
        for(int x = 0; x < DETAIL_TEX_N; x++){
            float u = (float)x / (float)DETAIL_TEX_N;
            float v = (float)y / (float)DETAIL_TEX_N;
            float h = 0.0f;
            h += 0.500f * DetailValueNoise(u *  8.0f, v *  8.0f,  8);
            h += 0.250f * DetailValueNoise(u * 16.0f, v * 16.0f, 16);
            h += 0.125f * DetailValueNoise(u * 32.0f, v * 32.0f, 32);
            // Directional ripple trains; integer frequencies over the tile
            // keep them seamless (phase noise is periodic too).
            float ph1 = 6.2831853f * DetailValueNoise(u * 4.0f, v * 4.0f, 4);
            float ph2 = 6.2831853f * DetailValueNoise(u * 6.0f, v * 6.0f, 6);
            h += 0.16f * (0.5f + 0.5f * sinf(6.2831853f * ( 3.0f * u + 1.0f * v) + ph1));
            h += 0.10f * (0.5f + 0.5f * sinf(6.2831853f * (-2.0f * u + 3.0f * v) + ph2));
            s_h[y * DETAIL_TEX_N + x] = h * hNorm;
        }
    }

    D3DLOCKED_RECT lr;
    if(FAILED(g_detailTex->LockRect(0, &lr, nullptr, 0))){
        g_detailTex->Release();
        g_detailTex = nullptr;
        return;
    }
    for(int y = 0; y < DETAIL_TEX_N; y++){
        DWORD *dst = (DWORD*)((BYTE*)lr.pBits + y * lr.Pitch);
        int ym = (y + DETAIL_TEX_N - 1) & (DETAIL_TEX_N - 1);
        int yp = (y + 1) & (DETAIL_TEX_N - 1);
        for(int x = 0; x < DETAIL_TEX_N; x++){
            int xm = (x + DETAIL_TEX_N - 1) & (DETAIL_TEX_N - 1);
            int xp = (x + 1) & (DETAIL_TEX_N - 1);
            float hC = s_h[y * DETAIL_TEX_N + x];
            float nx = (s_h[y  * DETAIL_TEX_N + xm] - s_h[y  * DETAIL_TEX_N + xp]) * 3.0f;
            float ny = (s_h[ym * DETAIL_TEX_N + x ] - s_h[yp * DETAIL_TEX_N + x ]) * 3.0f;
            float nz = 1.0f;
            float invLen = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
            nx *= invLen; ny *= invLen; nz *= invLen;
            int r = (int)((nx * 0.5f + 0.5f) * 255.0f + 0.5f);
            int g = (int)((ny * 0.5f + 0.5f) * 255.0f + 0.5f);
            int b = (int)((nz * 0.5f + 0.5f) * 255.0f + 0.5f);
            int a = (int)(hC * 255.0f + 0.5f);
            if(r < 0) r = 0; if(r > 255) r = 255;
            if(g < 0) g = 0; if(g > 255) g = 255;
            if(b < 0) b = 0; if(b > 255) b = 255;
            if(a < 0) a = 0; if(a > 255) a = 255;
            dst[x] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
        }
    }
    g_detailTex->UnlockRect(0);
    if(g_detailTexHasMips)
        g_detailTex->GenerateMipSubLevels();
    dbglog("[water] detail normal tex %dx%d mips=%d", DETAIL_TEX_N, DETAIL_TEX_N,
           g_detailTexHasMips ? 1 : 0);
}

// Timecycle values are stored as floats but come from 0-255 DAT columns;
// accept either scale. (Sky colours are shorts 0-255.)
static inline float tc01(float v)
{
    float s = v > 1.5f ? v * (1.0f / 255.0f) : v;
    if(s < 0.0f) s = 0.0f;
    if(s > 1.0f) s = 1.0f;
    return s;
}

static inline float satf(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void waterPipe_setRenderState(void)
{
    // Reset first — only set true after every setup step succeeded, so callers
    // can trust the flag as "water pipe fully bound this draw".
    g_waterParallaxActive = false;

    SyncDevice();
    if(!g_d3dDevice) return;
    void *ps = waterPipe_pixelShader();
    if(!config->waterParallaxEnable || !ps || !Water_VS) return;
    if(g_d3dDevice->TestCooperativeLevel() != D3D_OK){
        if(dbglog_throttle("water_lost"))
            dbglog("[water] device not OK, falling back to original draw");
        return;
    }

    // Quality tier 0-3: gates cost inside the shader (uniform branch on
    // c11.x) AND the scene/depth copies here — those only run at tier >= 2.
    int tier = config->waterQuality;
    if(tier < 0) tier = 0;
    if(tier > 3) tier = 3;
    bool needRT = (tier >= 2);

    D3DVIEWPORT9 vp;
    g_d3dDevice->GetViewport(&vp);
    if(vp.Width < 1 || vp.Height < 1) return;

    // Copy scene for refraction (tier >= 2 only)
    if(needRT){
        EnsureSceneTexture(vp.Width, vp.Height);
        if(!g_sceneTexture){
            // No refraction RT — bail BEFORE touching any shader/render state;
            // the hook falls through to the original water draw.
            if(dbglog_throttle("water_nort"))
                dbglog("[water] refraction RT unavailable (%ux%u)", vp.Width, vp.Height);
            return;
        }
        CopySceneToTexture();
    }

    // Copy scene depth (INTZ -> R32F) for depth-based shore blending /
    // translucency (tier >= 2 only — this is the expensive helper). Runs per
    // water draw so a second buffer flush sees the first flush's water
    // surface and blends into it instead of popping.
    bool depthValid = false;
    if(needRT){
        EnsureDepthTexture(vp.Width, vp.Height);
        if(g_depthTexture){
            depthValid = PostFX_CopyDepthToTexture(g_depthTexture, g_depthWidth, g_depthHeight);
            // The depth-copy helper juggles RT/DS/raw states — flush RW's render
            // state cache so every RwRenderStateSet below re-applies from scratch.
            _rwD3D9RenderStateFlushCache();
        }
    }

    // Detail normal texture (procedurally generated once, mip-mapped)
    EnsureDetailNormalTex();

    // Delta time
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    static LARGE_INTEGER lastTime = {0};
    static double perfFreqInv = 0.0;
    if(perfFreqInv == 0.0){
        LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
        perfFreqInv = 1.0 / (double)freq.QuadPart;
    }
    if(lastTime.QuadPart == 0) lastTime = now;
    g_waterTime += (float)((double)(now.QuadPart - lastTime.QuadPart) * perfFreqInv);
    lastTime = now;

    // Set shaders
    RwD3D9SetVertexShader(Water_VS);
    RwD3D9SetPixelShader(ps);

    // VS constants
    float wvp[16];
    // Water TempBuffer verts are world-space (identity world uploaded at VS c4
    // below), but _rwD3D9VSGetComposedTransformMatrix composes with
    // D3D9ActiveTransform — the world matrix left set by the last building/
    // vehicle atomic draw (rpworld.h: "Call SetActiveWorldMatrix before...
    //"). That stale world displaces the water quads by a random object's
    // transform (quads land over land/props) and disagrees with the identity
    // world used for worldPos/viewDir. Null it for the compose only, then
    // restore — D3D9Transform itself is untouched on the NULL path.
    extern RwMatrix *&D3D9ActiveTransform;
    RwMatrix *savedWorldM = D3D9ActiveTransform;
    D3D9ActiveTransform = NULL;
    _rwD3D9VSGetComposedTransformMatrix(wvp);
    D3D9ActiveTransform = savedWorldM;
    RwD3D9SetVertexShaderConstant(0, wvp, 4);

    float world[16] = {0};
    world[0] = world[5] = world[10] = world[15] = 1.0f;
    RwD3D9SetVertexShaderConstant(4, world, 4);

    RwV3d camPos = {0, 0, 0};
    // Hardened: alignment/wild check first, then SEH around the FIRST deref
    // chain (camera -> frame -> LTM -> pos) so a corrupted-but-aligned camera
    // object fails open to {0,0,0} instead of AVing. POD locals only (C2712).
    // 0xC170C4 is unreliable (docs: "normally holds NULL"; corrupted runs leave
    // float bits there) — second source: Scene.camera, the camera RW is
    // actually rendering the scene with (gta.h GlobalScene).
    RwCamera *wcam = ccamera;
    if(!waterCamUsable(wcam))
        wcam = Scene.camera;
    if(waterCamUsable(wcam)){
        InterlockedIncrement(&g_guardDepth);
        __try {
            RwFrame *camFrame = RwCameraGetFrame(wcam);
            if(camFrame){
                RwMatrix *camLTM = RwFrameGetLTM(camFrame);
                if(camLTM)
                    camPos = camLTM->pos;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){
            if(dbglog_throttle("water_camav"))
                dbglog("[water] camera frame/LTM fault code=0x%08X p=%p -> pos=0",
                       GetExceptionCode(), wcam);
            camPos.x = camPos.y = camPos.z = 0.0f;
        }
        InterlockedDecrement(&g_guardDepth);
    }
    float camTime[4] = {camPos.x, camPos.y, camPos.z, g_waterTime};
    RwD3D9SetVertexShaderConstant(8, camTime, 1);

    // Sun direction from pDirect (directional light), matching vehiclePipe pattern
    extern RpLight *&pDirect;
    RwV3d sunDirVec = {0, 0, 0};
    if(pDirect){
        RwFrame *sunFrame = RpLightGetFrame(pDirect);
        if(sunFrame){
            RwMatrix *sunLTM = RwFrameGetLTM(sunFrame);
            if(sunLTM)
                sunDirVec = *RwMatrixGetAt(sunLTM);
        }
    }
    float sunDirNeg[4] = {-sunDirVec.x, -sunDirVec.y, -sunDirVec.z, 1.0f};
    RwD3D9SetVertexShaderConstant(9, sunDirNeg, 1);

    // ---- Timecycle water colour / alpha + sky reflection tint ----
    // SA's CTimeCycle::m_CurrentColours carries the interpolated timecyc
    // values (timecyc per-hour/weather waterR/G/B/A + sky columns). The INI
    // waterDeep*/waterShallow* keys become optional overrides (req 4).
    float shR, shG, shB, dpR, dpG, dpB, waterAlpha;
    float skyTopP[4] = {0, 0, 0, 0};
    float skyBotP[4] = {0, 0, 0, 0};
    float lightP[4] = {0, 0, 0, 0};    // c12 = timecycle ambient rgb + sky luma
    float directP[4] = {0, 0, 0, 0};   // c13 = timecycle directional rgb + day
    {
        CColourSet &tc = CTimeCycle__m_CurrentColours;
        // Reflection tint — always timecycle: sky bottom = horizon colour
        // (grazing), sky top = zenith colour.
        skyTopP[0] = satf((float)tc.skyTopR / 255.0f);
        skyTopP[1] = satf((float)tc.skyTopG / 255.0f);
        skyTopP[2] = satf((float)tc.skyTopB / 255.0f);
        skyBotP[0] = satf((float)tc.skyBotR / 255.0f);
        skyBotP[1] = satf((float)tc.skyBotG / 255.0f);
        skyBotP[2] = satf((float)tc.skyBotB / 255.0f);

        // ---- Timecycle light (defect: night water glowed white) ----
        // Ambient/directional are ALREADY 0-1 in CColourSet (the game divides
        // the timecyc.dat columns by 255, weather.cpp blends the same way) —
        // do NOT divide again. Neither can separate day from night here: the
        // custom timecycle pins ambient near 0 and ships Dir=255 in EVERY
        // slot, so the day/night gate rides the sky luminance instead — the
        // same "sky the player sees" signal pipelinecommon's night ceiling
        // uses (gta.h CColourSet has no fog colour column; skyBot IS the
        // horizon/fog colour distant water fades into).
        lightP[0] = satf(tc.ambientR);
        lightP[1] = satf(tc.ambientG);
        lightP[2] = satf(tc.ambientB);
        float topL = 0.2126f*skyTopP[0] + 0.7152f*skyTopP[1] + 0.0722f*skyTopP[2];
        float botL = 0.2126f*skyBotP[0] + 0.7152f*skyBotP[1] + 0.0722f*skyBotP[2];
        float skyL = 0.5f * (topL + botL);
        lightP[3] = skyL;

        directP[0] = satf(tc.directionalR);
        directP[1] = satf(tc.directionalG);
        directP[2] = satf(tc.directionalB);
        // 0 = full night (sky luma < 0.12), 1 = day (>= 0.22). Dusk/dawn ramp
        // in between and the timecycle lerp smooths it hour to hour.
        // skyL < 0.003 = timecycle not populated yet (startup frame) — same
        // no-data test pipelinecommon's night ceiling uses; treat as daylight
        // so the first frames render the authored colour instead of black.
        float dayF = (skyL - 0.12f) * (1.0f / 0.10f);
        if(skyL < 0.003f) dayF = 1.0f;
        if(dayF < 0.0f) dayF = 0.0f;
        if(dayF > 1.0f) dayF = 1.0f;
        directP[3] = dayF;

        if(config->waterUseTimecycle){
            dpR = tc01(tc.waterR);
            dpG = tc01(tc.waterG);
            dpB = tc01(tc.waterB);
            // SA's OWN water colour quirk from CWaterLevel (col = col*0.65 +
            // 0.27, water-refs.md "HD-water color quirk" 0x6E716B): lifts AND
            // desaturates the authored colour — this is what vanilla renders.
            // It replaces the old dp*2.4+0.08 boost, which over-saturated the
            // day colour into turquoise AND blew the 85/85/65 night colour out
            // to near-white ("toxic sludge"). Derived from the timecyc colour,
            // so it tracks time of day for free.
            float bR = satf(dpR * 0.65f + 0.27f);
            float bG = satf(dpG * 0.65f + 0.27f);
            float bB = satf(dpB * 0.65f + 0.27f);
            // Shallow = same body over a bright bottom: a small desaturating
            // lift off the base (NOT a second hardcoded brightness ramp), so
            // the shallow->deep step stays small enough to read as a ramp.
            shR = satf(bR + (1.0f - bR) * 0.18f);
            shG = satf(bG + (1.0f - bG) * 0.18f);
            shB = satf(bB + (1.0f - bB) * 0.18f);
            dpR = bR;
            dpG = bG;
            dpB = bB;
            waterAlpha = tc01(tc.waterA);
            if(waterAlpha < 0.05f) waterAlpha = 0.05f;
        }else{
            shR = config->waterShallowR;
            shG = config->waterShallowG;
            shB = config->waterShallowB;
            dpR = config->waterDeepR;
            dpG = config->waterDeepG;
            dpB = config->waterDeepB;
            waterAlpha = 1.0f;
        }
    }

    // ---- Style constant presets (waterStyle switches the entry + preset) ----
    // Xbox: the INI knobs verbatim (the rewritten core is user-tunable).
    // IV:   tighter Blinn glints, stronger capped reflection, distance normal
    //       fade 1 - d^2*4e-4 (water-refs.md IV section).
    // V:    watertune SpecularFalloff 1118 at low intensity, damped/flattened
    //       reflection baked in-shader (5/6 + lerp(NdotV,1,0.3)), stronger
    //       reflection preset (water-refs.md V section).
    float specPow = config->waterSpecularPower;
    float specInt = config->waterSpecularIntensity;
    float reflStr = config->waterReflectionStrength;
    float distFadeK = 0.0f;     // c11.y — IV distance normal fade coefficient
    float pierceScale = 0.35f;  // c11.z — tier-3 sun pierce intensity
    if(config->waterStyle == 1){          // GTA IV preset
        specPow = 250.0f;
        specInt = config->waterSpecularIntensity * 0.6f;
        reflStr = satf(config->waterReflectionStrength * 1.6f);
        distFadeK = 4e-4f;
    }else if(config->waterStyle == 2){    // GTA V preset (watertune)
        specPow = 1118.0f;
        specInt = config->waterSpecularIntensity * 0.3f;
        reflStr = satf(config->waterReflectionStrength * 1.55f);
        pierceScale = 0.5f;
    }

    // PS constants — INI-driven knobs + timecycle colours
    float timeP[4] = {g_waterTime, 1.0f, config->waterFoamThreshold, config->waterFoamSoftness};
    RwD3D9SetPixelShaderConstant(0, timeP, 1);

    // c1 = (normalStrength, tileScale, fresnelPower, reflectionStrength)
    float waterP[4] = {config->waterNormalStrength, config->waterTileScale,
                       config->waterFresnelPower, reflStr};
    RwD3D9SetPixelShaderConstant(1, waterP, 1);

    // c2 = (specularPower, specularIntensity, translucency, shoreFadeRange)
    float specP[4] = {specPow, specInt,
                      config->waterTranslucency, config->waterShoreFade};
    RwD3D9SetPixelShaderConstant(2, specP, 1);

    // c3 = shallow rgb + deepRange (metres of shallow->deep ramp)
    float shallow[4] = {shR, shG, shB, 20.0f};
    RwD3D9SetPixelShaderConstant(3, shallow, 1);

    // c4 = deep rgb + base water alpha (timecycle waterA)
    float deep[4] = {dpR, dpG, dpB, waterAlpha};
    RwD3D9SetPixelShaderConstant(4, deep, 1);

    float invW = 1.0f / (float)(vp.Width  > 0 ? vp.Width  : 1);
    float invH = 1.0f / (float)(vp.Height > 0 ? vp.Height : 1);
    float screenP[4] = {(float)vp.Width, (float)vp.Height, invW, invH};
    RwD3D9SetPixelShaderConstant(5, screenP, 1);
    RwD3D9SetPixelShaderConstant(6, sunDirNeg, 1);
    float camPosPS[4] = {camPos.x, camPos.y, camPos.z, 0.0f};
    RwD3D9SetPixelShaderConstant(7, camPosPS, 1);

    float nearClip = 0.5f, farClip = 1000.0f;
    // Hardened: same usability check + SEH for the clip-plane deref chain
    // (fresh load of 0xC170C4, matching the original double-read semantics).
    RwCamera *wcamClip = ccamera;
    if(!waterCamUsable(wcamClip))
        wcamClip = Scene.camera;
    if(waterCamUsable(wcamClip)){
        InterlockedIncrement(&g_guardDepth);
        __try {
            nearClip = RwCameraGetNearClipPlane(wcamClip);
            farClip = RwCameraGetFarClipPlane(wcamClip);
        } __except(EXCEPTION_EXECUTE_HANDLER){
            if(dbglog_throttle("water_camav"))
                dbglog("[water] camera clip fault code=0x%08X p=%p -> defaults",
                       GetExceptionCode(), wcamClip);
            nearClip = 0.5f;
            farClip = 1000.0f;
        }
        InterlockedDecrement(&g_guardDepth);
    }
    // c8 = (nearClip, farClip, depthValid, refractScale)
    float clipP[4] = {nearClip, farClip, depthValid ? 1.0f : 0.0f, config->waterParallaxScale};
    RwD3D9SetPixelShaderConstant(8, clipP, 1);

    RwD3D9SetPixelShaderConstant(9, skyTopP, 1);
    RwD3D9SetPixelShaderConstant(10, skyBotP, 1);

    // c11 = (qualityTier, distFadeK, sunPierceScale, fresnelSplitFlag)
    // The flag drives the composite form in Water_Parallax.hlsl: 1 = the
    // fresnel split (reflection AND refraction live per view direction),
    // 0 = the legacy lerp(scene, surf, bodyOpacity) rollback. Default 1 —
    // it MUST be uploaded, c11.w == 0 selects the legacy path.
    float qualP[4] = {(float)tier, distFadeK, pierceScale,
                      (float)(g_waterFresnelSplit ? 1 : 0)};
    RwD3D9SetPixelShaderConstant(11, qualP, 1);

    // c12/c13 = timecycle light (ambient rgb + sky luma, directional rgb + day
    // factor) — the water body/highlights follow it exactly like the other
    // lighting models instead of an unlit hardcoded brightness (night glow fix).
    RwD3D9SetPixelShaderConstant(12, lightP, 1);
    RwD3D9SetPixelShaderConstant(13, directP, 1);

    // ---- Textures + sampler states ----
    // s0 = scene colour copy (refraction), s1 = mip-mapped detail normals,
    // s2 = raw scene depth copy. Raw SetTexture bypasses RW's state cache —
    // clear the cached stage-0 raster first (RenderLumaPass idiom). Tier 0/1
    // never fetch s0/s2 (no copies exist), so leave them unbound.
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
    g_d3dDevice->SetTexture(0, needRT ? g_sceneTexture : nullptr);
    g_d3dDevice->SetTexture(1, g_detailTex);
    g_d3dDevice->SetTexture(2, (needRT && depthValid) ? g_depthTexture : nullptr);

    g_d3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_d3dDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    g_d3dDevice->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_d3dDevice->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    // Mip-correct LOD: trilinear on the detail texture (req 3).
    g_d3dDevice->SetSamplerState(1, D3DSAMP_MIPFILTER,
        g_detailTexHasMips ? D3DTEXF_LINEAR : D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    g_d3dDevice->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    g_d3dDevice->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_d3dDevice->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    // Tier >= 2: opaque replace — translucency is composited in-shader (lerp
    // with the refracted scene), so no alpha blending is wanted at the draw.
    // Tier 0/1: no scene copy exists — the shader returns an opacity alpha
    // and the classic SRCALPHA/INVSRCALPHA blend (SA/re3 water convention)
    // composites it live against the framebuffer for free.
    if(tier >= 2){
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)false);
    }else{
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)true);
    }
    // Double-sided single pass: the sheet must render from BELOW as well
    // (underwater view of the surface). A backface-culled draw split the
    // plane into front/back halves and showed as seams along the surface
    // edges from under the water. The value is saved pre-Get (a failed Get
    // leaves the out-param untouched) and restored in
    // waterPipe_restoreRenderState.
    s_savedCull = rwCULLMODECULLBACK;
    RwRenderStateGet(rwRENDERSTATECULLMODE, (void*)&s_savedCull);
    if(s_savedCull != rwCULLMODECULLNONE && s_savedCull != rwCULLMODECULLBACK)
        s_savedCull = rwCULLMODECULLBACK;
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)false);

    g_waterParallaxActive = true;
}

void waterPipe_restoreRenderState(void)
{
    if(!g_waterParallaxActive) return;
    RwD3D9SetPixelShader(nullptr);
    RwD3D9SetVertexShader(nullptr);
    g_d3dDevice->SetTexture(0, nullptr);
    g_d3dDevice->SetTexture(1, nullptr);
    g_d3dDevice->SetTexture(2, nullptr);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
    // Sampler states back to the RW-ish linear/wrap default for ALL stages the
    // water pipe touched (0-2). Stage 0 was set to CLAMP + MIPFILTER NONE for
    // the scene copy; leaving it leaked into the next stage-0 consumer bound
    // through the RW cache (tiling building/floor textures then edge-smear
    // instead of wrapping — a three-layer doctrine violation: restore every
    // state you modify). RW re-applies a texture's own filter/addressing bits
    // on the next bind, so this default is safe for every consumer.
    for(int s = 0; s <= 2; s++){
        g_d3dDevice->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        g_d3dDevice->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    // Blend states back to the SA/re3 convention (SRCALPHA/INVSRCALPHA) —
    // tier 0/1 left them explicitly set, and everything after the water draw
    // expects the game defaults.
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)false);
    // Back to the frame's own cull mode (double-sided was water-only).
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)s_savedCull);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)true);
    g_waterParallaxActive = false;
}
