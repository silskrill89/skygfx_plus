// xboxBuildingPS.hlsl - Xbox building (env map + diffuse) (ps_3_0)
// Compiled via /E main -> xboxBuildingPS.cso
// Used by the Xbox building pipe for env-mapped materials.
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
    return tex2D(tex0_xbox, IN.texcoord0 * 0.5) * IN.color * 10.0 +
           tex2D(tex1_xbox, IN.texcoord1 * 0.5) * IN.envcolor;
}
