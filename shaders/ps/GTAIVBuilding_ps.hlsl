<<<<<<< HEAD
// GTA IV Building Pixel Shader
// Adapted from RAGE megashader.fxh

struct VS_OUTPUT {
    float4 pos : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float3 worldNormal : TEXCOORD1;
    float2 envTexcoord : TEXCOORD2;
};

sampler2D diffuseSampler : register(s0);
sampler2D envSampler : register(s1);

float4 psParams : register(c0); // {colorScale, detailTile, wetAmount, unused}

float4 main(VS_OUTPUT input) : COLOR0
{
    float4 diffuse = tex2D(diffuseSampler, input.texcoord);

    float3 color = input.color.rgb * diffuse.rgb;

    float wetness = psParams.z;
    if(wetness > 0.01){
        float4 envColor = tex2D(envSampler, input.envTexcoord);
        float NdotV = saturate(dot(input.worldNormal, float3(0,0,1)));
        float fresnel = pow(1.0 - NdotV, 3.0);
        color = lerp(color, envColor.rgb * 0.5, fresnel * wetness * 0.3);
    }

    return float4(color, diffuse.a);
}

technique GTAIVBuilding {
    pass P0 {
        PixelShader = compile ps_3_0 main();
    }
=======
sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);

float4		fogColor : register(c0);

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
	float4 baseColor = tex2D(tex0, IN.texcoord0) * IN.color;

	float3 envColor = tex2D(tex1, IN.WorldNormal.xy * 0.5 + 0.5).rgb;
	baseColor.rgb = lerp(baseColor.rgb, envColor, IN.envColor.a);

	return baseColor;
>>>>>>> master
}
