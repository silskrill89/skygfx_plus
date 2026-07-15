#ifndef EDGE_DETECTION_H
#define EDGE_DETECTION_H

#include <d3d9.h>
#include <d3dx9.h>

class EdgeDetection {
public:
    enum EdgeInput {
        EDGE_INPUT_LUMA,
        EDGE_INPUT_COLOR,
        EDGE_INPUT_DEPTH,
        EDGE_INPUT_NORMAL,
        EDGE_INPUT_NORMAL_DEPTH
    };

    enum EdgeOutput {
        EDGE_OUTPUT_RG,
        EDGE_OUTPUT_RGBA
    };

    struct Config {
        float threshold;
        float depthThreshold;
        float normalThreshold;
        int maxSearchSteps;
        int maxSearchStepsDiag;
        float cornerRounding;
        bool predication;
        float predicationThreshold;
        float predicationScale;
        float predicationStrength;
        bool localContrastAdaptation;
        float localContrastFactor;
        
        Config() 
            : threshold(0.1f)
            , depthThreshold(0.01f)
            , normalThreshold(0.1f)
            , maxSearchSteps(16)
            , maxSearchStepsDiag(8)
            , cornerRounding(25.0f)
            , predication(false)
            , predicationThreshold(0.01f)
            , predicationScale(2.0f)
            , predicationStrength(0.4f)
            , localContrastAdaptation(true)
            , localContrastFactor(2.0f) {}
    };

    struct ExternalStorage {
        IDirect3DTexture9* edgeTex;
        IDirect3DSurface9* edgeSurface;
        IDirect3DTexture9* blendTex;
        IDirect3DSurface9* blendSurface;
        
        ExternalStorage(IDirect3DTexture9* eTex = nullptr, IDirect3DSurface9* eSurf = nullptr,
                        IDirect3DTexture9* bTex = nullptr, IDirect3DSurface9* bSurf = nullptr)
            : edgeTex(eTex), edgeSurface(eSurf), blendTex(bTex), blendSurface(bSurf) {}
    };

    EdgeDetection(IDirect3DDevice9* device, int width, int height, const Config& config = Config(), 
                  const ExternalStorage& storage = ExternalStorage());
    ~EdgeDetection();

    void detectEdges(IDirect3DTexture9* input, IDirect3DTexture9* normalTex, IDirect3DTexture9* depthTex, 
                     EdgeInput input, IDirect3DTexture9* predicationTex = nullptr);
    void calculateBlendingWeights();
    void neighborhoodBlending(IDirect3DTexture9* src, IDirect3DSurface9* dst);

    void go(IDirect3DTexture9* input, IDirect3DTexture9* normalTex, IDirect3DTexture9* depthTex,
            IDirect3DTexture9* src, IDirect3DSurface9* dst, EdgeInput input, 
            IDirect3DTexture9* predicationTex = nullptr);

    float getThreshold() const { return config.threshold; }
    void setThreshold(float t) { config.threshold = t; }

    float getNormalThreshold() const { return config.normalThreshold; }
    void setNormalThreshold(float t) { config.normalThreshold = t; }

    int getMaxSearchSteps() const { return config.maxSearchSteps; }
    void setMaxSearchSteps(int s) { config.maxSearchSteps = s; }

    int getMaxSearchStepsDiag() const { return config.maxSearchStepsDiag; }
    void setMaxSearchStepsDiag(int s) { config.maxSearchStepsDiag = s; }

    float getCornerRounding() const { return config.cornerRounding; }
    void setCornerRounding(float c) { config.cornerRounding = c; }

    IDirect3DTexture9* getEdgeTexture() const { return edgeTex; }
    IDirect3DTexture9* getBlendTexture() const { return blendTex; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    void loadAreaTex();
    void loadSearchTex();
    void edgeDetectionPass(IDirect3DTexture9* input, IDirect3DTexture9* normalTex, 
                           IDirect3DTexture9* depthTex, EdgeInput input, 
                           IDirect3DTexture9* predicationTex);
    void blendingWeightsCalculationPass();
    void neighborhoodBlendingPass(IDirect3DTexture9* src, IDirect3DSurface9* dst);
    void quad(int width, int height);

    IDirect3DDevice9* device;
    ID3DXEffect* effect;
    IDirect3DVertexDeclaration9* vertexDeclaration;

    IDirect3DTexture9* edgeTex;
    IDirect3DSurface9* edgeSurface;
    bool releaseEdgeResources;

    IDirect3DTexture9* blendTex;
    IDirect3DSurface9* blendSurface;
    bool releaseBlendResources;

    IDirect3DTexture9* areaTex;
    IDirect3DTexture9* searchTex;

    Config config;
    int width, height;

    D3DXHANDLE thresholdHandle;
    D3DXHANDLE depthThresholdHandle;
    D3DXHANDLE normalThresholdHandle;
    D3DXHANDLE maxSearchStepsHandle;
    D3DXHANDLE maxSearchStepsDiagHandle;
    D3DXHANDLE cornerRoundingHandle;
    D3DXHANDLE areaTexHandle;
    D3DXHANDLE searchTexHandle;
    D3DXHANDLE colorTexHandle;
    D3DXHANDLE normalTexHandle;
    D3DXHANDLE depthTexHandle;
    D3DXHANDLE predicationTexHandle;
    D3DXHANDLE edgesTexHandle;
    D3DXHANDLE blendTexHandle;
    D3DXHANDLE lumaEdgeDetectionHandle;
    D3DXHANDLE colorEdgeDetectionHandle;
    D3DXHANDLE depthEdgeDetectionHandle;
    D3DXHANDLE normalEdgeDetectionHandle;
    D3DXHANDLE normalDepthEdgeDetectionHandle;
    D3DXHANDLE blendWeightCalculationHandle;
    D3DXHANDLE neighborhoodBlendingHandle;
};

#endif