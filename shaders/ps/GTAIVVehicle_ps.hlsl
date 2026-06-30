<<<<<<< HEAD
// GTA IV Vehicle Pixel Shader
// Adapted from RAGE vehicle_common.fxh

struct VS_OUTPUT {
    float4 pos : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float3 worldNormal : TEXCOORD1;
    float3 viewDir : TEXCOORD2;
    float2 envTexcoord : TEXCOORD3;
    float fresnel : TEXCOORD4;
};

sampler2D diffuseSampler : register(s0);
sampler2D envSampler : register(s1);

float4 fxParams : register(c0);  // {fxSwitch, shininess, specularity, lightmult}
float4 psParams : register(c1);  // {colorScale, unused, unused, unused}

float4 main(VS_OUTPUT input) : COLOR0
{
    float4 diffuse = tex2D(diffuseSampler, input.texcoord);
    float4 envColor = tex2D(envSampler, input.envTexcoord);

    float specularity = fxParams.z;
    float3 color = input.color.rgb * diffuse.rgb;
    color += envColor.rgb * input.fresnel * specularity * fxParams.w;

    color *= psParams.x;

    return float4(color, diffuse.a * input.color.a);
}

technique GTAIVVehicle {
    pass P0 {
        PixelShader = compile ps_3_0 main();
    }
=======
sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);

struct PS_INPUT
{
	float2 texcoord0	: TEXCOORD0;
	float3 WorldNormal	: TEXCOORD1;
	float3 WorldPos		: TEXCOORD2;
	float4 color		: COLOR0;
	float4 envColor		: COLOR1;
};

float4 main(PS_INPUT IN) : COLOR
{
	float4 diff = tex2D(tex0, IN.texcoord0) * IN.color;
	float4 env = tex2D(tex1, IN.texcoord0);
	float4 color = lerp(diff, env * IN.envColor, IN.envColor.a) + diff;
	color.a = diff.a;
	return color;
>>>>>>> master
}
