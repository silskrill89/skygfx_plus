// Water_Underwater.hlsl — W7-D lane: underwater volume fog + subsurface
// scatter (ps_3_0). Ultra-tier steal from the CryEngine water recipe,
// docs/plans/water-refs.md:122:
//
//     underwater volume fog  exp2(-fogDensity.w · volDepth / max(V·Front, 1e-7))
//     + SSS (scale 2.0, EdotL² · height)
//
// CPU side: src/render/postfx_underwater.cpp (PostFX_UnderwaterTick), drawn as
// the s_ffQuad fullscreen quad over the camera raster. Register contract is
// IDENTICAL to HeightFog.hlsl (same s0/s1/c0..c5 conventions) so the pass is
// a drop-in sibling in the same chain region.
//
//   s0 = front buffer (padded 2048² raster: fetch at uv * fbParams.xy)
//   s1 = live INTZ scene depth, screen-sized (raw uv, window-space z)
//
//   c0 = fogParams  (density, sssScale, sssDepthRef, maxFog)
//   c1 = fogColour  absorption colour RGB, deep->shallow lerp (w unused)
//   c2 = projInfo   (recipVW.x, recipVW.y, -n·f/(f-n), f/(f-n))
//   c3 = fbParams   (frontBufferU, frontBufferV, 0, 0)
//   c4 = camPos     camera world position xyz, w = camera depth BELOW the water
//                   surface in metres (> 0 — the CPU only calls this pass when
//                   the camera is submerged)
//   c5 = camAxisZ   (right.z, up.z, at.z, 0) — camera axes' world-Z row. This
//                   is also the WORLD-UP vector expressed in view space, so
//                   dot(viewRay, c5.xyz) == V·Front (water surface normal).
//   c6 = sunDir     sun direction in VIEW space xyz (world dir mapped by the
//                   CPU), w = water surface level (world Z)
//   c7 = sssColour  timecycle directional light RGB — in-scatter tint (w unused)

sampler2D sceneTex : register(s0);
sampler2D depthTex : register(s1);

uniform float4 fogParams  : register(c0);
uniform float4 fogColour  : register(c1);
uniform float4 projInfo   : register(c2);
uniform float4 fbParams   : register(c3);
uniform float4 camPos     : register(c4);
uniform float4 camAxisZ   : register(c5);
uniform float4 sunDir     : register(c6);
uniform float4 sssColour  : register(c7);

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 tex = IN.texCoord;

    // Depth is screen-sized (raw UV); the front buffer is the padded 2048²
    // raster so its fetch is scaled by fbParams.xy (HeightFog convention).
    float depth  = tex2D(depthTex, tex).r;
    float3 scene = tex2D(sceneTex, tex * fbParams.xy).rgb;
    float3 result = scene;

    // Sky / far-plane pixels hold no geometry inside the water volume — pass
    // the scene through untouched (same early-out as HeightFog).
    if(depth < 0.999f)
    {
        // Linearise window-space z. Guards (SM3.0 turns INF/NaN into a black
        // fullscreen quad): degenerate depth == projInfo.w, and linear <= 0.
        float denom = depth - projInfo.w;
        if(abs(denom) > 1e-9f)
        {
            float linearDepth = projInfo.z / denom;
            if(linearDepth > 0.0f)
            {
                // View-space position (SSAO/HeightFog convention: +Z forward).
                float2 ndc = tex * 2.0f - 1.0f;
                ndc.y = -ndc.y;
                float3 viewPos = float3(ndc * projInfo.xy * linearDepth, linearDepth);

                // World Z of the sample (HeightFog's camera-Z-row trick — needs
                // no full basis matrix).
                float worldZ = camPos.z
                             + viewPos.x * camAxisZ.x
                             + viewPos.y * camAxisZ.y
                             + viewPos.z * camAxisZ.z;

                float camDepth   = camPos.w;                                  // > 0, CPU gated
                float waterLevel = sunDir.w;
                float pixDepth   = max(waterLevel - worldZ, 0.0f);            // 0 for above-surface samples

                // Vertical extent of the water volume traversed by this ray:
                //   sample below the surface  -> |camDepth - pixDepth|
                //   sample above the surface  -> camDepth (ray exits at z = waterLevel,
                //                                        which the max() above collapses to 0)
                float volDepth = abs(camDepth - pixDepth);

                // Normalised view ray (world) — guard the length.
                float invLen   = 1.0f / max(length(viewPos), 1e-7f);
                float3 viewRay = viewPos * invLen;

                // V·Front: the view ray's world-vertical component. c5 is the
                // world-up axis expressed in view space (see header), so the
                // dot product is basis-invariant. Guarded exactly like the
                // reference recipe: max(..., 1e-7).
                float vFront = max(abs(dot(viewRay, camAxisZ.xyz)), 1e-7f);

                // Slab path length (metres of water the ray passes through),
                // capped by the geometric view distance — for an all-submerged
                // ray the two are identical; for a grazing exit ray the cap
                // keeps the guarded divide from exploding.
                float path  = min(volDepth / vFront, linearDepth);

                // Recipe: exp2(-density · path) — Beer-Lambert in base 2.
                // saturate: density >= 0 and path >= 0 make this a no-op
                // (exp2(-x) in (0,1]); a negative/corrupt density upload can
                // never extrapolate the lerp below the scene colour.
                float fog = saturate(1.0f - exp2(-fogParams.x * path));
                fog = min(fog, fogParams.w);                                    // maxFog cap

                // SSS forward scatter (scale 2.0 · gain, EdotL² · height):
                // in-scatter grows when looking toward the sun and with the
                // sample's depth below the surface. All saturate-guarded.
                float edotl   = saturate(dot(viewRay, sunDir.xyz));
                float heightF = saturate(pixDepth / max(fogParams.z, 1e-7f));
                float sss     = fogParams.y * edotl * edotl * heightF;

                // Blend: absorption toward the water colour + light-tinted
                // in-scatter, both weighted by the same fog factor so the SSS
                // term can never outshine an unfogged pixel.
                float3 water = fogColour.rgb + sssColour.rgb * sss;
                result = lerp(scene, water, fog);
            }
        }
    }

    return float4(saturate(result), 1.0f);
}
