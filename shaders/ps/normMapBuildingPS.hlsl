sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);

float4		colorscale : register(c0);
float4		nmParams : register(c1);
float3		ambient : register(c4);
float3		directCol[7] : register(c5);
float3		directDir[7] : register(c12);
float4		matCol : register(c19);
float4		surfProps : register(c20);

#define surfAmb		(surfProps.x)
#define surfDiff	(surfProps.z)
#define intensity	(nmParams.x)
#define debugMode	(nmParams.y)

struct PS_INPUT
{
	float2 texcoord0	: TEXCOORD0;
	float3 WorldNormal	: TEXCOORD1;
	float4 color		: COLOR0;
};

float3 DoDirLight(float3 lightCol, float3 lightDir, float3 N)
{
	float l = max(0.0, dot(N, -lightDir));
	return l * lightCol;
}

float4 main(PS_INPUT IN) : COLOR
{
	float4 baseColor = tex2D(tex0, IN.texcoord0) * IN.color * colorscale.x;

	if(intensity <= 0.0)
		return baseColor;

	float3 normal;
	normal.xy = tex2D(tex1, IN.texcoord0).wy * 2.0 - 1.0;
	normal.z = sqrt(1.0 - normal.x*normal.x - normal.y*normal.y);
	normal = normalize(normal);

	float3 N = normalize(IN.WorldNormal);

	float3 lighting = ambient * surfAmb;
	for(int i = 0; i < 7; i++)
		lighting += DoDirLight(directCol[i], directDir[i], N);

	// apply normal map detail to diffuse lighting only
	float nmDiffuse = saturate(dot(normal, normalize(float3(0.3, -0.5, 0.8))));
	lighting += nmDiffuse * surfDiff * 0.3;

	baseColor.rgb *= lerp(1.0, lighting, intensity);

	if(debugMode >= 1.0 && debugMode < 2.0)
		return float4(normal * 0.5 + 0.5, 1.0);
	if(debugMode >= 2.0)
		return float4(lighting.x, lighting.y, lighting.z, 1.0);

	return baseColor;
}
