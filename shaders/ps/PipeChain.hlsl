// PipeChain.hlsl - 4-pass post-processing chain
// ps_3_0
// Uses pipeParams.x to select pass: 0=input, 1=mid_a, 2=mid_b, 3=output
// Middle two passes (1,2) act in tandem - split from what would be a single pass

sampler2D sceneTex    : register(s0); // scene color (front buffer)
sampler2D normalTex   : register(s1); // normal buffer (stereo-derived)
sampler2D depthTex    : register(s2); // depth buffer (INTZ)
sampler2D intermediate: register(s3); // intermediate A or B (ping-pong)

float4 pipeParams  : register(c0); // (passIdx, time, intensity, unused)
float4 projInfo    : register(c1); // projection info for depth reconstruction
float4 screenSize  : register(c2); // (width, height, 1/width, 1/height)

struct PS_INPUT {
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT {
    float4 Color : COLOR0;
};

// ---- Pass 0: Input - decode scene, prepare for chain ----
PS_OUTPUT pass_input(PS_INPUT IN)
{
    PS_OUTPUT OUT;
    float3 scene = tex2D(sceneTex, IN.TexCoord).rgb;
    float3 normal = tex2D(normalTex, IN.TexCoord).rgb;
    // Store scene in RGB, edge indicator from normal in A
    float edge = 1.0 - saturate(abs(normal.x - 0.5) + abs(normal.y - 0.5));
    OUT.Color = float4(scene, edge);
    return OUT;
}

// ---- Pass 2a: Mid-tandem first half - edge-aware contrast detection ----
PS_OUTPUT pass_mid_a(PS_INPUT IN)
{
    PS_OUTPUT OUT;
    float2 px = screenSize.zw;
    float3 c = tex2D(sceneTex, IN.TexCoord).rgb;
    float3 n = tex2D(normalTex, IN.TexCoord).rgb;

    // Sample normal neighbors for edge detection
    float3 nR = tex2D(normalTex, IN.TexCoord + float2(px.x, 0)).rgb;
    float3 nL = tex2D(normalTex, IN.TexCoord - float2(px.x, 0)).rgb;
    float3 nU = tex2D(normalTex, IN.TexCoord + float2(0, px.y)).rgb;
    float3 nD = tex2D(normalTex, IN.TexCoord - float2(0, px.y)).rgb;

    // Cross-gradient edge detection on normals
    float3 edgeH = abs(nR - nL);
    float3 edgeV = abs(nU - nD);
    float edge = saturate(dot(edgeH + edgeV, float3(1, 1, 1)) * 2.0);

    // Depth-based edge
    float dC = tex2D(depthTex, IN.TexCoord).r;
    float dR = tex2D(depthTex, IN.TexCoord + float2(px.x, 0)).r;
    float dL = tex2D(depthTex, IN.TexCoord - float2(px.x, 0)).r;
    float dU = tex2D(depthTex, IN.TexCoord + float2(0, px.y)).r;
    float dD = tex2D(depthTex, IN.TexCoord - float2(0, px.y)).r;
    float depthEdge = saturate(abs(dR - dL) + abs(dU - dD) * 100.0);

    edge = max(edge, depthEdge);

    // Store scene in RGB, combined edge in A
    OUT.Color = float4(c, edge);
    return OUT;
}

// ---- Pass 2b: Mid-tandem second half - bilateral blur using edge ----
PS_OUTPUT pass_mid_b(PS_INPUT IN)
{
    PS_OUTPUT OUT;
    float2 px = screenSize.zw;
    float4 center = tex2D(sceneTex, IN.TexCoord);
    float centerEdge = tex2D(intermediate, IN.TexCoord).a;

    // Bilateral blur - sharp at edges, smooth in flat areas
    float3 sum = center.rgb;
    float totalW = 1.0;
    int radius = 2;
    for(int x = -2; x <= 2; x++){
        for(int y = -2; y <= 2; y++){
            if(x == 0 && y == 0) continue;
            float2 off = float2(x, y) * px;
            float4 s = tex2D(sceneTex, IN.TexCoord + off);
            float sEdge = tex2D(intermediate, IN.TexCoord + off).a;

            // Weight: spatial + edge-aware
            float spatialW = 1.0 / (1.0 + abs(x) + abs(y));
            float edgeW = 1.0 / (1.0 + abs(centerEdge - sEdge) * 10.0);
            float w = spatialW * edgeW;

            sum += s.rgb * w;
            totalW += w;
        }
    }

    OUT.Color = float4(sum / totalW, centerEdge);
    return OUT;
}

// ---- Pass 4: Output - final composite ----
PS_OUTPUT pass_output(PS_INPUT IN)
{
    PS_OUTPUT OUT;
    float3 scene = tex2D(sceneTex, IN.TexCoord).rgb;
    float3 processed = tex2D(intermediate, IN.TexCoord).rgb;
    float edge = tex2D(intermediate, IN.TexCoord).a;

    // Blend processed result with original, stronger in flat areas
    float blendFactor = pipeParams.z * (1.0 - edge * 0.8);
    OUT.Color = float4(lerp(scene, processed, blendFactor), 1.0);
    return OUT;
}

// ---- Main entry - dispatch by pass index ----
PS_OUTPUT main(PS_INPUT IN)
{
    int passIdx = (int)pipeParams.x;
    if(passIdx == 0) return pass_input(IN);
    if(passIdx == 1) return pass_mid_a(IN);
    if(passIdx == 2) return pass_mid_b(IN);
    return pass_output(IN);
}
