uniform sampler2D tex : register(s0);
uniform float4 redGrade : register(c0);
uniform float4 greenGrade : register(c1);
uniform float4 blueGrade : register(c2);
uniform float4 tonemapParams : register(c5); // x=tonemap enable (0/1), y=exposure, z=0, w=0

struct PS_INPUT
{
	float3 texcoord0	: TEXCOORD0;
};

float3 ReinhardTonemap(float3 c)
{
	return c / (1.0 + c);
}

float4
main(PS_INPUT IN) : COLOR
{
	float4 c = tex2D(tex, IN.texcoord0.xy);
	c.a = 1.0f;

	// Reinhard tonemapping (before grading)
	if(tonemapParams.x > 0.5){
		float exposure = tonemapParams.y;
		c.rgb = ReinhardTonemap(c.rgb * exposure);
	}

	float4 o;
	o.r = dot(redGrade, c);
	o.g = dot(greenGrade, c);
	o.b = dot(blueGrade, c);
	o.a = 1.0f;
	return o;
}
