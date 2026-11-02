// VehiclePBR_Modern.hlsl - Unified vehicle pixel shader (ps_3_0)
// ALL vehicle rendering paths in one file.
//
// Entry points:
//   main               - PBR vehicle (GGX/Smith/Schlick, IBL, cloud shadows, normal buf)
//   main_rubber        - PBR rubber/tire (parametric roughness, dirt/wear tint, subsurface)
//   main_ps2EnvSpecFx  - PS2 env + spec dual-layer
//   main_specCarFx     - Specular car FX (env + specular)
//   main_mobileVehicle - Mobile vehicle (env lerp + specular)
//   main_normMapVehicle - Normal-mapped vehicle (env blend)
//
// Compile with /E <entry> ps_3_0 (or ps_2_0 for legacy passes)
//
// Shared constants (c0-c13): surfProps, fxParams, eye, directCol, lightCol, directDir, lightDir

#include "../include/PBR_Common.hlsl"

sampler2D diffuseTex   : register(s0);
sampler2D envMapTex    : register(s1);
sampler2D maskTex      : register(s2);
sampler2D iblTex       : register(s3);
sampler2D normalBufTex : register(s4);
// NM lane: <diffuse>_n texture normal map (gloss in .a). Bound raw on s8 by
// the PBR cbs (vehiclePipe.cpp / buildingPipe.cpp) — s5/s6/s7 are owned by
// Forward+ (cluster tile + light atlases), s8 is the first genuinely free
// stage across every bind site in the project.
sampler2D nmTex        : register(s8);

float4 surfProps   : register(c0);
float4 fxParams    : register(c1);  // .z = envIntensity
float3 eyePos      : register(c2);
float4 iblParams   : register(c3);
float4 cloudShadow : register(c4);
float4 directCol   : register(c5);
float4 lightCol[6] : register(c6);
float3 directDir   : register(c12);
float3 lightDir[6] : register(c13);
float4 matCol      : register(c19);
float4 pbrParams   : register(c22); // {glossiness, specular, specTint, metallicness}
float4 paintNoise  : register(c23); // x=wheel, y=noiseScale, z=edgeBlend
float4 ambientColor : register(c24); // xyz=ambient rgb from timecycle, w=normalBuf enable (0/1)
float3 viewRight    : register(c25); // view matrix row 0 (world→view rotation)
float3 viewUp       : register(c26); // view matrix row 1
float3 viewFwd      : register(c27); // view matrix row 2
float4 skyParams    : register(c28); // xyz=skyTop color (0-1), w=skyReflect strength
float4 screenSize   : register(c29); // (screenW, screenH, 1/screenW, 1/screenH) — screen UV for normal-buffer sampling
// NM lane: {bound, intensity, glossFromAlpha, alphaOverride} — uploaded
// PER MESH by the PBR cbs next to the s8 bind. x/y/z are zeroed whenever no
// normal map is bound (D3D9 constants persist across draws). .z gates
// gloss-in-alpha on the CPU format check (DXT5/RGBA only — DXT1's 1-bit alpha
// is not gloss data). .w is the per-TEXTURE alpha override written on BOTH
// the normal-map and no-normal-map paths: 1 = force output alpha 1 (solid
// opaque textures, incl. DXT3 with unused/empty alpha blocks), 0 = use the
// texture alpha (cutout/faded/legacy textures).
float4 nmParams     : register(c48);

// Forward+ clustered lights
sampler2D clusterTex  : register(s5);  // tile index texture (RGBA8 POINT/CLAMP)
float4 clusterParams  : register(c45); // (tileSizePx, gridW, gridH, lightCount)
float4 layerCfg   : register(c46);  // x = vehPBRLayers layer bitmask
// PBR BUILDING env — uploaded per mesh by the PBR cb (buildingPipe.cpp).
// {x = enable, y = material env-map weight, z = sky reflect strength, w = 0}
// See the env block at the end of main_building().
float4 bldEnv     : register(c47);
// Part A3/B: {chromeClearcoat (0.6=legacy identity), envSpecularityMult (1=identity), z, w}
// — uploaded ONCE per CustomPipeRenderCB_Env invocation before the mesh loop
// (vehiclePipe.cpp). c44 was VS-only (campos) repo-wide; free in every PS.
//   .z = vehEnvStrengthMode (0/1)   .w = vehEnvGlint strength (0..1)
// REFLECTION REGISTER CONTRACT: c44.{x,y,z,w} and c20.{y,z,w} are the only
// vehicle-reflection config inputs of main(); the rest ride iblParams (c3),
// fxParams (c1), layerCfg (c46) and skyParams (c28).
float4 chromeParams : register(c44);
// Forward+ light atlases (see ClusterLighting below):
// s6 = pos/radius, s7 = color/type, both A32B32G32R32F 64x64 POINT.
// (The old <=32-light constant arrays at c48-c111 are gone.)
sampler2D lightPosTex : register(s6);
sampler2D lightColTex : register(s7);
// x = sky hemisphere ambient weight (config->pbrIblAmbientWeight)
// y = vehEnvFallback (0/1)   z = WetRoads 0..1 (CPU-gated by config->vehWetEnv)
// w = vehEnvMask (0/1)
// Uploaded once per Env cb; main_building only reads .x (buildingPipe.cpp:1074
// writes a 1-float upload, so .y/.z/.w are stale-but-unread on that path).
float4 iblAmbient : register(c20);

// Universal dynamic-sky hemisphere ambient tint, shared by the vehicle and building
// PBR paths so both sit on one ambient/IBL timeline. Samples the per-frame DynamicSky
// capture (s3) by ELEVATION only (zenith top, horizon bottom; azimuth ignored so the
// sun disc can't smear) and returns a MULTIPLICATIVE ratio skyCol/ambientColor centered
// at 1.0. Modulating the surface preserves shadows (near-black stays near-black) and
// can never lift blacks the way an additive deviation did. sky==ambient -> 1.0 -> no
// double-ambient. iblAmbient.x (config pbrIblAmbientWeight) is the tuning knob.
// PENDING in-game A/B (reverted after black-screen regression) — keep as-is.
float3 SkyHemisphereTint(float3 N){
    float2 uv = float2(0.5, saturate(N.y * 0.5 + 0.5));
    float3 skyCol = tex2D(iblTex, uv).rgb;
    if(dot(skyCol, skyCol) <= 1e-6) return float3(1.0, 1.0, 1.0); // guard: no-op
    // CHROMATIC ratio only: normalize sky and ambient to UNIT LUMINANCE before dividing
    // so a dark / near-zero timecycle ambient can never explode the magnitude (this was
    // the bug that turned the whole frame mega-blue when ambient was ~0). Magnitude is
    // intentionally dropped here - this is a hue tie between sky and ambient, centered
    // at 1.0, and the clamp bounds any residual excursion. Multiplicative, never lifts
    // blacks; sky==ambient -> 1.0.
    const float3 LUMA = float3(0.299, 0.587, 0.114);
    float skyL = max(dot(skyCol, LUMA), 1e-4);
    float ambL = max(dot(ambientColor.rgb, LUMA), 1e-4);
    float3 ratio = (skyCol / skyL) / max(ambientColor.rgb / ambL, 1e-4);
    ratio = clamp(ratio, 0.6, 1.5);   // PENDING in-game A/B — leave clamp weights as-is.
    return lerp(float3(1.0, 1.0, 1.0), ratio, iblAmbient.x * 0.15);
}

float whiteNoise(float2 p){
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

// PBR layer toggle decode: 1 when the given bit is set in c46.x, else 0.
// bit0=base,1=env,2=spec,3=rim,4=ibl,5=sky,6=clearcoat,7=normalbuf
float LF(float bit){
    float v = floor(layerCfg.x / exp2(bit));
    return frac(v * 0.5) * 2.0;
}


// Burley Diffuse (Disney/Crytek) — returns BRDF × NdotL (energy-conserved cosine weighting)
// Matches CryEngine shadeLib.cfi BurleyBRDF exactly.
float BurleyDiffuse(float NdotL, float NdotV, float VdotH, float roughness){
    NdotV = max(NdotV, 0.1);  // Prevent overly dark edges (CryEngine convention)
    float fd90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
    float scatterL = lerp(1.0, fd90, pow(1.0 - NdotL, 5.0));
    float scatterV = lerp(1.0, fd90, pow(1.0 - NdotV, 5.0));
    return scatterL * scatterV * lerp(1.0, 1.0/1.51, roughness) * NdotL;
}

// ============================================================================
// FORWARD+ CLUSTER LIGHTS — tile index texture + light atlases
// ============================================================================
// c45 = (tileSizePx, gridW, gridH, lightCount); lightCount > 0 == enabled
//       (forwardPlusEnable==0 makes the C++ zero c45 and unbind s5).
// s5  tile index texture, RGBA8 POINT/CLAMP, (gridW*2)x(gridH*2) texels.
//     tile (x,y) = clamp(floor(vpos.xy / tileSizePx), grid); its 16 light
//     indices live in the 2x2 texel block at (2x..2x+1, 2y..2y+1):
//     texel (2x + k%2, 2y + k/2) R,G,B,A = indices 4k..4k+3 (k = 0..3).
//     Value 0 = empty slot, else a 1-based index into the light atlases.
//     Slots are FRONT-PACKED by the CPU culler (nonzero run then zeros), so
//     the first 0 ends the tile and the loop breaks there.
// s6  A32B32G32R32F 64x64 POINT — texel i = (pos.xyz VIEW space, radius)
// s7  A32B32G32R32F 64x64 POINT — texel i = (r, g, b, type)
// Atlas texel i (i = idx-1, row-major): u = (i%64 + 0.5)/64, v = (i/64 + 0.5)/64.
//
// Fetch budget: exactly ONE dependent hop (s5 index -> s6/s7 texel); every
// other coordinate derives from VPOS and the loop counter. s5/s6/s7 are
// POINT-filtered single-mip textures, so all in-loop fetches use tex2Dlod
// (explicit LOD 0 — no gradient instructions, which fxc X3526 rejects inside
// a [loop] containing break/continue). Semantics identical to tex2D here:
// POINT + single mip means every mip level would resolve to the same texel.
// SM3.0: dynamic [loop] over 16 slots with early break (instruction budget
// stays flat regardless of slot count); all normalize/divide guarded at 1e-7.

// SM3.0-safe normalize: never divides by < 1e-7 (zero vector stays zero, no NaN).
float3 FPNorm(float3 v){
    return v * (1.0 / max(length(v), 1e-7));
}

// ============================================================================
// NM LANE — texture normal maps + gloss-in-alpha (s8 / c48)
// ============================================================================
// Detection is load-time (normalmap.cpp): when the game requests texture
// `foo`, the current txd is probed for `foo_n` (DK22Pac convention) then
// `foo_norm`; the pairing rides an RW texture plugin slot on the diffuse
// texture. The PBR cbs bind the map raw on s8 and upload c48 per mesh:
//   x = bound (1 = s8 holds this mesh's normal map)
//   y = normalMapIntensity (tangent XY perturbation scale)
//   z = glossFromAlpha (1 = the map's format carries a real alpha channel)
// Gloss decode (CryEngine DDNA convention): gloss = nmTex.a, roughness =
// 1 - gloss, blended 85% toward the map over the material's c22 glossiness.
// Tangent frame: RW geometry ships no tangents and the vertex decl must not
// change — the frame is derived per pixel from screen-space derivatives
// (cotangent-frame trick). ddx/ddy run at TOP-LEVEL flow only (fxc X3526
// rejects gradient instructions inside a [loop] containing break/continue —
// the same rule the Forward+ cluster fetch follows); the s8 tex2D sits in a
// plain flag-gated if (same pattern as the normalBufTex sample).

// Cotangent frame from screen-space derivatives. N must be normalized.
// All divides guarded at 1e-7 (NaN = black rule).
void nmCotangentFrame(float3 N, float3 p, float2 uv, out float3 T, out float3 B){
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    T = dp2perp * duv1.x + dp1perp * duv2.x;
    B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = rsqrt(max(dot(T, T) + dot(B, B), 1e-7));
    T *= invmax;
    B *= invmax;
}

// Tangent-space -> world perturbation. nmS is the raw s8 sample; N is the
// (already SS-blended) surface normal; V points at the eye. Black/degenerate
// samples leave N untouched (NaN = black rule). Z is reconstructed from the
// (intensity-scaled) XY so the perturbation stays unit-length.
float3 nmPerturbNormal(float3 N, float3 V, float4 nmS, float2 uv,
                       float3 T, float3 B, float intensity){
    if(dot(nmS.rgb, nmS.rgb) <= 1e-6)
        return N;                                // black/unbound sample — no perturbation
    float3 mapN = nmS.rgb * 2.0 - 1.0;
    mapN.xy *= max(intensity, 0.0);
    mapN.z = sqrt(saturate(1.0 - dot(mapN.xy, mapN.xy)));
    float3 wn = mapN.x * T + mapN.y * B + mapN.z * N;
    float wLen2 = dot(wn, wn);
    // Degenerate guard: a zero-length result (degenerate UV derivatives +
    // extreme texel) would NaN through normalize — keep the geometric normal.
    if(wLen2 <= 1e-6)
        return N;
    return wn * (1.0 / max(sqrt(wLen2), 1e-7));
}

// The single index->atlas hop: fetch pos/radius (s6) + color/type (s7) at
// the 0-based atlas texel for a 1-based light index. posView is VIEW space.
void ClusterFetchLight(int idx1, out float3 posView, out float radius, out float3 col){
    float fi = (float)(idx1 - 1);                       // 0-based row-major texel
    float row = floor(fi * (1.0 / 64.0));
    float2 auv = (float2(fi - row * 64.0, row) + 0.5) * (1.0 / 64.0);
    float4 pr = tex2Dlod(lightPosTex, float4(auv, 0.0, 0.0)); // (pos.xyz view, radius)
    float4 ct = tex2Dlod(lightColTex, float4(auv, 0.0, 0.0)); // (rgb, type — type unused: point lobe)
    posView = pr.xyz;
    radius  = pr.w;
    col     = ct.rgb;
}

// Per-light GGX + clearcoat + Burley accumulation — the exact BRDF the old
// constant-array loops ran (only the light fetch changed). Flavors:
//   main           : fresnelLH=1 (F_SchlickLH), ccStr=chrome clearcoat (0.05 lobe)
//   main_building  : fresnelLH=0 (F_Schlick),    ccStr=0 (no clearcoat)
//   main_rubber    : specW=0 (diffuse only),     kD=0.5
// specW scales the GGX lobe only; ccStr scales the clearcoat lobe (old main
// multiplied it by ccStr alone — preserved).
void ClusterShadeOne(float3 ToLight, float radius, float3 lcol,
                     float3 N, float3 V, float3 F0, float roughness,
                     float3 baseColor, float3 kD, float ccStr, float specW,
                     float fresnelLH,
                     inout float3 specAccum, inout float3 diffAccum){
    float dist = length(ToLight);
    if(dist >= radius || dist <= 1e-6) return;
    float3 Lc = ToLight * (1.0 / max(dist, 1e-7));
    float atten = saturate(1.0 - dist / max(radius, 1e-7));
    atten *= atten;                                     // quadratic falloff
    float NdotLc = max(dot(N, Lc), 0.0);
    if(NdotLc <= 0.0) return;
    float NdotV = max(dot(N, V), 0.0);
    float3 Hc = FPNorm(V + Lc);
    float NdotHc = max(dot(N, Hc), 0.0);
    float LdotHc = max(dot(Lc, Hc), 0.0);
    if(specW > 0.0){
        float Dc = D_GGX(NdotHc, roughness);
        float Visc = V_SmithCorrelated(NdotV, NdotLc, roughness);
        float3 Fc = (fresnelLH > 0.5) ? F_SchlickLH(LdotHc, F0) : F_Schlick(LdotHc, F0);
        specAccum += Dc * Fc * Visc * NdotLc * lcol * atten * specW;
        if(ccStr > 0.0){
            // Clearcoat per-cluster-light (old main: sigma 0.05, white F0, * ccStr)
            float Dc_cc = D_GGX(NdotHc, 0.05);
            float Visc_cc = V_SmithCorrelated(NdotV, NdotLc, 0.05);
            float3 Fc_cc = F_SchlickLH(LdotHc, float3(0.04, 0.04, 0.04));
            specAccum += Dc_cc * Fc_cc * Visc_cc * NdotLc * lcol * atten * ccStr;
        }
    }
    // Diffuse from cluster lights (Burley) — baseColor * diffc * col * atten * kD / PI
    float VdotHc = max(dot(V, Hc), 0.0);
    float diffc = BurleyDiffuse(NdotLc, NdotV, VdotHc, roughness);
    diffAccum += baseColor * diffc * lcol * atten * kD * (1.0 / 3.14159);
}

// Shared cluster entry: world-space surface (used by main, main_building,
// main_rubber, main_normMapVehicle). s6 light positions are VIEW space; lift
// them back to world via eyePos (c2, camera world pos) + the world->view
// rotation rows (c25-c27) before the world-space BRDF.
void ClusterLighting(float2 px, float3 worldPos, float3 N, float3 V,
                     float3 F0, float roughness, float3 baseColor, float3 kD,
                     float ccStr, float specW, float fresnelLH,
                     inout float3 specAccum, inout float3 diffAccum){
    if(clusterParams.w <= 0.0) return;                  // forwardPlusEnable gate
    // View-row validity gate: c25-c27 are unit rows of the view rotation, so
    // the squared-length sum is ~3. Zero/garbage rows (register never uploaded
    // on a path) would teleport every light to the eye — skip instead.
    float rowsLen = dot(viewRight, viewRight) + dot(viewUp, viewUp) + dot(viewFwd, viewFwd);
    if(abs(rowsLen - 3.0) > 0.5) return;

    // Screen-space tile (VPOS), clamped to the grid — same binning the CPU
    // culler fills. The 16 indices live in the 2x2 texel block at 2*tile.
    float2 tileF = floor(px / max(clusterParams.x, 1e-7));
    tileF = clamp(tileF, float2(0.0, 0.0), max(clusterParams.yz - 1.0, float2(0.0, 0.0)));
    float2 blockBase = tileF * 2.0;
    float2 texelStep = 0.5 / max(clusterParams.yz, float2(1.0, 1.0));  // 1/(2*grid)

    [loop] for(int s = 0; s < 16; s++){
        float sf = (float)s;
        float k4f = floor(sf * 0.25);                   // texel in the 2x2 block (0..3)  = s >> 2
        int c  = (int)(sf - k4f * 4.0 + 0.5);           // channel in that texel (0..3)   = s & 3
        float kx = k4f - 2.0 * floor(k4f * 0.5);        // block column                   = k4 & 1
        float ky = floor(k4f * 0.5);                    // block row                      = k4 >> 1
        float2 tuv = (blockBase + float2(kx, ky) + 0.5) * texelStep;
        float4 t = tex2Dlod(clusterTex, float4(tuv, 0.0, 0.0));
        float4 m = float4((c == 0) ? 1.0 : 0.0, (c == 1) ? 1.0 : 0.0,
                          (c == 2) ? 1.0 : 0.0, (c == 3) ? 1.0 : 0.0);
        int idx = (int)(dot(t, m) * 255.0 + 0.5);       // 0 = empty
        if(idx == 0) break;                             // front-packed tail
        if(idx > 4096) continue;
        float3 lpView, lcol;
        float radius;
        ClusterFetchLight(idx, lpView, radius, lcol);
        float3 lpWorld = eyePos + viewRight * lpView.x + viewUp * lpView.y + viewFwd * lpView.z;
        ClusterShadeOne(lpWorld - worldPos, radius, lcol, N, V, F0, roughness,
                        baseColor, kD, ccStr, specW, fresnelLH, specAccum, diffAccum);
    }
}

// ADAPTED SIGNATURE: cluster variant for entries whose VS provides NO surface
// world position/normal (main_mobileVehicle, main_specCarFx, main_ps2EnvSpecFx
// — vehiclePipeVS.hlsl gives them env UVs / a reflection vector only, and the
// legacy cbs upload no eye/view-row registers). Runs entirely in the light
// atlas' native VIEW space: the pixel's view ray (tanHalfFov ~ 0.7, SA default
// ~70deg; screen size estimated from the c45 tile grid) is the surface proxy.
// The tile lookup keeps the light list screen-local, so this is a bounded
// approximation of the same GGX + clearcoat + Burley lobes (shared
// ClusterShadeOne). Needs only c45 + s5/s6/s7 + VPOS. Fixed dielectric
// material (F0 0.04) at the call sites since these entries expose no material
// registers.
//
// NO DEPTH SOURCE — why the surface is a ray proxy, not a reconstructed
// position (orc-1 code review, finding #1). None of the three calling cbs
// binds a depth/normal texture: CustomPipeRenderCB_PS2 / _Specular / _mobile
// set only s0-s2 (material/env/spec) plus the Forward+ s5/s6/s7 block — s4
// (normalBufTex, whose alpha carries linear depth/far, NormalBuffer.hlsl:90)
// and c29 (screenSize) are uploaded ONLY by the Env/Building cbs
// (vehiclePipe.cpp:2744/:2749, buildingPipe.cpp:1735), whose entries run the
// world-space ClusterLighting instead. The pixel's true view-space position
// is therefore NOT derivable here without new binds (staged — see the
// checkpoint report), so the proxy is:
//   proxy point P  = the point of the pixel's view ray closest to each light;
//   ToLight        = lpView - P = the exact perpendicular light-to-ray
//                    offset — the MINIMUM distance from the light to ANY
//                    point of the ray, hence a lower bound on the true
//                    light-to-surface distance. Attenuation from it is an
//                    upper bound on true brightness (no invented depth error,
//                    unlike a fixed/eye-depth proxy would be);
//   N              = normalize(lpView), the camera->light direction (per
//                    light). The previous camera-facing N = -dir was
//                    self-defeating with this ToLight: ToLight ⊥ dir by
//                    construction and N ∥ dir, so dot(N,ToLight) ≡ 0 and
//                    ClusterShadeOne's `if(NdotLc <= 0.0) return;` fired for
//                    EVERY light — the feature was dead while still paying
//                    the 16-tap loop. Now, with θ = angle(lpView, dir):
//                      dot(N,ToLight) = |lpView|·sin²θ ≥ 0
//                      NdotLc         = sinθ
//                    which is > 0 for every light with ToLight ≠ 0; the only
//                    zero case (lpView ∥ dir) implies ToLight = 0 ⇒ dist = 0
//                    ⇒ the dist ≤ 1e-6 guard bails first anyway. FPNorm
//                    guards |lpView| < 1e-7 (light at the eye ⇒ N = 0 ⇒
//                    NdotLc = 0 ⇒ no light, no NaN).
//   KNOWN APPROXIMATION: directionally dim for lights near the view axis
//   (sinθ → 0) and brightest ~90° off-axis; no depth means no better surface
//   orientation is derivable without new binds. V stays the camera-facing
//   ray direction (view vector from a proxy point on the ray toward the eye).
void ClusterLightingScreen(float2 px, float3 F0, float roughness,
                           float3 baseColor, float3 kD, float ccStr, float specW,
                           inout float3 specAccum, inout float3 diffAccum){
    if(clusterParams.w <= 0.0) return;                  // forwardPlusEnable gate
    float2 scrSize = max(clusterParams.x * clusterParams.yz, float2(1e-7, 1e-7));
    float2 uv = px / scrSize;                           // 0..1 within the tile grid
    float3 dir = float3((uv.x * 2.0 - 1.0) * 0.7, (1.0 - uv.y * 2.0) * 0.7, 1.0);
    // View vector: the proxy point sits ON the ray, so the direction back
    // toward the eye is the camera-facing ray direction (loop-invariant).
    // N is per-light now (see header) and is fetched inside the loop.
    float3 V = FPNorm(-dir);

    float2 tileF = floor(px / max(clusterParams.x, 1e-7));
    tileF = clamp(tileF, float2(0.0, 0.0), max(clusterParams.yz - 1.0, float2(0.0, 0.0)));
    float2 blockBase = tileF * 2.0;
    float2 texelStep = 0.5 / max(clusterParams.yz, float2(1.0, 1.0));

    [loop] for(int s = 0; s < 16; s++){
        float sf = (float)s;
        float k4f = floor(sf * 0.25);
        int c  = (int)(sf - k4f * 4.0 + 0.5);
        float kx = k4f - 2.0 * floor(k4f * 0.5);
        float ky = floor(k4f * 0.5);
        float2 tuv = (blockBase + float2(kx, ky) + 0.5) * texelStep;
        float4 t = tex2Dlod(clusterTex, float4(tuv, 0.0, 0.0));
        float4 m = float4((c == 0) ? 1.0 : 0.0, (c == 1) ? 1.0 : 0.0,
                          (c == 2) ? 1.0 : 0.0, (c == 3) ? 1.0 : 0.0);
        int idx = (int)(dot(t, m) * 255.0 + 0.5);
        if(idx == 0) break;
        if(idx > 4096) continue;
        float3 lpView, lcol;
        float radius;
        ClusterFetchLight(idx, lpView, radius, lcol);
        // Closest point of the pixel's view ray to the light -> proxy surface;
        // ToLight is the exact perpendicular light-to-ray distance (lower
        // bound on the true light-to-surface distance — see header).
        float t0 = dot(lpView, dir) / max(dot(dir, dir), 1e-7);
        float3 ToLight = lpView - dir * t0;
        // Per-light shading normal = camera->light direction. Replaces the old
        // N = FPNorm(-dir): that was ∥ dir while ToLight ⊥ dir, so
        // dot(N,ToLight) ≡ 0 and every light bailed at NdotLc <= 0 (orc-1
        // finding #1). Now dot(N,ToLight) = |lpView|·sin²θ ≥ 0 and
        // NdotLc = sinθ > 0 whenever ToLight ≠ 0; FPNorm guards |lpView|
        // < 1e-7 (light at the eye ⇒ N = 0 ⇒ no light, no NaN).
        float3 N = FPNorm(lpView);
        ClusterShadeOne(ToLight, radius, lcol, N, V, F0, roughness,
                        baseColor, kD, ccStr, specW, 1.0, specAccum, diffAccum);
    }
}

struct PS_INPUT{
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float3 ViewDir     : TEXCOORD3;
    float3 SunDir      : TEXCOORD4;
    float4 color       : COLOR0;
    float4 envColor    : COLOR1;
};

float4 main(PS_INPUT IN, float4 vpos : VPOS) : COLOR
{
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = length(IN.ViewDir) > 1e-6 ? IN.ViewDir / length(IN.ViewDir) : float3(0, 0, 1);
    // Use PS c12 (directDir) — world-space sun direction.
    // IN.SunDir from VS is LOCAL-space (wrong for rotated vehicles).
    float3 L = length(directDir) > 1e-6 ? -normalize(directDir) : float3(0, 0, -1);

    // ---- Base color: game lighting from VS (matches building pipe) ----
    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    // IN.color.rgb = VS-baked game lighting (ambient + 7 directional × matCol).
    // Same approach as main_building: vertex color IS the diffuse lighting.
    // Our PBR specular/env/reflection layers sit on top of this.
    float3 baseColor = diff.rgb * IN.color.rgb;

    // Blend with screen-space normal (if available)
    // ambientColor.w > 0 means normal buffer is bound and valid
    if(LF(7) > 0.5 && ambientColor.w > 0.5){
        // Sample with SCREEN UV (derived from VPOS), not diffuse UV — the normal
        // buffer is a fullscreen half-res texture; diffuse UVs smear wrong texels
        // across the paint.
        float2 screenUV = vpos.xy * screenSize.zw;
        float4 ssSample = tex2D(normalBufTex, screenUV);
        float3 ssNormalView = ssSample.rgb * 2.0 - 1.0;
        float ssLen = length(ssNormalView);
        // Validity gate — hardened, backported verbatim from main_building
        // (:560+), which needed exactly this fix. The old test here was
        //   if(dot(ssNormal, ssNormal) > 0.25)
        // which only rejects an all-zero texel: a never-cleared / stale /
        // resolution-changed D3DPOOL_DEFAULT RT decodes to an ARBITRARY length
        // (and the (0.5,0.5,1.0) sky marker is unit-length but is sky, not
        // geometry) — those tilted N and drove GGX blowout + glints on the
        // paint. NormalBuffer writes normalized normals, so valid texels decode
        // to unit length; .a carries depth/far, so reject the sky marker there.
        // Lerp the weight OUT instead of a binary accept.
        float ssValid = smoothstep(0.55, 0.85, ssLen)
                      * (1.0 - smoothstep(1.15, 1.45, ssLen))
                      * (1.0 - step(0.99, ssSample.a));
        // NormalBuffer.hlsl reconstructs from view-space depth and writes a
        // VIEW-space normal. c25-c27 are the world->view rotation ROWS, so
        // n_world = n_view.x*row0 + n_view.y*row1 + n_view.z*row2 (= R^T*n).
        // Without this transform a view-space vector was lerped into world N,
        // making the shading rotate with the camera (back faces on front).
        // PENDING in-game A/B (reverted after black-screen regression) — keep
        // this view->world normal-buffer transform as-is.
        float3 ssNormalWorld = ssNormalView.x * viewRight
                             + ssNormalView.y * viewUp
                             + ssNormalView.z * viewFwd;
        float ssWorldLen = length(ssNormalWorld);
        // Rows must be a sane orthonormal view rotation (same gate
        // ClusterLighting uses). s4 is only bound by the Env/Building cbs,
        // which upload c25-c27; fail closed on zero/garbage rows or NaN.
        float rowsLen = dot(viewRight, viewRight) + dot(viewUp, viewUp) + dot(viewFwd, viewFwd);
        ssValid *= (abs(rowsLen - 3.0) < 0.5) ? 1.0 : 0.0;
        // Anti-flip: the world-space sample must agree with the rendered face's
        // geometric normal and with its world-Z sign (GTA world is Z-up). The
        // Z test is skipped for near-vertical walls (|N.z| small) so wall
        // detail still blends; horizontal surfaces fail closed on a flipped
        // sample. All comparisons fail closed on NaN; ssWorldLen guards the
        // divide.
        ssValid *= (ssWorldLen > 1e-6 && dot(ssNormalWorld, N) > 0.0) ? 1.0 : 0.0;
        ssValid *= (abs(N.z) < 0.1 || ssNormalWorld.z * N.z >= 0.0) ? 1.0 : 0.0;
        if(ssValid > 0.001)
            N = normalize(lerp(N, ssNormalWorld / max(ssWorldLen, 1e-7), 0.3 * ssValid));
    }

    // ---- NM lane: texture normal map perturbation (s8/c48) ----
    // Cotangent frame from screen-space derivatives — computed at TOP-LEVEL
    // flow (ddx/ddy are illegal inside [loop]+break in SM3.0, X3526). The s8
    // sample sits in a plain flag-gated if (defined-sample rule: never read a
    // potentially unbound stage unconditionally).
    float3 nmT, nmB;
    nmCotangentFrame(N, -V, IN.texcoord0, nmT, nmB);
    float4 nmS = float4(0.5, 0.5, 1.0, 1.0);
    if(nmParams.x > 0.5){
        nmS = tex2D(nmTex, IN.texcoord0);
        N = nmPerturbNormal(N, V, nmS, IN.texcoord0, nmT, nmB, nmParams.y);
    }

    // ---- PBR material properties (KHR_materials_pbrSpecularGlossiness workflow) ----
    // Everything is dielectric. No metallicness.
    // pbrParams.x = glossiness (1=smooth/shiny, 0=rough) — PRIMARY variable
    // pbrParams.y = specular reflectance (F0 at normal incidence, ~0.04 for dielectrics)
    // pbrParams.z = specular color tint (0=white, 1=tinted by paint)
    // pbrParams.w = metallicness (0=dielectric paint, 1=chrome/metal)
    // glTF KHR_materials_pbrSpecularGlossiness:
    //   α = (1 - glossiness)^2  — our D_GGX/V_SmithCorrelated square internally,
    //   so we pass (1 - glossiness) as σ, giving α² correctly.
    //   c_diff = diffuse * (1 - max(specular.r, specular.g, specular.b))
    float glossiness  = pbrParams.x;
    // NM lane gloss-in-alpha (CryEngine DDNA): gloss = nmTex.a → roughness =
    // 1 - gloss. Blended 85% toward the map over the material's c22 glossiness;
    // c48.z gates on the CPU format check (real alpha channel only). Applied
    // BEFORE the wheel-roughness override and the wet-road block below so both
    // still layer on top of the map's gloss.
    if(nmParams.x > 0.5 && nmParams.z > 0.5)
        glossiness = lerp(glossiness, nmS.a, 0.85);
    float specularF0  = pbrParams.y;
    float specTint    = pbrParams.z;
    // Metalness drives the env-reflection Fresnel F0 and the neutral reflection
    // tint. 0 = paint (dielectric, F0~0.04, paint-tinted reflections); >0 = chrome
    // / wheel metal (high F0, white mirror reflections). Set in vehiclePipe.cpp
    // from the BRDF surface type so metallic trim reads as metal, not tinted paint.
    float metallicness   = saturate(pbrParams.w);

    // Wheel roughness override: reduce glossiness for wheels (tires are rough)
    float isWheel = paintNoise.x;
    float wheelNoise = whiteNoise(IN.texcoord0 * 47.0);
    glossiness = lerp(glossiness, 0.85, isWheel * wheelNoise * 0.5);

    // ---- WET ROADS (ported from main_building :550-565) ----
    // [missing term] main_building damps the base and sharpens the surface in
    // the rain; main() had NO wet response at all, so cars stayed dry-looking
    // beside wet buildings. CPU folds config->vehWetEnv into PS c20.z, so this
    // block is a no-op (c20.z == 0) whenever the toggle is off.
    float wetRoadsVeh = saturate(iblAmbient.z);
    if(wetRoadsVeh > 0.01){
        baseColor *= lerp(1.0, 0.85, wetRoadsVeh);   // water absorption
        glossiness  = lerp(glossiness, 0.9, wetRoadsVeh);
    }

    // (paintNoise.y/z = noiseScale/edgeBlend, reserved for future reflection breakup)

    // ---- Paint tint from carcols ----
    float3 paintTint = matCol.rgb;

    // ---- Core PBR vectors ----
    float NdotV = max(dot(N, V), 0.0);

    // F0: dielectric reflectance, optionally tinted by paint color
    float3 F0 = lerp(float3(specularF0, specularF0, specularF0), paintTint * specularF0, specTint);

    // Energy conservation for specular ONLY — diffuseScale is NOT applied to
    // baseColor because IN.color.rgb already has game lighting baked in.
    // Applying it to vertex-lit color would darken/wash out the paint.
    float metallicFactor = saturate((specularF0 - 0.5) * 2.0);

    // Fresnel for specular energy split (kD not used for the VS-baked directional
    // diffuse — that is already energy-managed by the game). It IS used for the
    // new cluster-light diffuse term further down.
    // BUG FIX (interior blackout): kDv is the DIFFUSE energy budget and must be
    // VIEW-INDEPENDENT. Deriving it from F_Schlick(NdotV, F0) collapsed it to 0
    // at grazing angles (normal-mapped NdotV -> 0), zeroing every Forward+
    // point-light diffuse term indoors as the camera angle changed (black).
    // EnergyConservation(F0) = saturate(1 - luminance(F0)) is view-independent.
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kDv = EnergyConservation(F0);

    // ---- Daylight sky-lift (vehicle diffuse only) ----
    // The timecycle ambient reaching car materials is near-zero (live log:
    // raw=(0,0,0), final=0.034), so non-sunlit faces were lit almost entirely by
    // the baked prelight. Sample the dynamic-sky capture by elevation and apply a
    // small MULTIPLICATIVE lift: it scales the existing paint colour, so it can
    // never grey/wash the paint (the additive version pushed saturated colours
    // into the tonemap's desaturating top end) and never lifts true blacks.
    // Self-extinguishes at night, stronger on sky-facing surfaces.
    // PENDING in-game A/B (reverted after black-screen regression) — leave skyFill as-is.
    float3 skyFill = tex2D(iblTex, float2(0.5, saturate(N.y * 0.5 + 0.5))).rgb;
    float skyFillLuma = dot(skyFill, float3(0.299, 0.587, 0.114));
    baseColor *= 1.0 + skyFillLuma * 0.22 * LF(4) * saturate(N.y * 0.5 + 0.5);

    // ---- Diffuse: VS vertex color already has full game lighting ----
    // Ambient + 7 directional lights baked into IN.color.rgb by the VS.
    // PBR specular and env reflection layer ON TOP of game-lit base.
    // Matches building pipe approach (main_building line 366).
    float3 layer1 = baseColor * LF(0);

    // Sun direction for specular (from PS c12, same as buildings)
    float3 sunContrib = directCol.rgb;
    float NdotL_sun = max(dot(N, L), 0.0);

    // ---- Environment Reflection: clearcoat Fresnel drives visibility ----
    // Car paint has a clearcoat — env reflections visible at all angles, stronger at grazing
    float3 R_world = reflect(-V, N);
    // Env reflection strength knob. iblParams.x was the (dead) specular slot;
    // it is now repurposed so reflection tuning needs no extra register.
    float envIntensity = max(iblParams.x, 0.0);
    // Strength placement [energy-consistent composite].
    // Legacy scaled the env CONTENT by envIntensity while kr still REMOVED kr
    // of the body, so turning the strength slider down darkened the paint by
    // kr instead of weakening the reflection (lerp(body, dim*env, kr) =
    // body*(1-kr) + dim*env*kr). Energy mode keeps the content at full scale
    // and puts the knob on the Fresnel COVERAGE (kr *= intensity): bit-
    // identical at intensity 1.0, body fully intact at low intensity.
    // chromeParams.z carries the mode (0 = legacy). See config->vehEnvStrengthMode.
    float envContent  = 1.2;
    float envCoverage = 1.0;
    if(chromeParams.z > 0.5)
        envCoverage = saturate(envIntensity);
    else
        envContent = 1.2 * envIntensity;
    // Wet roads widen the reflection band (a glassy film mirrors more of the
    // hemisphere at every angle) — kr is saturate()'d at the composite below.
    envCoverage *= lerp(1.0, 2.0, wetRoadsVeh);
    // The env map is a PERSPECTIVE render from the camera's viewpoint (60m clip),
    // so sample it by projecting the world-space reflection vector with the main
    // camera's view window (c3.zw = tanHalfFovX/Y). Upward faces now reflect the
    // sky (top half of the env render) and side faces reflect the world, instead
    // of the geometrically-arbitrary patches the sphere-map formula produced.
    float3 R_view = float3(dot(R_world, viewRight), dot(R_world, viewUp), dot(R_world, viewFwd));
    float rz = max(R_view.z, 0.05);
    float2 envReflUV = saturate(float2(
        0.5 + 0.5 * clamp(R_view.x / (rz * max(iblParams.z, 1e-7)), -1.2, 1.2),
        0.5 - 0.5 * clamp(R_view.y / (rz * max(iblParams.w, 1e-7)), -1.2, 1.2)));
    float3 envRefl = tex2D(envMapTex, envReflUV).rgb;
    float3 iblSample = tex2D(iblTex, envReflUV).rgb;
    // Content of the reflection TEXTURE, decided BEFORE the mask so a
    // masked-to-black region reads as "the mask says no reflection here" and
    // not as "reflectionTex failed to render".
    float rawEnvHasContent = (dot(envRefl, envRefl) > 1e-6) ? 1.0 : 0.0;

    // [missing term: reflection MASK — PS2 / Specular / Neo parity]
    // main_ps2EnvSpecFx and main_specCarFx both modulate by maskTex (s2), and
    // CarPipe::RenderEnvTex bakes CarReflectionMask into reflectionTex with a
    // 0..1 screen quad (SRCBLEND=ZERO/DESTBLEND=SRCCOLOR) — but the
    // RenderSphereReflections path that CAR_MOBILE/CAR_ENV/CAR_MODERN use never
    // bakes it, and this cb bound s2 without ever reading it. The mask is
    // authored in reflection-RT UV space, which is exactly envReflUV, so
    // sampling it at the same UV reproduces the baked result (Mobile lerps
    // against that baked RT, so this is bit-comparable to the reference path).
    // CPU sends 0 when config->vehEnvMask is off OR reflectionMask failed to load.
    if(iblAmbient.w > 0.5 && rawEnvHasContent > 0.5)
        envRefl *= tex2D(maskTex, envReflUV).rgb;

    // [missing term: env-source VALIDITY FALLBACK]
    // The old lerp(body, envTerm, kr) against an EMPTY reflectionTex replaced
    // up to kr of the paint with black — the single biggest reason Modern read
    // darker than Mobile. Only a texture with no content runs the chain
    // env -> ibl -> sky (a masked-to-black region deliberately does NOT: that
    // fade must stay a fade). envUsable is then measured on the FINAL source,
    // so kr is forced to 0 wherever there is nothing to reflect — no lerp
    // toward an empty environment, no double-darkening. Better than Mobile
    // here, which lerps body*(1-k) against the masked black. Toggle
    // config->vehEnvFallback: off skips the chain AND pins envUsable = 1,
    // i.e. the exact legacy behaviour.
    float3 envSrc = envRefl;
    if(iblAmbient.y > 0.5 && rawEnvHasContent < 0.5){
        envSrc = iblSample;
        if(dot(envSrc, envSrc) <= 1e-6) envSrc = skyParams.rgb;
    }
    float envUsable = 1.0;
    if(iblAmbient.y > 0.5)
        envUsable = (dot(envSrc, envSrc) > 1e-6) ? 1.0 : 0.0;
    // Blend env with subtle IBL tint for depth
    float3 iblBlend = lerp(envSrc, envSrc + iblSample * 0.08 * LF(4), 0.4);
    // Sky contribution: upward-facing surfaces reflect sky color from the top of the sphere map
    float skyBlend = saturate(N.y) * skyParams.w;
    iblBlend = lerp(iblBlend, skyParams.rgb, skyBlend * 0.3 * LF(5));
    // Universal sky tint folded into the REFLECTION/cloud layer (not the global
    // composite): it MODULATES what the surface mirrors — cloud/sky — so it can tint
    // without lifting blacks. (Multiplicative; shadows preserved.)
    iblBlend *= SkyHemisphereTint(N);
    // Clearcoat Fresnel: carcols shininess drives env gloss intensity
    // fxParams.w = envData->GetShininess() * 8 * envShininessMult — the same carcols
    // value the VS bakes into IN.envColor.a (which the glass shader reads).
    // fxParams.y is envPower (≈20 default) — it now drives the SUN-GLINT
    // exponent below (envGlint), NOT paint glossiness (c22.x) — this comment
    // is preserved: .y must not drive gloss. It must not saturate to a flat
    // 1.0 either: min-guarded and used only as a pow() exponent.
    // Fresnel F0 rises from dielectric (0.04) toward the material F0 for metals,
    // so chrome/wheel gain a strong grazing-to-normal mirror band.
    float clearcoatFresnel = SchlickFresnelScalar(NdotV, lerp(0.04, max(specularF0, 0.04), metallicness));
    // Part A2: widened from saturate() (which clamped every mult>1 back to 1.0
    // — the "envShininessMult doesn't work" bug). cap 4.0 honors mult>1 while
    // keeping ≤1 values bit-identical to the old saturate (CPU also clamps
    // shininess to 1.0 whenever the shaped mult ≤ 1).
    float carcolsShine = min(fxParams.w, 4.0f);  // 0..4 carcols shininess (1.0 = legacy normalized)
    // Part B (chromeClearcoat, PS c44.x): strength of the dielectric clearcoat
    // highlight band. Default 0.6 = legacy hardcode (identity when chromeClearcoat
    // = 0.6). Blend by metallicness: chrome (metallicness≈1) gets the full config
    // knob, paint stays on the legacy 0.6 so the slider at default changes nothing.
    float ccStr = lerp(0.6, chromeParams.x, metallicness);
    // Gloss strength: visible env reflection at all angles (0.45 base), stronger
    // at grazing. v3 (0.10 base × 1.5 boost × 0.15 grazing weight) put face-on
    // env at ~2% — invisible. v1 (0.25-0.8 × 2.0 × 0.5) white-washed dark paint.
    // This sits between: ~0.2 added face-on on a 0.3 paint, ~0.6 at grazing.
    // Paint tinting: reflection tinted by paint color at normal incidence, white at grazing.
    // Metals reflect neutrally (white); paint reflections stay paint-tinted.
    float3 reflTint = lerp(matCol.rgb, float3(1,1,1), max(clearcoatFresnel, metallicness));
    // Part A1 (envPower): fxParams.y as the SUN-GLINT exponent — the reflection
    // brightens where the mirror direction lines up with the sun; higher power =
    // tighter/more pinpoint glint, lower = broader. Never touches paint gloss
    // (c22.x) or carcolsShine, min-guarded ≥1 so 0 can't wash the term, and the
    // blend only ADDS (default power≈20 → a small hot spot on sun-facing
    // reflections; no darkening anywhere, so default output stays ~parity).
    float envPow = max(fxParams.y, 1.0f);
    float envGlint = pow(saturate(dot(R_world, L)), envPow);
    float3 envTerm = iblBlend * reflTint * envContent * (1.0 + 0.5 * envGlint);
    // Clearcoat coverage scalar (carcols-shade safe): paint dominates face-on
    // (kr ~0.15); carcols shininess widens the env band toward grazing. This is
    // the weight of the ADDITIVE env layer below, never a body replacement.
    float kr = saturate(clearcoatFresnel * (1.0 + 2.0 * carcolsShine) + 0.05) * LF(1);
    kr = lerp(kr, saturate(kr * 1.5), metallicFactor);
    // Chrome/wheel: guarantee a visible mirror band regardless of paint carcols,
    // so metallic trim reads as metal instead of dark tinted paint.
    kr = saturate(max(kr, metallicness * 0.55));
    // Coverage inputs for the additive env layer:
    //   * envCoverage — the strength knob scales the env layer's weight, never
    //     the env content (chromeParams.z note above);
    //   * envUsable   — an environment with no content contributes nothing.
    // Toggle off (config->vehEnvFallback=0) => envUsable stays 1 => legacy.
    kr = saturate(kr * envCoverage * envUsable);
    // ---- c270294 ADDITIVE CLEARCOAT COMPOSITE (restored) ----
    // Sept-3 target: body + environment; the env NEVER replaces the paint.
    // The lerp(layer1, envTerm, kr) form erased the body at grazing (kr -> 1)
    // and read as an x-ray sphere. The old envMask does not exist any more;
    // the substitute is saturate(kr) * 0.5 — kr is the current coverage
    // scalar (clearcoat Fresnel x carcols shine + metallic band, scaled by
    // envCoverage x envUsable and LF(1)), whose 0..1 range maps onto the old
    // envMask's 0.15..0.50 range. envTerm keeps the newer envContent/envGlint
    // factors and envTexMod restores the target's texture-AO/dirt modulation.
    float3 envTexMod = max(diff.rgb, float3(0.05, 0.05, 0.05));
    float3 layer2 = envTerm * (saturate(kr) * 0.5) * envTexMod;

    // [missing term: standalone MOBILE sun glint]
    // main_mobileVehicle adds pow(dot(reflectV, sunDir), 10) * strength * 2
    // * sunColor directly on top of its env lerp — texture-INDEPENDENT, so the
    // highlight survives a dead reflectionTex (Modern's envGlint is multiplied
    // by the env source and dies with it). Off by default
    // (config->vehEnvGlint = 0) because Modern already carries the GGX sun lobe
    // and envGlint; also gated by LF(2) (sun/spec layer) so the existing layer
    // bitmask can switch it off independently.
    if(chromeParams.w > 0.0){
        float mobileGlint = pow(saturate(dot(R_world, L)), 10.0);
        layer2 += reflTint * sunContrib * (mobileGlint * chromeParams.w * 2.0) * LF(2);
    }

    // ---- Specular: GGX/Smith for direct sun highlight ----
    // glTF KHR_materials_pbrSpecularGlossiness: D_GGX and V_SmithCorrelated
    // take σ = (1 - glossiness) and square internally to get α = σ²
    float3 specTotal = float3(0, 0, 0);
    if(NdotL_sun > 0.0){
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);
        float D = D_GGX(NdotH, 1.0 - glossiness);
        float Vis = V_SmithCorrelated(NdotV, NdotL_sun, 1.0 - glossiness);
        float3 F = F_SchlickLH(LdotH, F0);
        specTotal += D * F * Vis * NdotL_sun * sunContrib;
        // Clearcoat specular: white dielectric highlight on top of paint
        float D_cc = D_GGX(NdotH, 0.25);  // very smooth clearcoat
        float Vis_cc = V_SmithCorrelated(NdotV, NdotL_sun, 0.25);
        float3 F_cc = F_SchlickLH(LdotH, float3(0.04, 0.04, 0.04));  // clearcoat F0
        specTotal += D_cc * F_cc * Vis_cc * NdotL_sun * sunContrib * ccStr * LF(6);
    }
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float3 H = normalize(V + Ll);
        float NdotL = max(dot(N, Ll), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(Ll, H), 0.0);
        if(NdotL > 0.0){
            float D = D_GGX(NdotH, 1.0 - glossiness);
            float Vis = V_SmithCorrelated(NdotV, NdotL, 1.0 - glossiness);
            float3 F = F_SchlickLH(LdotH, F0);
            specTotal += D * F * Vis * NdotL * lightCol[i].rgb;
            // Clearcoat per-light
            float D_cc_l = D_GGX(NdotH, 0.25);
            float Vis_cc_l = V_SmithCorrelated(NdotV, NdotL, 0.25);
            float3 F_cc_l = F_SchlickLH(LdotH, float3(0.04, 0.04, 0.04));
            specTotal += D_cc_l * F_cc_l * Vis_cc_l * NdotL * lightCol[i].rgb * ccStr * LF(6);
        }
    }

    // ---- Clustered Point Lights (Forward+) ----
    // Atlas scheme: 16 indices/tile from s5, lights from s6/s7 (see
    // ClusterLighting). Same GGX + clearcoat + Burley math as before — the
    // kDv diffuse, ccStr clearcoat (sigma 0.05) and F_SchlickLH flavor of the
    // old 4-light constant loop are preserved via the helper parameters.
    float3 clusterDiffuse = float3(0, 0, 0);
    ClusterLighting(vpos.xy, IN.WorldPos, N, V, F0, 1.0 - glossiness,
                    baseColor, kDv, ccStr, 1.0, 1.0,
                    specTotal, clusterDiffuse);

    // Edge highlight: subtle Fresnel rim catches sun at grazing angles.
    // Reduced from 0.45 — game lighting already has edge detail from VS.
    float rimFresnel = pow(1.0 - saturate(NdotV), 2.0);
    float3 rimLight = sunContrib * rimFresnel * 0.10 * LF(3);

    specTotal *= 1.0 * LF(2);  // visible sun glints
    // Part A3 (envSpecularityMult, PS c44.y): the CPU surfProps.specular mult was
    // dead (main() never reads surfProps.y / c0.z), so the slider did nothing.
    // Scale the final spec combine here instead — c44.y default 1.0 = identity.
    // (Global specular scale approximation; chromeEnvBoost rides c3.x separately.)
    specTotal *= max(chromeParams.y, 0.0f);

    // ---- COMPOSITE ----
    // layer1 = VS game lighting (ambient + 7 directional × matCol, matches building pipe)
    // layer2 = additive env gloss (c270294: color = layer1 + layer2 — the body is
    // never removed at grazing angles, which was the see-through-sphere bug)
    // specTotal and rimLight are additive on top
    // PENDING in-game A/B (reverted after black-screen regression) — keep this
    // additive clearcoat composite as-is.
    float3 color = layer1 + layer2;
    color += specTotal;                                // specular highlights (base + clearcoat)
    color += rimLight;                                 // Fresnel rim on top of clearcoat
    color += clusterDiffuse;                           // street-lamp diffuse (was missing)

    // Output linear HDR — PostFX TonemapPass handles everything.
    // c48.w = per-texture alpha override (1 = force opaque, 0 = texture alpha).
    return float4(max(color,0.0), lerp(diff.a, 1.0, nmParams.w));
}

// ============================================================
// LEGACY VEHICLE ENTRY POINTS
// Each compiled independently via /E <entry>
// ============================================================

// Shared legacy helper
float specTermLegacy(float3 reflVec, float3 light, float pwr)
{
    return pow(max(dot(reflVec, light), 0.0), pwr);
}

// ============================================================
// Pass: PS2 env + spec dual-layer
// Entry: main_ps2EnvSpecFx
// ============================================================
struct PS_INPUT_PS2SPEC {
    float4 position : POSITION;
    float3 texcoord1 : TEXCOORD0;
    float3 texcoord2 : TEXCOORD1;
    float4 envcolor  : COLOR0;
    float4 speccolor : COLOR1;
};

float4 main_ps2EnvSpecFx(PS_INPUT_PS2SPEC IN, float4 vpos : VPOS) : COLOR
{
    float4 color = tex2D(envMapTex, IN.texcoord1.xy) * IN.envcolor +
           tex2D(maskTex, IN.texcoord2.xy) * IN.speccolor;
    // NO ClusterLightingScreen here: this additive FX pass binds neither the
    // Forward+ constants (c45) nor the light atlases (s5-s7), so clustered
    // lights would be an undefined read. FX quads stay unclustered.
    return saturate(color);
}

// ============================================================
// Pass: Specular car FX
// Entry: main_specCarFx
// ============================================================
struct PS_INPUT_SPECCAR {
    float4 position : POSITION;
    float3 texcoord1 : TEXCOORD0;
    float4 envcolor  : COLOR0;
    float4 speccolor : COLOR1;
};

float4 main_specCarFx(PS_INPUT_SPECCAR IN, float4 vpos : VPOS) : COLOR
{
    float4 color = tex2D(envMapTex, IN.texcoord1.xy) * IN.envcolor + IN.speccolor;
    // NO ClusterLightingScreen here: this additive FX pass binds neither the
    // Forward+ constants (c45) nor the light atlases (s5-s7), so clustered
    // lights would be an undefined read. FX quads stay unclustered.
    return saturate(color);
}

// ============================================================
// Pass: Mobile vehicle
// Entry: main_mobileVehicle
// ============================================================
struct PS_INPUT_MOBILE {
    float2 texcoord0 : TEXCOORD0;
    float3 texcoord1 : TEXCOORD1;
    float4 color     : COLOR0;
    float3 spec      : COLOR1;
};

float4 main_mobileVehicle(PS_INPUT_MOBILE IN, float4 vpos : VPOS) : COLOR
{
    float4 col = tex2D(diffuseTex, IN.texcoord0) * IN.color;

    float2 ReflPos = (length(IN.texcoord1.xy) > 1e-6 ? normalize(IN.texcoord1.xy) : float2(0, 0)) * (IN.texcoord1.z * 0.5 + 0.5);
    ReflPos = ReflPos * float2(0.5, -0.5) + float2(0.5, 0.5);
    float4 ReflCol = tex2D(envMapTex, ReflPos);
    col.rgb = lerp(col.rgb, ReflCol.rgb, fxParams.y);
    col.a += ReflCol.b * 0.125;

    col.rgb += IN.spec;

    // Forward+ cluster response (was missing): this VS gives only a world
    // reflection vector (no world position/normal) and the mobile cb uploads
    // no eye/view rows, so the adapted screen-ray variant lights the composed
    // surface color (see ClusterLightingScreen). Small clearcoat (0.3) since
    // this is a full vehicle surface pass.
    float3 clusterSpec = float3(0, 0, 0);
    float3 clusterDiff = float3(0, 0, 0);
    ClusterLightingScreen(vpos.xy, float3(0.04, 0.04, 0.04), 0.5,
                          col.rgb, float3(0.5, 0.5, 0.5), 0.3, 1.0,
                          clusterSpec, clusterDiff);
    col.rgb += clusterSpec + clusterDiff;

    col.rgb = saturate(col.rgb);
    return col;
}

// ============================================================
// Pass: Normal-mapped vehicle
// Entry: main_normMapVehicle
// ============================================================
struct PS_INPUT_NORMMAP {
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float4 color       : COLOR0;
    float4 envColor    : COLOR1;
};

float4 main_normMapVehicle(PS_INPUT_NORMMAP IN, float4 vpos : VPOS) : COLOR
{
    float4 diff = tex2D(diffuseTex, IN.texcoord0) * IN.color;
    // Compute view-space reflection vector for camera-rendered sphere map
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = float3(0, 0, 1);  // approximate view dir in view space
    float3 R_world = reflect(-V, N);
    float3 R_view = float3(dot(R_world, viewRight), dot(R_world, viewUp), dot(R_world, viewFwd));
    float2 envUV = SphereEnvMapUV(R_view, V);
    float4 env = tex2D(envMapTex, envUV);
    // Lerp-replace: env replaces diffuse at grazing angles (clearcoat behavior)
    float envBlend = saturate(IN.envColor.a * 0.8);
    float3 color = lerp(diff.rgb, env.rgb * IN.envColor.rgb, envBlend);

    // Forward+ cluster response (was missing). This entry has real WorldPos /
    // WorldNormal, so the full world-space helper applies. Adapted inputs:
    // the entry exposes no material registers (c22 is stale off the PBR
    // paths), so a fixed dielectric preset is used (F0 0.04, roughness 0.5),
    // and V is derived from eyePos (the env blend above keeps its own
    // approximate (0,0,1) — untouched).
    float3 toEye = eyePos - IN.WorldPos;
    float3 Vn = length(toEye) > 1e-6 ? toEye / length(toEye) : float3(0, 0, 1);
    float3 clusterSpec = float3(0, 0, 0);
    float3 clusterDiff = float3(0, 0, 0);
    ClusterLighting(vpos.xy, IN.WorldPos, N, Vn,
                    float3(0.04, 0.04, 0.04), 0.5,
                    diff.rgb, float3(1.0, 1.0, 1.0), 0.0, 1.0, 1.0,
                    clusterSpec, clusterDiff);
    color.rgb += clusterSpec + clusterDiff;

    color.rgb = saturate(color.rgb);
    return float4(color.rgb, diff.a);
}

// ============================================================
// Pass: Building (unified PBR - material-driven, not arch-type)
// Entry: main_building
//
// BRDF material properties uploaded via c22:
//   c22 = {glossiness, reflectance, clearcoat, subsurface}
//   c23 = {specularTintR, specularTintG, specularTintB, 0}
//
// Uses vertex color as base color (buildings use vertex color).
// Supports day/night blending via vertex alpha.
// ============================================================
struct PS_INPUT_BUILDING {
    float2 texcoord0 : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float3 WorldPos    : TEXCOORD2;
    float3 ViewDir     : TEXCOORD3;
    float3 SunDir      : TEXCOORD4;
    // Camera-space normal from buildingPBRVS (TEXCOORD5) — MUST match the VS
    // output list or the halves drift and the env UV reads an undefined
    // interpolator. Ported from GTAIVBuilding_vs' Texcoord1.
    float3 CamNormal   : TEXCOORD5;
    float4 color       : COLOR0;  // vertex color (day/night blend in alpha)
    float4 dayNight    : COLOR1;  // day/night parameters
};

float4 main_building(PS_INPUT_BUILDING IN, float4 vpos : VPOS) : COLOR
{
    // Base color from texture * vertex color
    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    float3 baseColor = diff.rgb * IN.color.rgb;

    // BRDF material properties from library
    float glossiness   = pbrParams.x;  // from c22 (CryEngine glossiness workflow)
    // NM lane gloss-in-alpha (see main() note): gloss = nmTex.a, blended 85%
    // toward the map; c48.z gates on the CPU format check. Sampled here —
    // BEFORE roughness is derived — so the wet-road block below still layers
    // on top of the map's gloss.
    float4 nmS = float4(0.5, 0.5, 1.0, 1.0);
    if(nmParams.x > 0.5)
        nmS = tex2D(nmTex, IN.texcoord0);
    if(nmParams.x > 0.5 && nmParams.z > 0.5)
        glossiness = lerp(glossiness, nmS.a, 0.85);
    float roughness    = 1.0 - glossiness;
    float reflectance  = pbrParams.y;
    float clearcoat    = pbrParams.z;
    float subsurface   = pbrParams.w;
    float3 specularTint = paintNoise.xyz; // c23 = {specTintR, specTintG, specTintB}

    // Wet roads recovery: dayparam[3] = CWeather__WetRoads (0-1)
    // Passed via vertex color alpha from buildingPipe.cpp setDnParams()
    float wetRoads = IN.dayNight.a;

    // Wet surfaces: increase reflectance, decrease roughness, darken diffuse
    // This simulates water filling surface micro-structure (beta restoration)
    if(wetRoads > 0.01){
        // Wet surfaces are smoother (water fills gaps)
        roughness *= lerp(1.0, 0.3, wetRoads);
        // Wet surfaces are more reflective (higher F0)
        reflectance = lerp(reflectance, 0.04, wetRoads);  // water F0 = 0.04
        // Wet surfaces darken slightly (water absorption)
        baseColor *= lerp(1.0, 0.85, wetRoads);
        // Increase glossiness for wet surfaces
        glossiness = lerp(glossiness, 0.8, wetRoads);
    }

    // Normals
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = length(IN.ViewDir) > 1e-6 ? IN.ViewDir / length(IN.ViewDir) : float3(0, 0, 1);
    float3 L = length(IN.SunDir) > 1e-6 ? IN.SunDir / length(IN.SunDir) : float3(0, 0, -1);

    // Blend with screen-space normal (if available) — mirrors vehicle main().
    // ambientColor.w > 0.5 means the half-res normal buffer is bound and valid
    // (buildingPipe uploads c24.w=1.0 when s4 is set). Screen UV from VPOS.
    if(ambientColor.w > 0.5){
        float2 screenUV = vpos.xy * screenSize.zw;
        float4 ssSample = tex2D(normalBufTex, screenUV);
        float3 ssNormalView = ssSample.rgb * 2.0 - 1.0;
        float ssLen = length(ssNormalView);
        // Validity gate (strengthened): NormalBuffer encodes normalized
        // normals, so valid texels decode to unit length; stale/garbage texels
        // (never-cleared RT, skipped refresh, resolution-change remnants)
        // decode to arbitrary length and must NOT tilt N (GGX blowout — the
        // "white ground patches"). The sky marker (0.5,0.5,1.0) is unit-length
        // but carries depth-alpha 1.0 (NormalBuffer writes depth/far in alpha),
        // so reject it too — one-frame-stale sky must not blend where geometry
        // now is. Lerp-OUT invalid (weight -> 0) instead of a binary accept.
        float ssValid = smoothstep(0.55, 0.85, ssLen)
                      * (1.0 - smoothstep(1.15, 1.45, ssLen))
                      * (1.0 - step(0.99, ssSample.a));
        // View -> world: same c25-c27 world->view rotation rows as the vehicle
        // main() block above. NormalBuffer.hlsl is view-space; blending it raw
        // into this world-space N tilted the face shading with the camera.
        float3 ssNormalWorld = ssNormalView.x * viewRight
                             + ssNormalView.y * viewUp
                             + ssNormalView.z * viewFwd;
        float ssWorldLen = length(ssNormalWorld);
        // Fail closed on zero/garbage view rows (buildingPipe.cpp uploads
        // c25-c27 before dispatch; a missing upload must not teleport the
        // normal). Same |rowsLen-3| gate as ClusterLighting.
        float rowsLen = dot(viewRight, viewRight) + dot(viewUp, viewUp) + dot(viewFwd, viewFwd);
        ssValid *= (abs(rowsLen - 3.0) < 0.5) ? 1.0 : 0.0;
        // Anti-flip: same hemisphere as the geometric face; for horizontal
        // surfaces (|N.z| significant) also require the same world-Z sign
        // (Z-up). Near-vertical walls skip the Z test so wall detail blends.
        // NaN fails every comparison -> weight 0; ssWorldLen guards the divide.
        ssValid *= (ssWorldLen > 1e-6 && dot(ssNormalWorld, N) > 0.0) ? 1.0 : 0.0;
        ssValid *= (abs(N.z) < 0.1 || ssNormalWorld.z * N.z >= 0.0) ? 1.0 : 0.0;
        if(ssValid > 0.001)
            N = normalize(lerp(N, ssNormalWorld / max(ssWorldLen, 1e-7), 0.3 * ssValid));
    }

    // NM lane: tangent perturbation from the texture normal map (s8/c48).
    // Frame computed at TOP-LEVEL flow (ddx/ddy — see the NM lane note at the
    // cotangent-frame helper); nmS was sampled at the gloss decode above.
    float3 nmT, nmB;
    nmCotangentFrame(N, -V, IN.texcoord0, nmT, nmB);
    if(nmParams.x > 0.5)
        N = nmPerturbNormal(N, V, nmS, IN.texcoord0, nmT, nmB, nmParams.y);

    // Core PBR vectors
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    // F0: dielectric reflectance, optionally tinted by specular tint
    float3 F0 = lerp(float3(reflectance, reflectance, reflectance), baseColor * reflectance, specularTint.r);

    // Fresnel
    float3 F_atNdotV = F_Schlick(NdotV, F0);
    float3 kS = F_atNdotV;
    // BUG FIX (interior blackout): kD is the DIFFUSE energy budget and must be
    // VIEW-INDEPENDENT. kD = 1-kS tied it to view-dependent Fresnel: at grazing
    // angles (or normal-mapped NdotV -> 0) F_Schlick -> 1, kD -> 0, and
    // ClusterShadeOne's point-light diffuse vanished indoors (black at random
    // camera angles). EnergyConservation(F0) = saturate(1 - luminance(F0)).
    // PENDING in-game A/B (reverted after black-screen regression) — kD /
    // EnergyConservation (and the bldEnv block below) stay as-is.
    float3 kD = EnergyConservation(F0);

    // NOTE: Ambient is already baked into IN.color.rgb by the VS
    // (OUT.Color = (prelight*surfDiff + ambient*surfAmb) * matCol).
    // baseColor = tex * IN.color already contains ambient.
    // DO NOT add ambientColor again — that creates ambient² and washes out blacks.
    // Industry precedent: Skyrim Community Shaders fixed identical double-ambient bug
    // by setting vertexColor=1 for PBR paths.

    // Direct light from timecycle (PS c5) — 1:1 match with ped pipeline
    float3 sunContrib = directCol.rgb;

    // Diffuse lighting (Burley)
    float3 H = length(V + L) > 1e-6 ? normalize(V + L) : float3(0, 0, 1);
    float VdotH = max(dot(V, H), 0.0);
    float diffuse = BurleyDiffuse(NdotL, NdotV, VdotH, roughness);

    // Specular (GGX/Smith)
    float NdotH = max(dot(N, H), 0.0);
    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithCorrelated(NdotV, NdotL, roughness);
    float3 F = F_Schlick(max(dot(L, H), 0.0), F0);

    float3 specTotal = D * F * Vis * NdotL * sunContrib;

    // Multi-light accumulation
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdotL_l = max(dot(N, Ll), 0.0);
        if(NdotL_l > 0.0){
            float3 Hl = length(V + Ll) > 1e-6 ? normalize(V + Ll) : float3(0, 0, 1);
            float NdotH_l = max(dot(N, Hl), 0.0);
            float Dl = D_GGX(NdotH_l, roughness);
            float Visl = V_SmithCorrelated(NdotV, NdotL_l, roughness);
            float3 Fl = F_Schlick(max(dot(Ll, Hl), 0.0), F0);
            specTotal += Dl * Fl * Visl * NdotL_l * lightCol[i].rgb;
        }
    }

    // ---- Clustered Point Lights (Forward+) ----
    // Atlas scheme (s5 2x2 tile block + s6/s7 light atlases, see
    // ClusterLighting). Building flavor preserved: F_Schlick (not LH), NO
    // clearcoat lobe (ccStr=0), Burley diffuse with kD.
    float3 clusterDiffuse = float3(0, 0, 0);
    ClusterLighting(vpos.xy, IN.WorldPos, N, V, F0, roughness,
                    baseColor, kD, 0.0, 1.0, 0.0,
                    specTotal, clusterDiffuse);

    // Universal dynamic-sky ambient tint — MULTIPLICATIVE (see SkyHemisphereTint):
    // modulates the surface so black stays black and shadows are preserved.
    // This term consumes the SAME iblTex (s3) capture as the sky-reflection
    // lerp below, so it gets the same dark-capture rule: fade the tint's
    // DEVIATION FROM NEUTRAL 1.0 by the capture's own luminance. A dim
    // indoor/night capture then contributes proportionally less tint instead
    // of colouring a surface it has no valid sky data for. Scaling the tint's
    // magnitude itself would darken the body; fading toward 1.0 is the
    // multiplicative-safe form of "scale by sample luminance". NaN luminance
    // fails the > 0 compare -> gate 0 -> neutral (NaN = black rule).
    float3 ibl = SkyHemisphereTint(N);
    float3 iblRaw = tex2D(iblTex, float2(0.5, saturate(N.y * 0.5 + 0.5))).rgb;
    float iblLum = dot(iblRaw, float3(0.2126, 0.7152, 0.0722));
    float iblGate = (iblLum > 0.0) ? saturate(iblLum * 6.0) : 0.0;
    ibl = lerp(float3(1.0, 1.0, 1.0), ibl, iblGate);

    // Composite: modulate the VS-lit base by the sky tint (multiplicative => shadows
    // preserved), then add PBR specular and cluster lights.
    float3 color = baseColor * ibl;
    color += specTotal;
    color += clusterDiffuse;

    // ==== ENVIRONMENT REFLECTION — PORTED FROM THE GTA IV BUILDING PATH ====
    // [the gap this closes] IV, PS2 and Xbox ALL give materials flagged with
    // the envmap bit (`*(int*)&material->surfaceProps.specular & 1`) a
    // reflection: their callbacks bind CustomEnvMapPipeMaterialData::texture
    // to s1 and run a second additive Envcolor = 192/128 * shininess pass
    // (buildingPipe.cpp PS2 :683 / Xbox :809). The PBR cb had no equivalent
    // branch at all, so under Building=PBR those materials rendered FLAT —
    // PBR's reflection path was strictly WEAKER than IV's, which is what this
    // ports (plus a superset term IV does not have).
    //
    // c47 = {enable, materialEnvWeight, skyReflectStrength, 0}:
    //   .x  config->bldEnvReflect   — 0 disables the whole block
    //   .y  1.5 * envData->GetShininess() for an env-mapped material, else 0
    //       (192/128 == 1.5 is the exact Envcolor scale the IV/PS2 FX pass
    //       uses; the CPU only fills it when s1 really holds an env map)
    //   .z  config->bldSkyReflect   — Fresnel-weighted sky reflection from the
    //       already-bound IBL capture, for materials with NO env map
    //   UV  TEXCOORD5 = camera-space normal, `n.xy * 0.5 + 0.5` — IV's model
    //       (GTAIVBuilding_ps does exactly this remap; its c0 = {0.5,0,0,0}
    //       is the same affine map, folded into the expression here).
    // PENDING in-game A/B (reverted after black-screen regression) — keep bldEnv as-is.
    if(bldEnv.x > 0.0){
        if(bldEnv.y > 0.0){
            float2 envUV = saturate(IN.CamNormal.xy * 0.5 + 0.5);
            float3 envSample = tex2D(envMapTex, envUV).rgb;
            // Never let a dead/empty sample touch the body: an unbound or
            // black s1 would ADD nothing anyway, but a NaN would poison it
            // (NaN * w is still NaN and renders black) — the same defined-
            // sample rule the IV callback applies by binding the diffuse to s1.
            //
            // ENERGY MODEL: weight the additive env term by the Schlick
            // Fresnel F(NdotV) (PBR_Common F_Schlick) instead of adding a FLAT
            // envSample * bldEnv.y. The PBR base above is already fully lit, so
            // a flat add saturated env-mapped glass/metal facades to white.
            // F is the physically correct reflectance lobe: face-on dielectrics
            // barely add (F -> F0 ~= 0.04) while only grazing views approach a
            // mirror (F -> 1); tinted F0 also keeps metals colour-correct.
            // bldEnv.y remains the CPU-supplied intensity input — it is now
            // gated by F, so a high shininess (1.5 * shininess) no longer
            // blows out. F_atNdotV is guaranteed finite (F0 in [0,1], NdotV in
            // [0,1]) so no NaN can reach the additive.
            if(dot(envSample, envSample) > 1e-6)
                color += envSample * F_atNdotV * bldEnv.y;
        }else if(bldEnv.z > 0.0){
            // Superset term (NOT in IV): reflect the sky capture along the
            // mirrored view vector. s3 is already bound by the cb, and y is
            // sampled as ELEVATION — the same convention skyFill and
            // SkyHemisphereTint already use, so no new sampler or UV scheme.
            float3 R = reflect(-V, N);
            float3 skyEnv = tex2D(iblTex, float2(0.5, saturate(R.y * 0.5 + 0.5))).rgb;
            // GRAZING-ANGLE BLACK-SCREEN GUARD — three named constants, no new
            // register (all derive from bldEnv.z / F_atNdotV / the sample):
            //   BLD_SKY_LUM_FLOOR  0.10: a capture below this luminance is
            //                      black in all but name (the old 0.02 floor
            //                      accepted it) and must never replace the
            //                      body where F -> 1.
            //   BLD_ENV_WEIGHT_MAX 0.85: grazing limiter — even a fully-faded
            //                      Fresnel may own at most this fraction of
            //                      the surface; the rest stays body colour.
            //   BLD_SKY_LUM_GAIN   6.0: dim sample -> proportionally weaker
            //                      blend (full weight at skyLum >= 1/6).
            const float BLD_SKY_LUM_FLOOR  = 0.10;  // raised from 0.02: a
                                                    // 0.02-lum sample is black
                                                    // yet used to replace the body
            const float BLD_ENV_WEIGHT_MAX = 0.85;
            const float BLD_SKY_LUM_GAIN   = 6.0;
            // LUMINANCE floor: indoors the DynamicSky capture is dark and
            // meaningless, so at grazing F->1 a dark sample REPLACED the body
            // and geometry went black at view-dependent angles. A near-black
            // sample must NEVER replace the body; bright outdoor sky keeps
            // reflecting. A NaN luminance fails the compare -> skyValid = 0
            // (NaN = black rule).
            float skyLum = dot(skyEnv, float3(0.2126, 0.7152, 0.0722));
            float skyValid = (skyLum > BLD_SKY_LUM_FLOOR) ? 1.0 : 0.0;
            // Energy-consistent F*env + (1-F)*body (same rule the vehicle
            // lane uses): an environment with no content replaces NOTHING, so
            // a dead IBL can never darken a wall the way a raw lerp would.
            float envW = saturate(bldEnv.z * saturate(F_atNdotV.r) * skyValid);
            // Grazing limiter + luminance-proportional scale. The scale is
            // applied ONLY when skyValid == 1: skyLum may be NaN on a dead
            // sample, and 0 * NaN is still NaN — the branch (not arithmetic)
            // is what enforces the NaN = black rule here.
            envW = min(envW, BLD_ENV_WEIGHT_MAX);
            if(skyValid > 0.5)
                envW *= saturate(skyLum * BLD_SKY_LUM_GAIN);  // dim sample -> weaker
            color = lerp(color, skyEnv, envW);
        }
    }

    // Output linear HDR — PostFX TonemapPass handles tonemapping uniformly.
    // Alpha must match PS2 simplePS (tex * IN.color * colorscale → a = tex.a
    // * color.a): vertex/material alpha (WIND fade, decal vertex fade) was
    // dropped → alpha-blended road decals rendered opaque → black marks.
    // c48.w = per-texture alpha override (1 = force opaque, 0 = texture alpha).
    // Paint-marker guard: material alpha byte 16 (c19.a = 16/255 = 0.063) is
    // the SA "use vehicle paint" marker, not translucency — it reaches here as
    // IN.color.a and would make painted surfaces ~6% opaque. Real fades stay
    // below 0.1 only in the marker band, so lift exactly that band to 1.
    float matA = IN.color.a;
    if(matA > 0.0 && matA < 0.1) matA = 1.0;
    return float4(max(color, 0.0), lerp(saturate(diff.a * matA), 1.0, nmParams.w));
}

// ============================================================
// RUBBER/TIRE ENTRY POINT
// Entry: main_rubber
// Pure diffuse rubber — no specular, no Fresnel sheen (tires are matte)
// c22 = {roughness, F0, tintR, tintG}  (tireParams)
// c23 = {tintB, dirtLevel, wearFactor, 0}  (tireParams2)
// ============================================================
float4 main_rubber(PS_INPUT IN, float4 vpos : VPOS) : COLOR
{
    float3 N = length(IN.WorldNormal) > 1e-6 ? IN.WorldNormal / length(IN.WorldNormal) : float3(0, 1, 0);
    float3 V = length(IN.ViewDir) > 1e-6 ? IN.ViewDir / length(IN.ViewDir) : float3(0, 0, 1);
    float3 L = length(IN.SunDir) > 1e-6 ? IN.SunDir / length(IN.SunDir) : float3(0, 0, -1);

    float4 diff = tex2D(diffuseTex, IN.texcoord0);
    float3 baseColor = diff.rgb * matCol.rgb;

    float roughness  = pbrParams.x;
    float dirtLevel  = paintNoise.y;
    float wearFactor = paintNoise.z;

    // Wet mud DARKENS tires (not brightens). Dry dust is the only brightener.
    float3 wetMud = float3(0.15, 0.12, 0.10);  // dark wet mud
    float3 dryDust = float3(0.35, 0.30, 0.25);  // light dry dust
    baseColor = lerp(baseColor, wetMud, dirtLevel * 0.5);    // wet mud darkens
    baseColor = lerp(baseColor, dryDust * baseColor, wearFactor * 0.15);  // dry dust lightens slightly

    // Rubber sky-lift: MULTIPLICATIVE, so tyres keep their dark hue instead of
    // gaining a grey additive wash (matches the body path). Fades out at night.
    float3 rubberSky = tex2D(iblTex, float2(0.5, saturate(N.y * 0.5 + 0.5))).rgb;
    baseColor *= 1.0 + dot(rubberSky, float3(0.299, 0.587, 0.114)) * 0.12 * LF(4);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL_sun = max(dot(N, L), 0.0);

    // Ambient: rubber is dark and matte — reduce ambient to 35% of timecycle value
    // so tires don't glow brighter than hub caps or wheel wells
    float3 ambient = ambientColor.rgb * baseColor * 0.35;

    // Wrap diffuse — tighter wrap than before, rubber shouldn't scatter this much
    float subsurface = 0.08;
    float wrapDiffuse = saturate((NdotL_sun + subsurface) / (1.0 + subsurface));
    float3 color = baseColor * wrapDiffuse * directCol.rgb + ambient;

    // Point lights — reduced contribution, rubber should stay dark
    for(int i = 0; i < 6; i++){
        float3 Ll = -lightDir[i];
        float NdL = max(dot(N, Ll), 0.0);
        float wrap = saturate((NdL + subsurface) / (1.0 + subsurface));
        color += baseColor * wrap * lightCol[i].rgb * 0.08;
    }

    // Cluster (street) lights — tyres were lit ONLY by the 6 directional lobes,
    // so at night they went pure black while the body received cluster diffuse.
    // Matching the body keeps wheel shading in step with the paint. Atlas scheme
    // (s5 2x2 tile block + s6/s7): diffuse-only flavor preserved via specW=0
    // and kD=0.5 (old `* 0.5 / 3.14159`); the helper adds straight into color.
    float3 rubberClusterSpec = float3(0, 0, 0);  // unused (specW=0)
    ClusterLighting(vpos.xy, IN.WorldPos, N, V,
                    float3(0.04, 0.04, 0.04), roughness,
                    baseColor, float3(0.5, 0.5, 0.5), 0.0, 0.0, 0.0,
                    rubberClusterSpec, color);

    // c48.w = per-texture alpha override (1 = force opaque, 0 = texture alpha).
    return float4(color, lerp(diff.a, 1.0, nmParams.w));
}
