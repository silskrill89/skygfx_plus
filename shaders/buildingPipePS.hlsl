// buildingPipePS.hlsl - Consolidated building pixel shaders
// Entry points: main_simple, main_simpleDetail, main_simpleFog, main_xboxBuilding, main_normMapBuilding
// Compile with /E <entry> ps_2_0 for each pass
//
// Merges: simplePS, simpleDetailPS, simpleFogPS, xboxBuildingPS, normMapBuildingPS

// ============================================================
// Pass 0: Simple building (texture * color * colorscale)
// Entry: main_simple
// ============================================================
uniform sampler2D tex_simple : register(s0);
uniform float4 colorscale_simple : register(c0);

struct PS_INPUT_SIMPLE {
    float3 texcoord0 : TEXCOORD0;
    float4 color     : COLOR0;
};

float4 main_simple(PS_INPUT_SIMPLE IN) : COLOR
{
    return tex2D(tex_simple, IN.texcoord0.xy) * IN.color * colorscale_simple.x;
}

// ============================================================
// Pass 1: Building with detail map
// Entry: main_simpleDetail
// ============================================================
uniform sampler2D tex0_detail : register(s0);
uniform sampler2D tex1_detail : register(s1);
uniform sampler2D tex2_detail : register(s2);
uniform float4 colorscale_detail : register(c0);
uniform float detailtile : register(c1);

struct PS_INPUT_DETAIL {
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
};

float4 main_simpleDetail(PS_INPUT_DETAIL IN) : COLOR
{
    float4 final = tex2D(tex0_detail, IN.texcoord0) * IN.color * colorscale_detail.x;
    final.rgb *= tex2D(tex2_detail, IN.texcoord0 * detailtile).rgb * 2.0;
    return final;
}

// ============================================================
// Pass 2: Building with distance fog
// Entry: main_simpleFog
// ============================================================
uniform sampler2D tex_fog : register(s0);
uniform float3 fogcol_fog : register(c0);
float3 campos_fog   : register(c1);
float2 fogdist_fog  : register(c2);

struct PS_INPUT_FOG {
    float3 texcoord0 : TEXCOORD0;
    float3 WorldPos  : TEXCOORD1;
    float4 color     : COLOR0;
};

float4 main_simpleFog(PS_INPUT_FOG IN) : COLOR
{
    float3 ReflVector = IN.WorldPos - campos_fog;
    float fog = clamp((fogdist_fog.x - length(ReflVector)) / fogdist_fog.y, 0.0, 1.0);
    float4 col = tex2D(tex_fog, IN.texcoord0.xy) * IN.color;
    col.rgb = lerp(fogcol_fog, col.rgb, fog);
    return col;
}

// ============================================================
// Pass 3: Xbox building (env map + diffuse)
// Entry: main_xboxBuilding
// ============================================================
uniform sampler2D tex0_xbox : register(s0);
uniform sampler2D tex1_xbox : register(s1);

struct PS_INPUT_XBOX {
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
    float4 envcolor  : COLOR1;
};

float4 main_xboxBuilding(PS_INPUT_XBOX IN) : COLOR
{
    return tex2D(tex0_xbox, IN.texcoord0 * 0.5) * IN.color * 10.0 +
           tex2D(tex1_xbox, IN.texcoord1 * 0.5) * IN.envcolor;
}

// ============================================================
// Pass 4: Normal mapped building
// Entry: main_normMapBuilding
// ============================================================
sampler2D tex0_normmap : register(s0);
sampler2D tex1_normmap : register(s1);

float4 colorscale_nm : register(c0);
float4 nmParams      : register(c1);
float3 ambient_nm    : register(c4);
float3 directCol_nm[7] : register(c5);
float3 directDir_nm[7] : register(c12);
float4 matCol_nm     : register(c19);
float4 surfProps_nm  : register(c20);

#define surfAmb_nm   (surfProps_nm.x)
#define surfDiff_nm  (surfProps_nm.z)
#define intensity    (nmParams.x)
#define debugMode    (nmParams.y)

float3 DoDirLightNm(float3 lightCol, float3 lightDir, float3 N)
{
    float l = max(0.0, dot(N, -lightDir));
    return l * lightCol;
}

float4 main_normMapBuilding(PS_INPUT_SIMPLE IN) : COLOR
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
