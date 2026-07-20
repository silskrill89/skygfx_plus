// normMapBuildingPS.hlsl - Normal-mapped building lighting (ps_3_0)
// Compiled via /E main -> normMapBuildingPS.cso
// Used by the rwnormal (DK22Pac) building path.
uniform sampler2D tex0_normmap : register(s0);
uniform sampler2D tex1_normmap : register(s1);

uniform float4 colorscale_nm : register(c0);
uniform float4 nmParams      : register(c1);
uniform float3 ambient_nm    : register(c4);
uniform float3 directCol_nm[7] : register(c5);
uniform float3 directDir_nm[7] : register(c12);
uniform float4 matCol_nm     : register(c19);
uniform float4 surfProps_nm  : register(c20);

#define surfAmb_nm   (surfProps_nm.x)
#define surfDiff_nm  (surfProps_nm.z)
#define intensity    (nmParams.x)
#define debugMode    (nmParams.y)

float3 DoDirLightNm(float3 lightCol, float3 lightDir, float3 N)
{
    float l = max(0.0, dot(N, -lightDir));
    return l * lightCol;
}

struct PS_INPUT_SIMPLE {
    float3 texcoord0 : TEXCOORD0;
    float4 color     : COLOR0;
};

float4 main(PS_INPUT_SIMPLE IN) : COLOR
{
    float4 baseColor = tex2D(tex0_normmap, IN.texcoord0) * IN.color * colorscale_nm.x;

    if(intensity <= 0.0)
        return baseColor;

    float3 normal;
    normal.xy = tex2D(tex1_normmap, IN.texcoord0).wy * 2.0 - 1.0;
    normal.z = sqrt(1.0 - normal.x * normal.x - normal.y * normal.y);
    normal = normalize(normal);

    float3 N = normalize(IN.texcoord0);

    float3 lighting = ambient_nm * surfAmb_nm;
    for(int i = 0; i < 7; i++)
        lighting += DoDirLightNm(directCol_nm[i], directDir_nm[i], N);

    float nmDiffuse = saturate(dot(normal, normalize(float3(0.3, -0.5, 0.8))));
    lighting += nmDiffuse * surfDiff_nm * 0.3;

    baseColor.rgb *= lerp(1.0, lighting, intensity);

    if(debugMode >= 1.0 && debugMode < 2.0)
        return float4(normal * 0.5 + 0.5, 1.0);
    if(debugMode >= 2.0)
        return float4(lighting.x, lighting.y, lighting.z, 1.0);

    return baseColor;
}
