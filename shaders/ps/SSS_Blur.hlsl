// SSS_Blur.hlsl - Separable Subsurface Scattering Blur (ps_3_0)
// Two-pass separable blur based on Jorge Jimenez SeparableSSS.
// Quality level 1: 17 samples symmetric kernel.
// Direction set via c17.xy on CPU (horizontal or vertical).
//
// c17 = (pixelSizeX * dirX, pixelSizeY * dirY, sssWidth, strength)
// c18 = (nearClip, farClip, 0, 0)
// s0 = scene color
// s1 = depth buffer

sampler2D colorTex : register(s0);
sampler2D depthTex : register(s1);

float4 SSSParams   : register(c17);
float4 DepthParams : register(c18);

struct PS_INPUT {
    float2 texCoord : TEXCOORD0;
};

float LinearDepth(float2 uv) {
    float d = tex2D(depthTex, uv).r;
    float n = DepthParams.x;
    float f = DepthParams.y;
    return n * f / (f - d * (f - n));
}

float EdgeWeight(float sampleDepth, float centerDepth) {
    float diff = abs(sampleDepth - centerDepth);
    return exp(-diff * 100.0) * saturate(1.0 - diff * 20.0);
}

float4 main(PS_INPUT IN) : COLOR {
    float2 dir = SSSParams.xy;
    float sssWidth = SSSParams.z;
    float strength = SSSParams.w;

    float3 centerColor = tex2D(colorTex, IN.texCoord).rgb;
    float centerDepth = LinearDepth(IN.texCoord);

    // Kernel weights (symmetric, 17 samples from SeparableSSS quality 1)
    // [center, 0.0208, 0.0417, 0.0833, 0.1667, 0.3333, 0.6667, 1.3333, 2.0, 2.6667, 3.3333, 4.0, 4.6667, 5.3333, 6.0, 6.6667, 7.3333, 8.0]
    static const float kernW[17] = { 0.18, 0.052963, 0.052397, 0.050680, 0.047966, 0.044220, 0.040208, 0.035505, 0.030478, 0.025290, 0.020093, 0.015040, 0.010268, 0.005903, 0.003117, 0.001507, 0.000670 };
    static const float kernO[17] = { 0.0, 0.0208, 0.0417, 0.0833, 0.1667, 0.3333, 0.6667, 1.3333, 2.0, 2.6667, 3.3333, 4.0, 4.6667, 5.3333, 6.0, 6.6667, 7.3333 };

    float3 totalColor = centerColor * kernW[0];
    float totalWeight = kernW[0];

    // Unrolled positive direction
    float2 o1  = IN.texCoord + dir * kernO[1]  * sssWidth;
    float2 o2  = IN.texCoord + dir * kernO[2]  * sssWidth;
    float2 o3  = IN.texCoord + dir * kernO[3]  * sssWidth;
    float2 o4  = IN.texCoord + dir * kernO[4]  * sssWidth;
    float2 o5  = IN.texCoord + dir * kernO[5]  * sssWidth;
    float2 o6  = IN.texCoord + dir * kernO[6]  * sssWidth;
    float2 o7  = IN.texCoord + dir * kernO[7]  * sssWidth;
    float2 o8  = IN.texCoord + dir * kernO[8]  * sssWidth;
    float2 o9  = IN.texCoord + dir * kernO[9]  * sssWidth;
    float2 o10 = IN.texCoord + dir * kernO[10] * sssWidth;
    float2 o11 = IN.texCoord + dir * kernO[11] * sssWidth;
    float2 o12 = IN.texCoord + dir * kernO[12] * sssWidth;
    float2 o13 = IN.texCoord + dir * kernO[13] * sssWidth;
    float2 o14 = IN.texCoord + dir * kernO[14] * sssWidth;
    float2 o15 = IN.texCoord + dir * kernO[15] * sssWidth;
    float2 o16 = IN.texCoord + dir * kernO[16] * sssWidth;

    float3 c1  = tex2D(colorTex, o1).rgb;  float w1  = kernW[1]  * EdgeWeight(LinearDepth(o1), centerDepth);
    float3 c2  = tex2D(colorTex, o2).rgb;  float w2  = kernW[2]  * EdgeWeight(LinearDepth(o2), centerDepth);
    float3 c3  = tex2D(colorTex, o3).rgb;  float w3  = kernW[3]  * EdgeWeight(LinearDepth(o3), centerDepth);
    float3 c4  = tex2D(colorTex, o4).rgb;  float w4  = kernW[4]  * EdgeWeight(LinearDepth(o4), centerDepth);
    float3 c5  = tex2D(colorTex, o5).rgb;  float w5  = kernW[5]  * EdgeWeight(LinearDepth(o5), centerDepth);
    float3 c6  = tex2D(colorTex, o6).rgb;  float w6  = kernW[6]  * EdgeWeight(LinearDepth(o6), centerDepth);
    float3 c7  = tex2D(colorTex, o7).rgb;  float w7  = kernW[7]  * EdgeWeight(LinearDepth(o7), centerDepth);
    float3 c8  = tex2D(colorTex, o8).rgb;  float w8  = kernW[8]  * EdgeWeight(LinearDepth(o8), centerDepth);
    float3 c9  = tex2D(colorTex, o9).rgb;  float w9  = kernW[9]  * EdgeWeight(LinearDepth(o9), centerDepth);
    float3 c10 = tex2D(colorTex, o10).rgb; float w10 = kernW[10] * EdgeWeight(LinearDepth(o10), centerDepth);
    float3 c11 = tex2D(colorTex, o11).rgb; float w11 = kernW[11] * EdgeWeight(LinearDepth(o11), centerDepth);
    float3 c12 = tex2D(colorTex, o12).rgb; float w12 = kernW[12] * EdgeWeight(LinearDepth(o12), centerDepth);
    float3 c13 = tex2D(colorTex, o13).rgb; float w13 = kernW[13] * EdgeWeight(LinearDepth(o13), centerDepth);
    float3 c14 = tex2D(colorTex, o14).rgb; float w14 = kernW[14] * EdgeWeight(LinearDepth(o14), centerDepth);
    float3 c15 = tex2D(colorTex, o15).rgb; float w15 = kernW[15] * EdgeWeight(LinearDepth(o15), centerDepth);
    float3 c16 = tex2D(colorTex, o16).rgb; float w16 = kernW[16] * EdgeWeight(LinearDepth(o16), centerDepth);

    totalColor += c1*w1 + c2*w2 + c3*w3 + c4*w4 + c5*w5 + c6*w6 + c7*w7 + c8*w8;
    totalColor += c9*w9 + c10*w10 + c11*w11 + c12*w12 + c13*w13 + c14*w14 + c15*w15 + c16*w16;
    totalWeight += w1+w2+w3+w4+w5+w6+w7+w8+w9+w10+w11+w12+w13+w14+w15+w16;

    // Negative direction (mirror)
    float2 n1  = IN.texCoord - dir * kernO[1]  * sssWidth;
    float2 n2  = IN.texCoord - dir * kernO[2]  * sssWidth;
    float2 n3  = IN.texCoord - dir * kernO[3]  * sssWidth;
    float2 n4  = IN.texCoord - dir * kernO[4]  * sssWidth;
    float2 n5  = IN.texCoord - dir * kernO[5]  * sssWidth;
    float2 n6  = IN.texCoord - dir * kernO[6]  * sssWidth;
    float2 n7  = IN.texCoord - dir * kernO[7]  * sssWidth;
    float2 n8  = IN.texCoord - dir * kernO[8]  * sssWidth;
    float2 n9  = IN.texCoord - dir * kernO[9]  * sssWidth;
    float2 n10 = IN.texCoord - dir * kernO[10] * sssWidth;
    float2 n11 = IN.texCoord - dir * kernO[11] * sssWidth;
    float2 n12 = IN.texCoord - dir * kernO[12] * sssWidth;
    float2 n13 = IN.texCoord - dir * kernO[13] * sssWidth;
    float2 n14 = IN.texCoord - dir * kernO[14] * sssWidth;
    float2 n15 = IN.texCoord - dir * kernO[15] * sssWidth;
    float2 n16 = IN.texCoord - dir * kernO[16] * sssWidth;

    float3 d1  = tex2D(colorTex, n1).rgb;  float nw1  = kernW[1]  * EdgeWeight(LinearDepth(n1), centerDepth);
    float3 d2  = tex2D(colorTex, n2).rgb;  float nw2  = kernW[2]  * EdgeWeight(LinearDepth(n2), centerDepth);
    float3 d3  = tex2D(colorTex, n3).rgb;  float nw3  = kernW[3]  * EdgeWeight(LinearDepth(n3), centerDepth);
    float3 d4  = tex2D(colorTex, n4).rgb;  float nw4  = kernW[4]  * EdgeWeight(LinearDepth(n4), centerDepth);
    float3 d5  = tex2D(colorTex, n5).rgb;  float nw5  = kernW[5]  * EdgeWeight(LinearDepth(n5), centerDepth);
    float3 d6  = tex2D(colorTex, n6).rgb;  float nw6  = kernW[6]  * EdgeWeight(LinearDepth(n6), centerDepth);
    float3 d7  = tex2D(colorTex, n7).rgb;  float nw7  = kernW[7]  * EdgeWeight(LinearDepth(n7), centerDepth);
    float3 d8  = tex2D(colorTex, n8).rgb;  float nw8  = kernW[8]  * EdgeWeight(LinearDepth(n8), centerDepth);
    float3 d9  = tex2D(colorTex, n9).rgb;  float nw9  = kernW[9]  * EdgeWeight(LinearDepth(n9), centerDepth);
    float3 d10 = tex2D(colorTex, n10).rgb; float nw10 = kernW[10] * EdgeWeight(LinearDepth(n10), centerDepth);
    float3 d11 = tex2D(colorTex, n11).rgb; float nw11 = kernW[11] * EdgeWeight(LinearDepth(n11), centerDepth);
    float3 d12 = tex2D(colorTex, n12).rgb; float nw12 = kernW[12] * EdgeWeight(LinearDepth(n12), centerDepth);
    float3 d13 = tex2D(colorTex, n13).rgb; float nw13 = kernW[13] * EdgeWeight(LinearDepth(n13), centerDepth);
    float3 d14 = tex2D(colorTex, n14).rgb; float nw14 = kernW[14] * EdgeWeight(LinearDepth(n14), centerDepth);
    float3 d15 = tex2D(colorTex, n15).rgb; float nw15 = kernW[15] * EdgeWeight(LinearDepth(n15), centerDepth);
    float3 d16 = tex2D(colorTex, n16).rgb; float nw16 = kernW[16] * EdgeWeight(LinearDepth(n16), centerDepth);

    totalColor += d1*nw1 + d2*nw2 + d3*nw3 + d4*nw4 + d5*nw5 + d6*nw6 + d7*nw7 + d8*nw8;
    totalColor += d9*nw9 + d10*nw10 + d11*nw11 + d12*nw12 + d13*nw13 + d14*nw14 + d15*nw15 + d16*nw16;
    totalWeight += nw1+nw2+nw3+nw4+nw5+nw6+nw7+nw8+nw9+nw10+nw11+nw12+nw13+nw14+nw15+nw16;

    float3 blurred = totalColor / max(totalWeight, 0.001);

    // Skin profile: red scatters more, green medium, blue least
    float3 skinProfile = float3(1.0, 0.7, 0.5);
    float3 result = lerp(centerColor, blurred, strength * skinProfile);

    return float4(saturate(result), 1.0);
}
