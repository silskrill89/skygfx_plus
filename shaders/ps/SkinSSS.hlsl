// SkinSSS.hlsl - PBR Skin Shader with Subsurface Scattering (ps_3_0)
// Full GGX/Smith/Schlick PBR with SSS wrap lighting and translucency.
// Expects TEXCOORD1=worldNormal, TEXCOORD2=worldPos (custom pipeline output).
//
// c0 = (roughness, reflectance, subsurface, specIntensity)
// c1 = (skinR, skinG, skinB, metalness)
// c2 = (bloodR, bloodG, bloodB, diffuseWrap)
// c3 = (camPosX, camPosY, camPosZ, 0)
// c4 = (lightDirX, lightDirY, lightDirZ, lightIntensity)
// c5 = (lightColR, lightColG, lightColB, ambientMul)
// c6 = (ambientR, ambientG, ambientB, 0)
// s0 = diffuse texture
// s1 = normal map

sampler2D diffuseTex : register(s0);
sampler2D normalTex  : register(s1);

float4 skinProps    : register(c0);
float4 skinColor    : register(c1);
float4 bloodColor   : register(c2);
float4 camPos       : register(c3);
float4 lightDir     : register(c4);
float4 lightCol     : register(c5);
float4 ambientCol   : register(c6);

struct PS_INPUT {
    float2 texCoord    : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float3 worldPos    : TEXCOORD2;
};

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * denom * denom + 0.0001);
}

float G_SchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float4 main(PS_INPUT IN) : COLOR {
    float roughness = skinProps.x;
    float reflectance = skinProps.y;
    float subsurface = skinProps.z;
    float specIntensity = skinProps.w;

    float4 diffuseSample = tex2D(diffuseTex, IN.texCoord);
    float3 albedo = diffuseSample.rgb * skinColor.rgb;
    float alpha = diffuseSample.a;

    float3 N = normalize(IN.worldNormal);
    float3 normalMap = tex2D(normalTex, IN.texCoord).rgb * 2.0 - 1.0;
    float3 dpdx = ddx(IN.worldPos);
    float3 dpdy = ddy(IN.worldPos);
    float3 T = normalize(dpdx * IN.texCoord.y - dpdy * IN.texCoord.x);
    float3 B = normalize(cross(N, T));
    N = normalize(N + normalMap.x * T + normalMap.y * B);

    float3 V = normalize(camPos.xyz - IN.worldPos);
    float3 L = normalize(lightDir.xyz);
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float3 F0 = float3(reflectance, reflectance, reflectance);

    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(HdotV, F0);

    float3 kD = (1.0 - F) * (1.0 - skinColor.a);
    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.001);

    float wrapFactor = bloodColor.w;
    float wrappedNdotL = saturate((NdotL + wrapFactor) / (1.0 + wrapFactor));
    float3 sssLight = albedo * subsurface * saturate(1.0 - NdotL) * float3(bloodColor.rgb);

    float3 H_back = normalize(-L + N * 0.2);
    float translucency = pow(saturate(dot(V, -L + N * 0.3)), 3.0) * subsurface;
    float3 transColor = float3(bloodColor.rgb) * translucency * NdotL;

    float3 diffuse = kD * albedo / 3.14159;
    float3 ambient = ambientCol.rgb * albedo * 0.3;

    float3 Lo = float3(0, 0, 0);
    Lo += (diffuse * wrappedNdotL + specular * specIntensity + sssLight + transColor) * lightCol.rgb * lightDir.w;
    Lo += ambient;

    Lo = Lo / (Lo + float3(1.0, 1.0, 1.0));

    return float4(saturate(Lo), alpha);
}
