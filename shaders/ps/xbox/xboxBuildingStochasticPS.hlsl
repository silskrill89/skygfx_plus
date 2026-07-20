// xboxBuildingStochasticPS.hlsl - Stochastic Xbox building (env map + diffuse) (ps_3_0)
// Compiled via /E main -> xboxBuildingStochasticPS.cso
// Used by the Xbox building pipe for env-mapped materials with stochastic sampling.
float2 hash2D2D_stoch(float2 s)
{
    return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)), dot(s, float2(269.5, 183.3))), 3.14159)) * 43758.5453);
}

float4 tex2DStochastic(sampler2D tex, float2 UV)
{
    float2 jitter = hash2D2D_stoch(UV * 100.0) * 0.01;
    return tex2D(tex, UV + jitter);
}

uniform sampler2D tex0_xbox : register(s0);
uniform sampler2D tex1_xbox : register(s1);

struct PS_INPUT_XBOX {
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
    float4 envcolor  : COLOR1;
};

float4 main(PS_INPUT_XBOX IN) : COLOR
{
    return tex2DStochastic(tex0_xbox, IN.texcoord0 * 1.2) * IN.color +
           tex2D(tex1_xbox, IN.texcoord1) * IN.envcolor;
}
