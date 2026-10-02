// HeightFog.hlsl - Crytek exponential height fog (ps_3_0)
// Fullscreen post-process: reads depth, reconstructs world height, blends fog.
// Drawn with s_ffQuad (screen UV 0..1): s0 = padded 2048² front buffer
// (fetch × fbParams.xy), s1 = screen-sized INTZ depth (raw UV).

sampler2D sceneTex : register(s0);
sampler2D depthTex : register(s1);

uniform float4 fogParams   : register(c0); // (density, falloff, startH, maxFog)
uniform float4 fogColor    : register(c1); // fog RGB (config or timecycle horizon)
uniform float4 projInfo    : register(c2); // (recipVW.x, recipVW.y, -nf/(f-n), f/(f-n))
uniform float4 fbParams    : register(c3); // (frontBufferU, frontBufferV, 0, 0)
uniform float4 camPos      : register(c4); // camera world position
uniform float4 camAxisZ    : register(c5); // (right.z, up.z, at.z, 0)

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;

    // Sample depth (screen-sized: raw UV) and scene (padded 2048² fb: scaled UV)
    float depth = tex2D(depthTex, tex).r;
    float3 scene = tex2D(sceneTex, tex * fbParams.xy).rgb;

    // Skip sky (depth at far plane)
    if(depth >= 0.999)
        return float4(scene, 1.0);

    // Linearize depth — guard the divide (degenerate depth == projInfo.w
    // produced +/-INF/NaN, which SM3.0 turns into a black full-screen quad)
    float denom = depth - projInfo.w;
    if(abs(denom) < 1e-9)
        return float4(scene, 1.0);
    float linearDepth = projInfo.z / denom;
    if(!(linearDepth > 0.0))
        return float4(scene, 1.0);

    // Reconstruct view-space position (same convention as SSAO's GetViewPos)
    float2 ndc = tex * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 viewPos = float3(ndc * projInfo.xy * linearDepth, linearDepth);

    // Approximate world height from camera orientation:
    // worldOffset = vx*right + vy*up + linearDepth*forward (camera at-axis).
    // (Sign was '-' before: that produced 2*linearDepth*|at.z| worldZ error
    // on any tilted/aerial camera — fog ignored camera pitch.)
    float worldZ = camPos.z + viewPos.x * camAxisZ.x
                 + viewPos.y * camAxisZ.y
                 + viewPos.z * camAxisZ.z;

    // Unpack fog parameters
    float fogDensity = fogParams.x;
    float heightFalloff = fogParams.y;
    float startHeight = fogParams.z;
    float maxFog = fogParams.w;

    // Crytek exponential height fog
    float heightFactor = exp(-heightFalloff * max(worldZ - startHeight, 0.0));
    float distFactor = 1.0 - exp(-fogDensity * linearDepth);
    float totalFog = saturate(heightFactor * distFactor);
    totalFog = min(totalFog, maxFog);

    // Blend fog with scene
    float3 result = lerp(scene, fogColor.rgb, totalFog);
    return float4(result, 1.0);
}
