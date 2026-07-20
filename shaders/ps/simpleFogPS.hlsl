// simpleFogPS.hlsl - Building distance fog (ps_3_0)
// Compiled via /E main -> simpleFogPS.cso
// Used by the spheremap/fog building callback.
uniform sampler2D tex_fog : register(s0);
uniform float3 fogcol_fog : register(c0);
uniform float3 campos_fog : register(c1);
uniform float2 fogdist_fog : register(c2);

struct PS_INPUT_FOG {
    float3 texcoord0 : TEXCOORD0;
    float3 WorldPos  : TEXCOORD1;
    float4 color     : COLOR0;
};

float4 main(PS_INPUT_FOG IN) : COLOR
{
    float3 ReflVector = IN.WorldPos - campos_fog;
    float fog = clamp((fogdist_fog.x - length(ReflVector)) / fogdist_fog.y, 0.0, 1.0);
    float4 col = tex2D(tex_fog, IN.texcoord0.xy) * IN.color;
    col.rgb = lerp(fogcol_fog, col.rgb, fog);
    return col;
}
