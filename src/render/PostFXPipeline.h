#ifndef POSTFX_PIPELINE_H
#define POSTFX_PIPELINE_H

#include <d3d9.h>
#include <d3dx9.h>

class PostFXPipeline {
public:
    struct Config {
        bool enableColorCorrection;
        bool enableSSAO;
        bool enableSMAA;
        bool enableLUT;
        bool useYCbCr;
        bool useGrading;
        
        float ssaoRadius;
        float ssaoPower;
        float ssaoNoiseScale;
        int ssaoSampleCount;
        
        float smaaThreshold;
        int smaaMaxSearchSteps;
        int smaaMaxSearchStepsDiag;
        float smaaCornerRounding;
        
        Config()
            : enableColorCorrection(true)
            , enableSSAO(true)
            , enableSMAA(true)
            , enableLUT(false)
            , useYCbCr(false)
            , useGrading(true)
            , ssaoRadius(0.8f)
            , ssaoPower(1.5f)
            , ssaoNoiseScale(4.0f)
            , ssaoSampleCount(16)
            , smaaThreshold(0.1f)
            , smaaMaxSearchSteps(16)
            , smaaMaxSearchStepsDiag(8)
            , smaaCornerRounding(25.0f) {}
    };

    PostFXPipeline(IDirect3DDevice9* device, int width, int height);
    ~PostFXPipeline();

    void resize(int width, int height);
    void process(RwRaster* inputRaster, RwRaster* outputRaster);
    
    void setConfig(const Config& cfg) { config = cfg; }
    const Config& getConfig() const { return config; }

    void setLUTTexture(IDirect3DTexture9* lutTex) { lutTexture = lutTex; }
    void setDepthTexture(IDirect3DTexture9* depthTex) { depthTexture = depthTex; }
    void setNormalTexture(IDirect3DTexture9* normalTex) { normalTexture = normalTex; }

    void setColorCorrectionParams(const float* redGrade, const float* greenGrade, const float* blueGrade);
    void setYCbCrParams(float lumaScale, float lumaOffset, float cbScale, float cbOffset, float crScale, float crOffset);

private:
    void initResources();
    void releaseResources();
    void createRenderTargets(int width, int height);
    void releaseRenderTargets();
    void createShaders();
    void releaseShaders();

    void passColorCorrection(IDirect3DSurface9* src, IDirect3DSurface9* dst);
    void passSSAO(IDirect3DSurface9* src, IDirect3DSurface9* dst);
    void passSMAA(IDirect3DSurface9* src, IDirect3DSurface9* dst);
    void passLUT(IDirect3DSurface9* src, IDirect3DSurface9* dst);
    void passCopy(IDirect3DSurface9* src, IDirect3DSurface9* dst);

    void setCommonRenderStates();
    void restoreRenderStates();
    void drawFullscreenQuad();

    IDirect3DDevice9* device;
    Config config;
    int width, height;

    IDirect3DTexture9* rtTex[2];
    IDirect3DSurface9* rtSurf[2];
    int currentRT;

    IDirect3DTexture9* depthTexture;
    IDirect3DTexture9* normalTexture;
    IDirect3DTexture9* lutTexture;
    IDirect3DTexture9* ssaoNoiseTex;

    void* colorCorrectionPS;
    void* ssaoPS;
    void* smaaPS;
    void* lutPS;
    void* copyPS;

    float redGrade[4], greenGrade[4], blueGrade[4];
    float ycbcrParams[6];

    DWORD savedRenderStates[16];
    bool statesSaved;
};

#endif