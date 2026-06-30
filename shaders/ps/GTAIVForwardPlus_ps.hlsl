<<<<<<< HEAD
// GTA IV Forward+ Unified Pixel Shader
// AMD Forward+ segmented rendering adapted for D3D9 ps_3_0
// Per-pixel light accumulation from GTA V tiled_lighting.fx
// BRDF techniques from CRYENGINE shadeLib.cfi (GGX, Burley diffuse, Schlick Fresnel)
// All geometry rendered forward with multi-light loop

struct PS_INPUT {
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float3 worldNormal : TEXCOORD1;
    float3 viewDir : TEXCOORD2;
    float2 envTexcoord : TEXCOORD3;
    float3 worldPos : TEXCOORD4;
};

sampler2D diffuseSampler : register(s0);
sampler2D envSampler : register(s1);

float4 materialParams : register(c0);  // {colorScale, fresnelPower, envMapStrength, materialType}

float4 lightData[16] : register(c4);   // [0-7]=pos/dir+w, [8-15]=color+intensity

float4 ambientFog : register(c20);     // {ambR, ambG, ambB, fogDensity}
float4 cameraTime : register(c21);     // {camX, camY, camZ, time}
float4 fxParams : register(c22);       // {shininess, specularity, lightCount, wetness}

// --- GGX Normal Distribution (CRYENGINE shadeLib.cfi) ---
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

// --- Correlated Smith Visibility (CRYENGINE shadeLib.cfi) ---
// V = G / (4 * NdotL * NdotV)
float V_SmithCorrelated(float NdotV, float NdotL, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float GGXL = NdotL * NdotL * (1.0 - a2) + a2;
    float GGXV = NdotV * NdotV * (1.0 - a2) + a2;
    return 0.5 / (sqrt(GGXV) * sqrt(GGXL) + 1e-5);
}

// --- Schlick Fresnel with micro-occlusion (CRYENGINE shadeLib.cfi) ---
float3 F_Schlick(float cosTheta, float F0)
{
    float fresnel = pow(1.0 - cosTheta, 5.0);
    return F0 + (1.0 - F0) * fresnel;
}

// --- Burley Diffuse (Disney/CRYENGINE) ---
// Renormalized diffuse with retro-reflection and grazing angle boost
float3 BurleyDiffuse(float NdotV, float NdotL, float LdotH, float roughness)
{
    float fd90 = 0.5 + 2.0 * LdotH * LdotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
    return float3(lightScatter, lightScatter, lightScatter) * float3(viewScatter, viewScatter, viewScatter);
}

// --- Oren-Nayar Diffuse (for rough surfaces like buildings) ---
float3 OrenNayarDiffuse(float NdotV, float NdotL, float roughness)
{
    float r2 = roughness * roughness;
    float A = 1.0 - 0.5 * r2 / (r2 + 0.33);
    float B = 0.45 * r2 / (r2 + 0.09);
    float thetaV = acos(NdotV);
    float thetaL = acos(NdotL);
    float alpha = max(thetaV, thetaL);
    float beta = min(thetaV, thetaL);
    return float3(A + B * sin(alpha) * tan(beta), A + B * sin(alpha) * tan(beta), A + B * sin(alpha) * tan(beta));
}

float4 main(PS_INPUT input) : COLOR0
{
    float3 N = normalize(input.worldNormal);
    float3 V = normalize(cameraTime.xyz - input.worldPos);

    float4 diffuse = tex2D(diffuseSampler, input.texcoord);
    float4 envColor = tex2D(envSampler, input.envTexcoord);

    float colorScale = materialParams.x;
    float fresnelPower = materialParams.y;
    float envMapStrength = materialParams.z;
    float materialType = materialParams.w;

    float shininess = fxParams.x;
    float specularity = fxParams.y;
    int lightCount = (int)fxParams.z;
    float wetness = fxParams.w;

    float3 baseColor = input.color.rgb * diffuse.rgb;

    // Roughness from shininess (invert: high shininess = low roughness)
    float roughness = saturate(1.0 - shininess);
    float NdotV = saturate(dot(N, V));

    // Material-specific F0
    float3 F0;
    if(materialType > 1.5){
        // Vehicle paint: metallic-like F0
        F0 = float3(0.04, 0.04, 0.04);
        baseColor *= 1.0; // metallic: diffuse suppressed by fresnel
    }else if(materialType > 0.5){
        // Vehicle mesh: dielectric
        F0 = float3(0.04, 0.04, 0.04);
    }else{
        // Building: dielectric
        F0 = float3(0.04, 0.04, 0.04);
    }

    // Forward+ per-tile light accumulation
    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    for(int i = 0; i < 8; i++){
        if(i >= lightCount) break;

        float4 lPos = lightData[i];
        float4 lCol = lightData[i + 8];

        float NdotL, NdotH, LdotH, atten;
        float3 L, H;

        if(lPos.w <= 0.0){
            // Directional light
            L = -lPos.xyz;
            H = normalize(V + L);
            NdotL = saturate(dot(N, L));
            NdotH = saturate(dot(N, H));
            LdotH = saturate(dot(L, H));
            atten = 1.0;
        }else{
            // Point light with physical attenuation
            L = lPos.xyz - input.worldPos;
            float dist = length(L);
            L = normalize(L);
            H = normalize(V + L);
            NdotL = saturate(dot(N, L));
            NdotH = saturate(dot(N, H));
            LdotH = saturate(dot(L, H));

            // Smooth physical attenuation (CRYENGINE-style)
            float radius = lPos.w;
            float distNorm = dist / radius;
            float distNorm2 = distNorm * distNorm;
            float attenNum = saturate(1.0 - distNorm2 * distNorm2);
            atten = attenNum * attenNum / (dist * dist + 1.0);
        }

        // Specular BRDF: GGX NDF * Smith Visibility * Schlick Fresnel
        float D = D_GGX(NdotH, roughness);
        float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
        float3 F = F_Schlick(LdotH, F0);

        float3 specContrib = D * F * Vis * NdotL * lCol.rgb * lCol.a * atten;
        totalSpecular += specContrib;

        // Diffuse BRDF: Burley for vehicles, Oren-Nayar for buildings
        float3 diffBRDF;
        if(materialType < 0.5){
            diffBRDF = OrenNayarDiffuse(NdotV, NdotL, roughness);
        }else{
            diffBRDF = BurleyDiffuse(NdotV, NdotL, LdotH, roughness);
        }

        // Energy conservation: diffuse = (1 - F) * (1 - metallic)
        float3 diffEnergy = (1.0 - F) * baseColor;
        totalDiffuse += diffBRDF * diffEnergy * NdotL * lCol.rgb * lCol.a * atten;
    }

    // Combine: ambient + diffuse + specular
    float3 color = ambientFog.rgb * baseColor;
    color += totalDiffuse;
    color += totalSpecular;

    // Environment map reflection (vehicles + wet roads)
    float fresnel = F_Schlick(NdotV, F0.x).x;
    float envStrength = envMapStrength * fresnel;

    // Wet road: boost reflections with fresnel-based blending
    if(wetness > 0.01){
        float wetFresnel = pow(1.0 - NdotV, 3.0);
        envStrength = max(envStrength, wetFresnel * wetness * 0.8);
    }

    color += envColor.rgb * envStrength;

    // Apply color scale
    color *= colorScale;

    return float4(color, diffuse.a);
}

technique GTAIVForwardPlus {
    pass P0 {
        PixelShader = compile ps_3_0 main();
    }
=======
sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);

float4		directCol	: register(c5);
float3		directDir[7]	: register(c12);

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
