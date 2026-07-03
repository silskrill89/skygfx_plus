// stochasticBuildingPS.hlsl - Stochastic building pixel shaders
// Entry points: main_simpleStochastic, main_simpleDetailStochastic, main_xboxBuildingStochastic
// Compile with /E <entry> ps_2_0 for each pass
//
// Merges: simpleStochasticPS, simpleDetailStochasticPS, xboxBuildingStochasticPS
// No external dependencies - inline jitter

float2 hash2D2D_stoch(float2 s)
{
    return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)), dot(s, float2(269.5, 183.3))), 3.14159)) * 43758.5453);
}

float4 tex2DStochastic(sampler2D tex, float2 UV)
{
    float2 jitter = hash2D2D_stoch(UV * 100.0) * 0.01;
    return tex2D(tex, UV + jitter);
}

// ============================================================
// Pass 0: Stochastic simple building
// Entry: main_simpleStochastic
// ============================================================
uniform sampler2D tex_simple : register(s0);
uniform float4 colorscale_simple : register(c0);

struct PS_INPUT_SIMPLE {
    float3 texcoord0 : TEXCOORD0;
    float4 color     : COLOR0;
};

float4 main_simpleStochastic(PS_INPUT_SIMPLE IN) : COLOR
{
    return tex2DStochastic(tex_simple, IN.texcoord0.xy * 1.2) * IN.color * colorscale_simple.x;
}

// ============================================================
// Pass 1: Stochastic building with detail map
// Entry: main_simpleDetailStochastic
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

float4 main_simpleDetailStochastic(PS_INPUT_DETAIL IN) : COLOR
{
    float4 final = tex2DStochastic(tex0_detail, IN.texcoord0 * 1.2) * IN.color * colorscale_detail.x;
    final.rgb *= tex2DStochastic(tex2_detail, IN.texcoord0 * detailtile).rgb * 2.0;
    return final;
}

// ============================================================
// Pass 2: Stochastic Xbox building (env map + diffuse)
// Entry: main_xboxBuildingStochastic
// ============================================================
uniform sampler2D tex0_xbox : register(s0);
uniform sampler2D tex1_xbox : register(s1);

struct PS_INPUT_XBOX {
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
    float4 envcolor  : COLOR1;
};

float4 main_xboxBuildingStochastic(PS_INPUT_XBOX IN) : COLOR
{
    return tex2DStochastic(tex0_xbox, IN.texcoord0 * 1.2) * IN.color +
           tex2D(tex1_xbox, IN.texcoord1) * IN.envcolor;
}
