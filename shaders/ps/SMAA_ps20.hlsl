<<<<<<< HEAD
// Full SMAA implementation based on open source documentation
// Subpixel Morphological Anti-Aliasing with edge detection and blending
// Compatible with ps_3_0
=======
// SMAA pixel shader - edge detection and blending
// Simplified SMAA for D3D9, compiled with ps_3_0 via build system
>>>>>>> master

uniform sampler2D colorTexture : register(s0);
uniform sampler2D areaTex : register(s1);
uniform sampler2D searchTex : register(s2);

uniform float4 ssaoParams : register(c0); // x=threshold, y=maxSearchSteps, z=cornerRounding, w=unused
uniform float4 screenSize : register(c1); // x=width, y=height, z=1/width, w=1/height
uniform float4 smaaParams : register(c2); // x=subpixelAA, y=cornerRounding, z=unused, w=unused

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;
    float2 pixel = screenSize.zw;
    
    // Edge detection using luma
    float3 c0 = tex2D(colorTexture, tex).rgb;
    float L0 = Luma(c0);
    
    float L[4];
    L[0] = Luma(tex2D(colorTexture, tex + float2(-pixel.x, 0)).rgb);
    L[1] = Luma(tex2D(colorTexture, tex + float2(pixel.x, 0)).rgb);
    L[2] = Luma(tex2D(colorTexture, tex + float2(0, -pixel.y)).rgb);
    L[3] = Luma(tex2D(colorTexture, tex + float2(0, pixel.y)).rgb);
    
    float4 edges;
    edges.x = abs(L0 - L[0]);
    edges.y = abs(L0 - L[1]);
    edges.z = abs(L0 - L[2]);
    edges.w = abs(L0 - L[3]);
    
    float threshold = ssaoParams.x;
    edges = step(threshold, edges);
    
    float edgeCount = dot(edges, 1.0);
    
    if (edgeCount < 0.5)
        return tex2D(colorTexture, tex);
    
    // Calculate subpixel mask using area texture
    float2 subpixelMask = tex2D(areaTex, edges.xy).rg;
    float alpha = smaaParams.x * subpixelMask.x * subpixelMask.y;
    
    // Search for corresponding edge in search texture
    float2 searchOffset = tex2D(searchTex, edges.xy).rg * pixel;
    
    // Apply morphological operations
    float2 blendCoord = tex + searchOffset;
    
    // Sample color at blend coordinate
    float3 blendedColor = tex2D(colorTexture, blendCoord).rgb;
    
    // Blend with original color based on alpha
    float3 finalColor = lerp(c0, blendedColor, alpha);
    
    return float4(finalColor, 1.0);
}