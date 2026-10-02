// PipeChain.hlsl - 4-pass post chain + classify pack mode (ps_3_0)
//
// pipeParams.x pass select:
//   0 = input, 1 = mid_a, 2 = mid_b, 3 = output (chain)
//   9 = classify pack mode — forced as PS by the building/vehicle pipes
//       around a second draw of their mesh (see PipeChain_ClassifyBegin).
//
// UV domains: the chain is drawn with s_ffQuad (screen UV 0..1).
//   s0 scene      = padded 2048² front buffer  -> fetch × fbParams.xy
//   s1 normal     = half-res screen buffer     -> raw UV
//   s2 depth      = screen-sized INTZ          -> raw UV
//   s3/s4/A,B     = screen-sized RTs           -> raw UV

sampler2D sceneTex    : register(s0); // scene color (front buffer)
sampler2D normalTex   : register(s1); // normal buffer (stereo-derived)
sampler2D depthTex    : register(s2); // depth buffer (INTZ)
sampler2D intermediate: register(s3); // intermediate A or B (ping-pong)
sampler2D classifyTex : register(s4); // BRDF pack (R=class G=gloss B=spec A=metal)

float4 pipeParams  : register(c0); // (passIdx, time, intensity, unused)
float4 packParams  : register(c1); // pass 9: (class/255, gloss, spec, metal)
float4 screenSize  : register(c2); // (width, height, 1/width, 1/height)
float4 fbParams    : register(c3); // (frontBufferU, frontBufferV, 0, 0)
float4 projInfo    : register(c4); // (recipVW.x, recipVW.y, -nf/(f-n), f/(f-n))

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
    float3 scene = tex2D(sceneTex, IN.TexCoord * fbParams.xy).rgb;
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
    float3 c = tex2D(sceneTex, IN.TexCoord * fbParams.xy).rgb;
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
    float4 center = tex2D(sceneTex, IN.TexCoord * fbParams.xy);
    float centerEdge = tex2D(intermediate, IN.TexCoord).a;

    // Bilateral blur - sharp at edges, smooth in flat areas
    float3 sum = center.rgb;
    float totalW = 1.0;
    int radius = 2;
    for(int x = -2; x <= 2; x++){
        for(int y = -2; y <= 2; y++){
            if(x == 0 && y == 0) continue;
            float2 off = float2(x, y) * px;
            float4 s = tex2D(sceneTex, (IN.TexCoord + off) * fbParams.xy);
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

// ---- Pass 4: Output - BRDF-weighted screen-space reflection composite ----
PS_OUTPUT pass_output(PS_INPUT IN)
{
    PS_OUTPUT OUT;
    float2 uv = IN.TexCoord;
    float3 scene = tex2D(sceneTex, uv * fbParams.xy).rgb;
    float3 processed = tex2D(intermediate, uv).rgb;
    float edge = tex2D(intermediate, uv).a;

    // Original edge-aware blend (kept: subtle contrast/AA at intensity).
    // x0.5 decouples the blur/contrast lerp from the reflection weight below
    // so the default intensity (0.3) doesn't over-blur the frame — reflections
    // keep their own BRDF-weighted blendFactor.
    float blendFactor = pipeParams.z * 0.5 * (1.0 - edge * 0.8);
    float3 result = lerp(scene, processed, blendFactor);

    // --- BRDF material-classified reflection --------------------------------
    // pack = (surfaceType/255, glossiness, specular/F0, metallicness) written
    // by the geometry pipes; pack == 0 means never drawn (sky/background) =>
    // weight 0. Weights come from the SAME brdfLibrary values the pipes feed
    // into pipeUploadPBR: dull wood/road (spec 0.04, gloss 0.3) ~ invisible,
    // chrome (0.56/0.9) strong, car paint (0.04/0.85) a clearcoat hint.
    float4 pack = tex2D(classifyTex, uv);
    if(pack.y > 0.001 || pack.z > 0.001){
        float3 n = tex2D(normalTex, uv).rgb * 2.0 - 1.0;
        float nlen = length(n);
        float d = tex2D(depthTex, uv).r;
        if(nlen > 0.5 && d < 0.999){
            n /= nlen;
            float denom = d - projInfo.w;
            if(abs(denom) > 1e-9){
                float lin = projInfo.z / denom;
                if(lin > 0.01 && projInfo.x > 1e-7 && projInfo.y > 1e-7){
                    // View-space position (SSAO/HeightFog convention)
                    float2 ndc = uv * 2.0 - 1.0;
                    ndc.y = -ndc.y;
                    float3 vp = float3(ndc * projInfo.xy * lin, lin);
                    float3 V = normalize(vp);       // eye -> surface
                    float3 R = reflect(V, n);       // reflected dir (view space)

                    // March a fixed 2m along the reflection and re-project:
                    // ndc = view.xy / (view.z * recipVW)
                    float3 pr = vp + R * 2.0;
                    if(pr.z > 0.01){
                        float2 ndc2 = pr.xy / (pr.z * projInfo.xy);
                        float2 envUV = float2(ndc2.x * 0.5 + 0.5,
                                               0.5 - ndc2.y * 0.5);
                        // Off-screen reflection ray: REJECT instead of the old
                        // saturate(), which smeared the clamped edge texel
                        // across every silhouette that reflected off-screen.
                        if(envUV.x > 0.0 && envUV.x < 1.0 &&
                           envUV.y > 0.0 && envUV.y < 1.0){
                            float3 env = tex2D(sceneTex, envUV * fbParams.xy).rgb;

                            float NdV = saturate(dot(n, -V));
                            float fres = 0.25 + 0.75 * pow(1.0 - NdV, 3.0);
                            float brdfW = saturate(pack.z * 1.6) * pack.y;
                            float reflBlend = saturate(pipeParams.z * brdfW * fres
                                                       * (1.0 - edge * 0.8));
                            result = lerp(result, env, reflBlend);
                        }
                    }
                }
            }
        }
    }

    OUT.Color = float4(result, 1.0);
    return OUT;
}

// ---- Main entry - dispatch by pass index ----
PS_OUTPUT main(PS_INPUT IN)
{
    // Classify pack mode: the geometry pipes force this PS with c0.x = 9 and
    // c1 = pack floats; RGB = BRDF pack, A = the mesh's diffuse-texture alpha
    // (s0 is still the mesh diffuse during the classify re-draw). The pipes
    // run with alpha-test ON (pipelinecommon forces
    // D3DRS_ALPHATESTENABLE=TRUE); packParams.a — metallicness, ~0 on
    // buildings — failed the test and DISCARDED every solid pixel, so the
    // classify buffer never filled. Echoing diffuse alpha reproduces the main
    // draw's cutout silhouette exactly (PBR_Lighting returns albedo.a,
    // VehiclePBR returns diff.a).
    if(pipeParams.x > 8.5){
        PS_OUTPUT OUT;
        OUT.Color = float4(packParams.rgb, tex2D(sceneTex, IN.TexCoord).a);
        return OUT;
    }
    int passIdx = (int)pipeParams.x;
    if(passIdx == 0) return pass_input(IN);
    if(passIdx == 1) return pass_mid_a(IN);
    if(passIdx == 2) return pass_mid_b(IN);
    return pass_output(IN);
}
