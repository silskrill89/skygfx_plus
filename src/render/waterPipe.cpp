#include "skygfx.h"
#include <d3d9.h>

// ============================================================
// Water Pipe - Procedural PBR water
// VS: Water_VS.hlsl  |  PS: Water_Parallax.hlsl
// Fully procedural normals — no texture dependencies.
// ============================================================

bool g_waterParallaxActive = false;
static float g_waterTime = 0.0f;

extern void *Water_Parallax;
extern void *Water_VS;

static RwCamera *&ccamera = *(RwCamera**)0xC170C4;
static IDirect3DDevice9 *g_d3dDevice = nullptr;
static IDirect3DTexture9 *g_sceneTexture = nullptr;
static int g_sceneWidth = 0, g_sceneHeight = 0;

struct GerstnerWave { float dirX, dirY, steepness, wavelength; };
static GerstnerWave g_waves[3] = {
    { 1.0f, 0.0f, 0.25f, 60.0f },
    { 0.3f, 1.0f, 0.15f, 31.0f },
    { -0.2f, 0.8f, 0.1f, 18.0f },
};

void waterPipe_init(void)
{
    g_waterParallaxActive = false;
    g_waterTime = 0.0f;
}

void waterPipe_shutdown(void)
{
    g_waterParallaxActive = false;
    if(g_sceneTexture){ g_sceneTexture->Release(); g_sceneTexture = nullptr; }
}

static void EnsureSceneTexture(int w, int h)
{
    if(g_sceneTexture && g_sceneWidth == w && g_sceneHeight == h) return;
    if(g_sceneTexture){ g_sceneTexture->Release(); g_sceneTexture = nullptr; }
    HRESULT hr = g_d3dDevice->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_sceneTexture, nullptr);
    if(FAILED(hr) || !g_sceneTexture) return;
    g_sceneWidth = w; g_sceneHeight = h;
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

void waterPipe_setRenderState(void)
{
    if(!g_d3dDevice)
        g_d3dDevice = *(IDirect3DDevice9**)0xC97C28;
    if(!g_d3dDevice) return;
    if(!config->waterParallaxEnable || !Water_Parallax || !Water_VS) return;

    // Copy scene for refraction
    D3DVIEWPORT9 vp;
    g_d3dDevice->GetViewport(&vp);
    EnsureSceneTexture(vp.Width, vp.Height);
    CopySceneToTexture();

    g_waterParallaxActive = true;

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
    RwD3D9SetPixelShader(Water_Parallax);

    // VS constants
    float wvp[16];
    _rwD3D9VSGetComposedTransformMatrix(wvp);
    RwD3D9SetVertexShaderConstant(0, wvp, 4);

    float world[16] = {0};
    world[0] = world[5] = world[10] = world[15] = 1.0f;
    RwD3D9SetVertexShaderConstant(4, world, 4);

    RwV3d camPos = {0, 0, 0};
    RwFrame *camFrame = ccamera ? RwCameraGetFrame(ccamera) : NULL;
    if(camFrame){
        RwMatrix *camLTM = RwFrameGetLTM(camFrame);
        if(camLTM)
            camPos = camLTM->pos;
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
    float sunDirNeg[4] = {-sunDirVec.x, -sunDirVec.y, -sunDirVec.z, 3.0f};
    RwD3D9SetVertexShaderConstant(9, sunDirNeg, 1);

    float w1[4] = {g_waves[0].dirX, g_waves[0].dirY, g_waves[0].steepness, 0.0f};
    RwD3D9SetVertexShaderConstant(10, w1, 1);
    float w2[4] = {g_waves[1].dirX, g_waves[1].dirY, g_waves[1].steepness, 0.0f};
    RwD3D9SetVertexShaderConstant(11, w2, 1);
    float w3[4] = {g_waves[2].dirX, g_waves[2].dirY, g_waves[2].steepness, 0.0f};
    RwD3D9SetVertexShaderConstant(12, w3, 1);
    float wProps[4] = {g_waves[0].wavelength, g_waves[1].wavelength, g_waves[2].wavelength, 1.0f};
    RwD3D9SetVertexShaderConstant(13, wProps, 1);

    // PS constants
    float timeP[4] = {g_waterTime, 1.0f, config->waterFoamThreshold, config->waterFoamSoftness};
    RwD3D9SetPixelShaderConstant(0, timeP, 1);

    float waterP[4] = {config->waterNormalStrength, config->waterParallaxScale, config->waterFresnelPower, 0.0f};
    RwD3D9SetPixelShaderConstant(1, waterP, 1);

    float specP[4] = {512.0f, 1.0f, 0.45f, 0.0f};
    RwD3D9SetPixelShaderConstant(2, specP, 1);

    float shallow[4] = {0.13f, 0.45f, 0.55f, 0.0f};
    RwD3D9SetPixelShaderConstant(3, shallow, 1);

    float deep[4] = {0.01f, 0.03f, 0.08f, 0.0f};
    RwD3D9SetPixelShaderConstant(4, deep, 1);

    float screenP[4] = {(float)g_sceneWidth, (float)g_sceneHeight,
                        1.0f / (float)g_sceneWidth, 1.0f / (float)g_sceneHeight};
    RwD3D9SetPixelShaderConstant(5, screenP, 1);
    RwD3D9SetPixelShaderConstant(6, sunDirNeg, 1);
    float camPosPS[4] = {camPos.x, camPos.y, camPos.z, 0.0f};
    RwD3D9SetPixelShaderConstant(7, camPosPS, 1);

    float nearClip = 0.5f, farClip = 1000.0f;
    if(ccamera){
        nearClip = RwCameraGetNearClipPlane(ccamera);
        farClip = RwCameraGetFarClipPlane(ccamera);
    }
    float clipP[4] = {nearClip, farClip, 0.0f, 0.0f};
    RwD3D9SetPixelShaderConstant(8, clipP, 1);

    // Only bind scene texture — everything else is procedural
    g_d3dDevice->SetTexture(0, g_sceneTexture);

    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)true);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)false);
}

void waterPipe_restoreRenderState(void)
{
    if(!g_waterParallaxActive) return;
    RwD3D9SetPixelShader(nullptr);
    RwD3D9SetVertexShader(nullptr);
    g_d3dDevice->SetTexture(0, nullptr);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)false);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)true);
    g_waterParallaxActive = false;
}
