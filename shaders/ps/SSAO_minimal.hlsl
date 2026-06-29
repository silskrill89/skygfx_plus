// Minimal SSAO for ps_2_0 - no loops, just 4 samples
uniform sampler2D depthTexture : register(s0);

uniform float4 ssaoParams : register(c0); // x=radius, y=power
uniform float4 screenSize : register(c1); // z=1/width, w=1/height
uniform float4 projInfo : register(c2); // z=proj43, w=proj33

struct PS_INPUT { float2 texCoord : TEXCOORD0; };

float4 main(PS_INPUT IN) : COLOR
{
    float d = tex2D(depthTexture, IN.texCoord).r;
    if (d >= 1.0) return float4(1,1,1,1);
    
    float viewZ = projInfo.z / (d - projInfo.w);
    float radius = ssaoParams.x;
    float invW = screenSize.z;
    float invH = screenSize.w;
    
    float occ = 0;
    
    // Sample right
    float2 uv = IN.texCoord + float2(radius * invW, 0);
    float sd = tex2D(depthTexture, uv).r;
    float svz = projInfo.z / (sd - projInfo.w);
    occ += step(0, viewZ - svz) * (1 - (viewZ - svz)/radius);
    
    // Sample left
    uv = IN.texCoord + float2(-radius * invW, 0);
    sd = tex2D(depthTexture, uv).r;
    svz = projInfo.z / (sd - projInfo.w);
    occ += step(0, viewZ - svz) * (1 - (viewZ - svz)/radius);
    
    // Sample down
    uv = IN.texCoord + float2(0, radius * invH);
    sd = tex2D(depthTexture, uv).r;
    svz = projInfo.z / (sd - projInfo.w);
    occ += step(0, viewZ - svz) * (1 - (viewZ - svz)/radius);
    
    // Sample up
    uv = IN.texCoord + float2(0, -radius * invH);
    sd = tex2D(depthTexture, uv).r;
    svz = projInfo.z / (sd - projInfo.w);
    occ += step(0, viewZ - svz) * (1 - (viewZ - svz)/radius);
    
    occ = 1 - occ * 0.25;
    occ = occ * occ;
    
    return float4(occ, occ, occ, 1);
}