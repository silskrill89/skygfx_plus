# GTA IV Shader Overrides — Extraction, SM3.0 Portability, and Port Plan

**Date:** 2026-10-25
**Mode of work:** read-only extraction. No source, shader, resource, or config file was modified.
**Only artifact written:** this file.

**Reference root (shorthand `LR/`):** `E:/SDKs/LibertyRecomp-main/LibertyRecompLib/shader_overrides/`
**Shader-cache evidence (shorthand `LRC/`):** `E:/SDKs/LibertyRecomp-main/LibertyRecompLib/shader/shader_cache.cpp`
**Our root:** `E:/dev(dave)/skygfx_plus_expIV/`

This document supersedes the "opaque blobs only" conclusion of
`docs/plans/2026-10-25-gtaiv-parity-audit.md` **for these 17 files only**. The
audit's statement that `shader_cache.cpp` holds the game's shaders as
compressed blobs remains true; what changes is that 12 of those blobs have a
readable, behaviourally-faithful HLSL reproduction in `shader_overrides/` (11 of
them postfx — the *only* readable GTA IV postfx source in the reference tree),
and that the four TLAD composites are the only readable film-grain source
anywhere in the SDK.

---

## 0. Executive summary

| # | File | Real IV shader (from `LRC/`) | Verdict | Value to us |
|---|------|------------------------------|---------|-------------|
| 1 | `manifest.json` | 18 hash→source mappings | **N/A (data)** | Gives the hash→IV-name table (§1) |
| 2 | `deferred_lighting_ps20_smooth.hlsl` | `deferred_lighting_ps20.bin` | **PORT-WITH-CHANGES** | Analytic sphere-volume integral; 34 instr measured |
| 3 | `deferred_lighting_vs7_smooth.hlsl` | `deferred_lighting_vs7.bin` | **PORT-WITH-CHANGES** | Volume proxy geometry builder (cone/sphere) |
| 4 | `gta_composite_mb.hlsl` | `rage_postfx_ps11.bin` | **PORT-WITH-CHANGES** | 8-tap reprojected MB + 5-tap CoC DoF + full grade |
| 5 | `gta_composite_mb_dof_debug.hlsl` | `rage_postfx_ps15.bin` | **SKIP** | Same math, debug constant layout only |
| 6 | `gta_composite_mb_high_dof.hlsl` | `rage_postfx_ps13.bin` | **PORT-WITH-CHANGES** | Same as #4 with the 8-bit HDR decode + wide DoF band |
| 7 | `gta_composite_mb_high_dof_debug.hlsl` | `rage_postfx_ps17.bin` | **SKIP** | Same math, debug constant layout only |
| 8 | `tlad_composite_noise.hlsl` | TLAD `PS_GTACompositeNoise` (`rage_postfx_e1.fxc`) | **PORT-WITH-CHANGES** | **The grain formula (§4)** |
| 9 | `tlad_composite_mb_noise.hlsl` | TLAD `PS_GTACompositeMBNoise` (`rage_postfx_e1.fxc`) | **PORT-WITH-CHANGES** | Grain + MB |
| 10 | `tlad_composite_high_dof_noise.hlsl` | TLAD `PS_GTACompositeHighDOFNoise` (`rage_postfx_e1.fxc`) | **PORT-WITH-CHANGES** | Grain + wide DoF band |
| 11 | `tlad_composite_mb_high_dof_noise.hlsl` | TLAD `PS_GTACompositeMBHighDOFNoise` (`rage_postfx_e1.fxc`) | **PORT-WITH-CHANGES** | Grain + MB + wide DoF band |
| 12 | `rage_postfx_scalable_bloom.hlsl` | `rage_postfx_ps8.bin` / `ps9.bin` (`PS_BlurX`/`PS_BlurY`) | **PORT** | 25-tap, 124-instr measured separable bloom |
| 13 | `rage_postfx_exponential_height_fog.hlsl` | `rage_postfx_ps19.bin` (`PS_GTADepthEffects`) | **PORT-WITH-CHANGES** | Analytic height-fog line integral; 65 instr measured |
| 14 | `rage_postfx_fog_vs.hlsl` | `rage_postfx_vs0.bin` | **PORT-WITH-CHANGES** | Only needed for the extra view-ray TEXCOORD |
| 15 | `water_ps1_smooth_shorelines.hlsl` | `water_ps1.bin` | **SKIP (container) / deltas already landed** | Its 2 authored deltas are already in `Water_Parallax.hlsl` |
| 16 | `xenos_shader_common.h` | — (host ABI) | **SKIP** + extract 4 helpers | 4-tap bicubic weights are SM3.0-safe |
| 17 | `shader_override_cache.h` | — (host ABI) | **SKIP** | Documents the "PS+VS must be a pair" override contract |

Four facts that shape everything below:

1. **The composites are not "effects" — they are the whole end-of-frame
   resolve.** Every `*composite*` file ends with the same tone-map + colour-grade
   chain (`ToneMapParams`, `Exposure`, `deSatContrastGamma`, `ColorShift`,
   `ColorCorrect`) at `LR/motion_blur/gta_composite_mb.hlsl:1504-1540` and
   `LR/film_grain/tlad_composite_noise.hlsl:1222-1247`. Porting a composite
   wholesale would duplicate `TonemapLUT.hlsl` + `ModernColorFilterPS.hlsl`.
   The portable parts are the sub-blocks (MB taps, CoC band, grain).
2. **There is no deferred *shading* in the deferred-lighting pair.** The PS
   reads only the G-buffer **depth** (`GBufferTextureSampler3`, `used_texture_mask
   0x00000001`) and returns one term: `lightColour * chord²/radius²`. Albedo,
   N·L and specular live in a resolve pass that is *not* in this folder
   (§2.4-§2.5).
3. **The bloom file contains no threshold.** `rage_postfx_ps8/ps9` are the
   separable *blur* of an already-extracted bloom buffer. The only "extraction"
   in the file is the detail high-pass `max(center - center_blur, 0)` (§2.3).
4. **The grain is a 1-tap white-noise texture lookup, not procedural noise** —
   and it lands exactly on our planned `s9` utility tile `B` channel (§4).

**Register headroom used by every plan below** (from the parity audit and
re-verified here: max `c48` / `s8` in `shaders/ps/*.hlsl`): first free PS
constant **`c49`**, first free PS sampler **`s9`**, `ps_3_0` ceiling `c223`/`s15`.

---

## 1. `manifest.json` — what it tells us

### 1.1 The mapping it provides

The manifest keys overrides by 64-bit XXH3 **shader hash**, not by name
(`LR/manifest.json:5-9`):

```json
{ "stage": "pixel", "hash": "0xEE75C9F6AA1AB16A", "language": "hlsl",
  "source": "motion_blur/gta_composite_mb.hlsl", "entry_point": "shaderMain",
  "specialization_constants_mask": "0x00000702",
  "defines": [ "GTA4_RECOMP" ] }
```

Cross-referencing those hashes against `LRC/shader_cache.cpp` yields the real
IV container names — **this is the useful output of the file**:

| Hash | `LRC/` path | Override file |
|------|-------------|---------------|
| `0xEE75C9F6AA1AB16A` | `rage_postfx/rage_postfx_ps11.bin` | `gta_composite_mb.hlsl` |
| `0xE51D9DD95A333D92` | `rage_postfx/rage_postfx_ps13.bin` | `gta_composite_mb_high_dof.hlsl` |
| `0xD3B2B2125BC24911` | `rage_postfx/rage_postfx_ps15.bin` | `gta_composite_mb_dof_debug.hlsl` |
| `0x94CC5AF0E2FF5B08` | `rage_postfx/rage_postfx_ps17.bin` | `gta_composite_mb_high_dof_debug.hlsl` |
| `0x79044EA1461439CA` | `rage_postfx_e1.fxc` (TLAD lib) | `tlad_composite_noise.hlsl` |
| `0xC5D3E7806E478A16` | `rage_postfx_e1.fxc` | `tlad_composite_mb_noise.hlsl` |
| `0x67C1FB770BB55E69` | `rage_postfx_e1.fxc` | `tlad_composite_high_dof_noise.hlsl` |
| `0xE4AF188ACBDA7362` | `rage_postfx_e1.fxc` | `tlad_composite_mb_high_dof_noise.hlsl` |
| `0xE74BFC50127AFED3` | `rage_postfx/rage_postfx_ps8.bin` | `scalable_bloom.hlsl` (`BLOOM_HORIZONTAL`) |
| `0x86CB9F8D0850ABE3` | `rage_postfx/rage_postfx_ps9.bin` | `scalable_bloom.hlsl` (`BLOOM_VERTICAL`) |
| `0xC3256D6D7C2E426D` | `water/water_ps1.bin` | `water_ps1_smooth_shorelines.hlsl` |
| `0xDF64C22EC010C136` | `deferred_lighting/deferred_lighting_vs7.bin` | `deferred_lighting_vs7_smooth.hlsl` |
| `0x458818340E2283DE` | `deferred_lighting/deferred_lighting_ps20.bin` | `deferred_lighting_ps20_smooth.hlsl` |
| `0xEFAACEED3DBD802D` | `rage_postfx/rage_postfx_vs0.bin` | `rage_postfx_fog_vs.hlsl` |
| `0x1A6669BBAFDC43E7` | `rage_postfx/rage_postfx_ps19.bin` | `rage_postfx_exponential_height_fog.hlsl` |
| `0x156BAD4A9EE62726` | `runtime_captured/gta4_156BAD4A9EE62726.container.vert` | `bink/vs_bink.hlsl` |
| `0x9E76B68B60127349` | `runtime_captured/gta4_9E76B68B60127349.container.frag` | `bink/ps_bink.hlsl` |
| `0xA6C9E2B8B2A59D7A` | `runtime_captured/gta4_A6C9E2B8B2A59D7A.container.frag` | `bink/ps_bink_no_alpha.hlsl` |

Notes:

- The `runtime_captured` entries prove the Bink overrides were reverse-engineered
  from captures, not from the shipped blob — they have **no** `rage_shaders/*.bin`
  counterpart. The Bink shaders are out of scope for us (SA uses its own movie path).
- `specialization_constants_mask: 0x00000702` on every pixel entry is the alpha-test
  capability mask (`SPEC_CONSTANT_ALPHA_TEST = 1<<1`, function bits `0x700`) declared in
  `LR/xenos_shader_common.h:6-10`. It means each override carries a compile-time
  alpha-test variant; ours never needs it (we set RS alpha test from C++).

### 1.2 What else exists in `shader_cache.cpp` (the "menu" of further targets)

Parsed from `LRC/shader_cache.cpp` (1,356 named entries):

| Family | Entries | What it is |
|--------|---------|-----------|
| `deferred_lighting/` | 63 (`ps0..ps41`, `vs0..vs20`) | Light-volume proxy shaders — 42 pixel / 21 vertex variants. Only `ps20`+`vs7` are readable. **This is where the per-light-type variants live** (cone/spot/capsule). |
| `rage_postfx/` | 26 (`ps0..ps23` + `vs0..vs1`) | The IV postfx chain. `ps11/13/15/17` = composites (readable). Everything else (bright-pass, tonemap, blur, HDR resolve, stipple) is blob-only. |
| `rage_postfx_e1.fxc`, `rage_postfx_e2.fxc` | 1 + 1 | Shader libraries: EFLC (TLAD/TBoGT) postfx packs. TLAD grain composites are in `e1` (readable, see §1.1). `e2` is entirely unreadable. |
| `water/` | 3 (`ps0`,`ps1`,`vs0`) + `water_ps1` readable | `water_ps1` = the shoreline variant. |
| `water_e2.fxc` | 1 | Water shader library (EFLC). Blob only. |
| `shadowz/`, `shadowzdir/`, `shadowsmartblit/` | 3 + 2 + 11 | Shadow-map depth / directional / blit shaders. Blob only. |
| `mirror/` | 2 | Rear-view mirror. Blob only. |
| `gta_*` material families | ~1,240 | Every material permutation: `gta_vehicle_*` (paint1/2/3, chrome, rims1/2, tire, glass, badges, lightsemissive, interior, shuts, disc, mesh, basic, generic), `gta_normal_spec_reflect[_decal]`, `gta_parallax[_steep][_specmap]`, `gta_glass_*`, `gta_ped_skin[_blendshape]`, `gta_terrain_va_2/3/4lyr`, `gta_trees`, `gta_grass`, `gta_hair_*`, `gta_rmptfx_*`, `gta_atmoscatt_clouds`, `gta_radar`, `gta_im`, plus `rage_default`, `rage_billboard_nobump`. |

**Consequence:** the readable surface is exactly 17 files out of 1,356 entries.
Every other IV shader stays opaque. Anything we build beyond these 17 is a
reconstruction, not a copy — mark it as such in code comments.

### 1.3 The override contract (`shader_override_cache.h`)

`LR/shader_override_cache.h:12-28` defines the runtime entry:

```cpp
struct ShaderOverrideCacheEntry {
  uint64_t hash; uint32_t stage; uint32_t specialization_constants_mask;
  uint32_t activation; uint32_t pipeline_pair_id; uint32_t used_texture_mask;
  uint32_t support_radius_bits; uint32_t supported_sample_count_mask;
  const uint64_t* counterpart_hashes; size_t counterpart_hash_count;
  const uint32_t* spirv; size_t spirv_size;
  const uint32_t* late_spirv; size_t late_spirv_size;
  const char* filename;
};
```

Two facts matter to us (`LR/manifest.json:136-147`, `:185-196`):

- The deferred-lighting pair is `"activation": "pipeline_pair"` with
  `pipeline_pair_id: 1` and **mutual** `counterpart_hashes`. The host refuses to
  install one without the other — **the light-volume VS and PS must ship
  together**, because the VS writes the interpolators the PS integrates
  (`TEXCOORD0.xyz = camPos - lightPos`, `TEXCOORD1.xyz = worldPos - camPos`,
  `TEXCOORD2.xyz = intensity*colour`).
- `support_radius_bits: "0x3F59999A"` (= 0.85f) is passed as a *compile-time
  define* to both stages, and `supported_sample_counts: [1,2,4]` shows the pass
  is MSAA-aware (`SV_Coverage` + `ComputeXenosAlphaToMask`,
  `LR/light_volume/deferred_lighting_ps20_smooth.hlsl:168-212`). We are
  single-sample; the sample-mask machinery drops out entirely.

---

## 2. Function-level breakdowns

Every large file is structured the same way:

| Lines | Content |
|-------|---------|
| 1-~780 | An inlined copy of `LR/xenos_shader_common.h` (verified by diff: only the spec-constant `#define`s differ) |
| ~780-905 | Per-file `cbuffer` declarations / Vulkan constant-index `#define`s + shader-specific `#define`s |
| ~910-970 | `struct Interpolators` (32 fields in, 18 out) / `PixelShaderOutput` |
| ~970-end | `shaderMain` — a decompiled, register-named (`r0..r31`, `ps`, `p0`, `a0`, `aL`) transcription of the Xbox microcode |

So **the code is not a hand-written effect**; it is a register-level
transcription with Xbox instruction semantics preserved 1:1 (including the
`max(x, x)` NaN-clear idiom and `clamp(rcp(x), -FLT_MAX, FLT_MAX)`). The
`while(true) switch(pc)` in the deferred VS (`LR/light_volume/deferred_lighting_vs7_smooth.hlsl:945-1132`)
is the decompiler's flattening of the original branch graph, not a GPU loop.

---

### 2.1 `xenos_shader_common.h` (726 lines) — shared ABI + helpers

**Verdict: SKIP as a dependency; PORT-WITH-CHANGES for 4 helpers.**

**A. Function breakdown**

| Function | Lines | Algorithm | Inputs | Output |
|---|---|---|---|---|
| `float4 cube(float4)` | 165-203 | Xbox cube-map address encoding: pick major axis of `value.zwx`, emit `(tc, sc, 2*major, faceId)` | face-space coord | Xbox CUBE operand format |
| `float3 cubeDir(float3)` | 205-241 | Inverse of the above; `face = uint(clamp(texCoord.z,0,5))`, `axis = face>>1`, `neg = face&1`, switch | Xbox cube coord | unit-ish direction |
| `tfetch2D/2DArray/3D/Cube` | 405-449 (+ Metal 290-334) | `texture.SampleBias(texCoord + offset/dimensions, lodBias)`; VS path uses `SampleLevel` | heap index, sampler index | float4 |
| `getWeights2D/2DArray/3D` | 451-468 | `frac(texCoord * dims + offset - 0.5)`, NaN→0 | — | fractional texel weights |
| `w0,w1,w2,w3` | 510-528 | Cubic B-spline basis: `w0=(1/6)(a(a(-a+3)-3)+1)`, `w1=(1/6)(a*a*(3a-6)+4)`, `w2=(1/6)(a(a(-3a+3)+3)+1)`, `w3=(1/6)a³` | `a` | weight |
| `g0,g1,h0,h1` | 530-548 | `g0=w0+w1`, `g1=w2+w3`; `h0=-1+w1/(w0+w1)+0.5`, `h1=1+w3/(w2+w3)+0.5` | `a` | tap offset / weight |
| `tfetch2DBicubic` | 590-620 | **4 taps**, cast as `px=floor(x-0.5)`, `fx=x-px`, weights `g0(fy)*(g0(fx)*T00 + g1(fx)*T10) + g1(fy)*(...)` | tex + texel offset | float4 |
| `tfetchR11G11B10` | 624-642 | Bit-unpack of an 11/11/10 normal (sign bits `0x400`, `0x200000`, `0x80000000`) | `uint4` | decoded normal |
| `computeMipLevel` | 683-694 | `max(0, 0.5*log2(max(dot(ddx(p),ddx(p)), dot(ddy(p),ddy(p)))))` | pixel coord | mip |
| `dst`, `max4`, `swapFloats` | 644-662 | Xbox D3D9 legacy `dst`; `max4 = max(max(x,y),max(z,w))`; conditional `.yxwz` swap | — | — |
| `atomicLoadUint` / `atomicFetchAddUint` | 698-720 | SM5 atomics | — | — |

**B. Portability**

- `tfetch*`, `getWeights*`, `atomic*`, all `Texture2DArray`/`Texture3D`/`TextureCube`
  heap access, `[[vk::…]]`, `PushConstants`, `asfloat(uint4(...))` → **SM5/Vulkan only. SKIP.**
- `w0..h1`, `tfetch2DBicubic` (4 taps, `ps_3_0` legal), `computeMipLevel`
  (`ddx`/`ddy` are `ps_3_0`-legal), `dst`, `max4`, `cubeDir` → **portable**.
- `tfetchR11G11B10` uses `&`, `>>`, `?:` on `uint` → **not `ps_3_0`** (`X3535`
  bitwise ops on legacy targets). Do not port; our normal G-buffer is A8R8G8B8.

**C. Port plan**

- `shaders/include/CommonMath.hlsl` — add `Bicubic4Tap(sampler2D, float2 uv, float2 texelOffset)`
  and `ComputeMipLevel(float2)`. 4 `tex2Dlod` taps, ~24 instr. No new registers.
- `dst`/`max4` already exist in equivalent form in `CommonMath.hlsl` — do not duplicate.
- INI gate: none (helper only).

---

### 2.2 `shader_override_cache.h` (31 lines)

**Verdict: SKIP.** Pure host ABI (`LR/shader_override_cache.h:6-10` stage/activation
enums, `:12-28` entry struct). No shader code. Its only actionable content is
the pair-activation contract summarised in §1.3.

---

### 2.3 `bloom/rage_postfx_scalable_bloom.hlsl` (132 lines) — `rage_postfx_ps8/ps9`

**Verdict: PORT. Highest value-to-risk ratio in the set.**

**A. Function breakdown**

| Function | Lines | Details |
|---|---|---|
| `AlphaTestPass(alpha, ref, fn)` | 13-23 | 8-way compare; unused by us |
| `SampleBloom(tex, samp, uv)` | 62-66 | `texture.SampleBias(bloom_sampler, uv, lod_bias).rgb` — **one tap**, lod bias from `SharedConstants + 752` |
| `shaderMain` | 71-131 | Two unrolled loops, 25 unique taps (see below) |

**Constants (verbatim):**

```hlsl
static const float kBloomUvScale = 0.0018518;                    // :46
static const float kBroadCenterWeight = 0.09290778895898182;     // :47
static const float kBroadWeights[12] = {                         // :48-55
    0.09043987558157364, 0.08342331057487526,
    0.07291734780749201, 0.06039368393189297,
    0.04739913868089060, 0.03525066121688894,
    0.024841674758290057, 0.016588619577573006,
    0.010496752439822546, 0.006293833998248673,
    0.003575956393107322, 0.0019252505598539797, };
static const float kCenterWeights[5] = {                          // :56-60
    0.2932482044098259, 0.22394306564588196,
    0.09969501558123543, 0.025843481100711958,
    0.003894335467257663, };
```

Weight sums (computed): centre kernel `0.6466241`, broad kernel `0.4535461`,
total **1.1001702** → the filter deliberately amplifies energy by 10 %.

**Tap geometry (verbatim):**

```hlsl
const float2 axis_texel =
    global_screen_size.yy * global_screen_size.zw * kBloomUvScale;   // :84-85
#if defined(BLOOM_HORIZONTAL)
  const float2 axis = float2(axis_texel.x, 0.0);                     // :88
#elif defined(BLOOM_VERTICAL)
  const float2 axis = float2(0.0, axis_texel.y);                     // :90
#endif
```

`globalScreenSize = (w, h, 1/w, 1/h)` (`:79-83`), so
`axis_texel = (h/w * 0.0018518, 1 * 0.0018518)` → **step = h/540 pixels on both
axes**, i.e. 1.333 px at 720p and 2.0 px at 1080p, radius = 12 steps = **h/45 px
(2.22 % of screen height)**. Resolution-independent by construction.

**Algorithm (exact):**

```hlsl
float3 center_blur = center * kCenterWeights[0];                    // :100
[unroll] for (uint radius = 1; radius < 5; ++radius) {              // :101-108
  const float2 offset = axis * float(radius);
  center_blur += (SampleBloom(tex,bloom_sampler,uv+offset)
                + SampleBloom(tex,bloom_sampler,uv-offset)) * kCenterWeights[radius]; }

float3 broad_detail = max(center - center_blur, 0.0) * kBroadCenterWeight;  // :110
[unroll] for (uint radius = 1; radius <= 12; ++radius) {            // :111-119
  const float2 offset = axis * float(radius);
  const float3 positive = max(SampleBloom(...uv+offset) - center_blur, 0.0);
  const float3 negative = max(SampleBloom(...uv-offset) - center_blur, 0.0);
  broad_detail += (positive + negative) * kBroadWeights[radius - 1]; }

output.oC0 = float4(center_blur + broad_detail, 1.0);               // :122
```

**There is no luminance threshold and no knee.** This is an unsharp-mask-shaped
bloom: a narrow 5-tap Gaussian (`center_blur`) subtracted from a 12-radius
bilateral-ish ring, with only positive residuals kept. It is *detail*
extraction, not bright-pass extraction. Our `Bloom_Extract.hlsl` is the
bright-pass; these two are complementary.

**Inputs / outputs:** `s0` = bloom source (already bright-passed), 1 sampler;
`c` = `globalScreenSize` only. Output RGB = blurred bloom, **alpha 1.0**.

**B. Portability: PORT**

Nothing in the body is SM4+: fixed `[unroll]` bounds, `tex2D`/`tex2Dbias`,
scalar/vector float math. Required changes: replace `Texture2D`/`SamplerState`
with `sampler2D`, `SampleBias(...,bias)` with `tex2Dbias`, `vk::RawBufferLoad`
with `register(cN)`, drop the alpha-test tail (`:123-130`).

**C. Port plan**

- **New file `shaders/ps/Bloom_IVBlur.hlsl`** (CSO id **`281`**), ONE entry, axis chosen at C++
  bind time (no `#define` split) — keeps one CSO. Register `c0 = (stepX, stepY, 0, 0)`.
  - `s0` = bloom mip — **no collision** (the bloom lane already binds `s0`).
  - `c0` — **no collision**: this is a standalone full-screen pass, and
    `RwD3D9SetPixelShaderConstant(0, …)` is uploaded immediately before the draw
    (same convention as `Bloom_Downsample`'s `c0`). Only one constant is needed.
- **Taps: 25** (not 33) — FXC reuses the four centre-blur radii as the first
  four broad radii because the offsets are identical. **Measured: 124 `ps_3_0`
  instruction slots (25 texture, 99 arithmetic)** (Appendix A).
- Runs at mip0 of the existing pyramid, replacing `Bloom_Upsample`'s tent when
  the IV mode is on.
- INI: **`bloomIVScalable=1`** (default 0), plus reuse `bloomThreshold` from
  the extract pass. Proposed values: `bloomThreshold=1.0`, `bloomIntensity=1.1`
  to match the kernel's 1.1× energy.

---

### 2.4 `light_volume/deferred_lighting_ps20_smooth.hlsl` (1058 lines) — `deferred_lighting_ps20`

**Verdict: PORT-WITH-CHANGES.** This is the cheapest real IV algorithm in the set.

**A. Function breakdown**

| Function | Lines | Details |
|---|---|---|
| `ComputeXenosAlphaToMask` | 168-212 | MSAA coverage emulation (`sampleCount` 1/2/4), `_spirv_` only — **delete** |
| `tfetch2D` … `atomic*` | 218-775 | inlined common ABI — **delete** |
| `shaderMain` | 890-1057 | 1 texture fetch, ~30 float ops |

**Constants** (`:805-810`):

```hlsl
float4 gDeferredLightRadius       : packoffset(c208);   // .x = authored radius (m)
float4 gDeferredProjParams        : packoffset(c210);   // .z, .w = depth linearisation
float4 gooDeferredLightScreenSize : packoffset(c209);   // .zw = 1/w, 1/h
```

**Inputs:** `s0` = `GBufferTextureSampler3` (**depth only**, `used_texture_mask
0x00000001`); interpolators `TEXCOORD0..2` written by the paired VS;
`SV_IsFrontFace` (`iFace`).

**Algorithm (verbatim, `:977-1038`):**

```hlsl
r3.xy = (abs(r3.xy) + c254.ww);                 // .51 = half-pixel + 0.01 guard
r3.xy = (r3.xy * gooDeferredLightScreenSize.zw);
r1.w  = tfetch2D(GBufferTextureSampler3, ..., r3.xy, ...).x;   // scene depth
r1.w  = (r1.w * gDeferredProjParams.z + gDeferredProjParams.w); // -> eye distance
...
const float authoredRadius = max(gDeferredLightRadius.x, gDeferredLightRadius.x);
const float supportRadiusScale = asfloat(GTA4_LIGHT_VOLUME_SUPPORT_BITS); // 0.85
const float smoothBoundary = authoredRadius * supportRadiusScale;
const float normalizedDistance =
    saturate((smoothBoundary - r1.x) / smoothBoundary);
const float smoothFalloff = normalizedDistance * normalizedDistance *
    (3.0 - 2.0 * normalizedDistance);                   // smoothstep
r1.y = authoredRadius * c255.x;                          // c255.x = 0.66
r0.z = r1.y * r1.y;                                      // r²
r1.x = smoothFalloff * r1.y;
r0.yw = max(r3.xy, -r1.xx);                              // clamp both ray/sphere
r0.yw = min(r1.xx, r0.yw);                               //   hits to [-r, +r]
r0.x  = r0.w - r0.y;                                     // chord length
r0.x  = ((0.33333 * r0.x) * r0.x) * rcp(r0.z);           // chord² / (3 r²)
output.oC0.xyz = (r0.xyz * c254.xxx);                    // ×3  ->  chord²/r²
output.oC0.w = 0.0;
```

So **the entire pixel shader is the analytic line integral of a sphere**:
`out = lightColour * (chord² / r²)`, where `chord` is the entry→exit distance of
the view ray inside the light sphere clamped to `±0.66·radius`, and `lightColour`
arrives pre-multiplied by `intensity × volumeParams × authoredColour` from the VS.
The `0.66` is GTA IV's authored volume scale (`:1024`, `c255.x`); `0.85` is
FusionShaders' conservative boundary for the smoothstep.

**What lighting terms does IV sum here?** **One:** the volume integral. There is
no albedo, no N·L, no specular, no ambient. This confirms (rather than
contradicts) the parity audit: the G-buffer shading is elsewhere and opaque.
Practical consequence for us: this is a **light-volume / volumetric-add pass**,
usable with our forward+ lights as an extra additive pass, not a deferred
renderer.

**B. Portability: PORT-WITH-CHANGES**

| Blocker | Change |
|---|---|
| `Texture2D<float4>[] g_Texture2DDescriptorHeap` + `SamplerState` | `sampler2D gbufDepth : register(s0)` |
| `vk::RawBufferLoad` constant fetches | `register(cN)` uniforms |
| `uint`/bitwise `g_SpecConstants & SPEC_CONSTANT_ALPHA_TEST` | delete the alpha-test tail (`:1040-1053`); we set alpha test from C++ |
| `SV_Coverage` / `ComputeXenosAlphaToMask` | delete (single-sample) |
| `asfloat(uint4(...))` literals | plain float literals |
| `iFace ? 1.0 : -1.0` | `VFACE` — supported in `ps_3_0` |

**C. Port plan**

- **New file `shaders/ps/GTAIVLightVolumePS.hlsl`** (entry `main`), new CSO id
  **`282`** (`280` is already reserved for W8-3 per `resources/resource.h:135`;
  the highest defined id today is `IDR_TREEALPHA 279`).
- Registers: `s0` = scene depth (reuse `g_ssaoDepthTex`, already bound in the
  postfx lane); `c0` = `(radius, 0, depthScale, depthBias)`, `c1` = `(1/w, 1/h,
  0, 0)`, `c2` = `(r, g, b, intensity)` — **`c0-c2`, standalone draw, no collision**
  (see the register rules in §5.2).
- **1 tap; measured 34 `ps_3_0` instructions** (Appendix A).
- Additive blend `ONE/ONE`, depth test off, drawn as the light proxy sphere/cone
  geometry produced by the VS. Requires the paired VS or an equivalent that
  emits `(camPos-lightPos, worldPos-camPos, colour*intensity)`.
- INI: **`ivLightVolume=1`** (default 0), **`ivLightVolumeScale=0.66`**,
  **`ivLightVolumeSmooth=0.85`**, **`ivLightVolumeIntensity=1.0`**.
- Gating note: this cannot be used before our lighting has per-light proxy
  geometry. `forwardplus.cpp` already owns per-light radius/cone data
  (`src/render/forwardplus.cpp:493-550`), so the VS can be fed from
  `CLight`/`CPointLight` instead of the IV `gDeferredLight*` block.

---

### 2.5 `light_volume/deferred_lighting_vs7_smooth.hlsl` (1134 lines) — `deferred_lighting_vs7`

**Verdict: PORT-WITH-CHANGES** (only if §2.4 ships; the pair contract requires it).

**A. Function breakdown**

`shaderMain` = one entry (`:848-1133`). The body is a `while(true) switch(pc)`
state machine (`:945-1132`) — 20 "cases", i.e. the original branch graph.

**Constants** (`:762-777`):

```hlsl
float4 gDeferredLightType               : packoffset(c208);  // .x = type id
float4 gDeferredLightPosition           : packoffset(c209);
float4 gDeferredLightDirection          : packoffset(c210);
float4 gDeferredLightTangent            : packoffset(c211);
float4 gDeferredLightRadius             : packoffset(c212);
float4 gDeferredVolumeRadiusScale       : packoffset(c213);
float4 gDeferredLightConeAngleI         : packoffset(c214);
float4 gDeferredLightConeAngle          : packoffset(c215);
float4 gDeferredLightColourAndIntensity : packoffset(c216);
float4 gDeferredLightVolumeParams       : packoffset(c217);
float4 gViewInverse[4]                  : packoffset(c12);
float4 gWorldViewProj[4]                : packoffset(c8);
```

**Algorithm:** builds the light proxy vertex from `POSITION0` only (`:792`) —
no vertex buffer attributes beyond position; the *shape* is synthesised from
the light type:

- **Type `0x40800000` (4.0) — spot/cone** (`:965-1006`): normalise the input
  position, build a basis from `gDeferredLightDirection × gDeferredLightTangent`,
  rotate the vertex by the `frac`-animated cone angle, scale by
  `gDeferredVolumeRadiusScale.x`:
  ```hlsl
  r0.x = frac(gDeferredLightConeAngle.x * (1 - saturate(max(-r1.z,-r1.z)))
              * c255.w + 0.5);                       // :980-983
  r0.z = r0.x * 6.2831855 + (-3.1415927);            // :986  (-π .. +π)
  r0.y = (1 - cos(r0.z)*cos(r0.z)) * sqrt(...);      // :989-995
  ```
- **Type `0x3F800000` (1.0) — sphere/capsule** (`:1007-1011`):
  `r0.xyz = r1.xyz * rsqrt(r1.z*r1.z + dot(r1.xy,r1.xy))` (normalise).
- **Default — cone with `gDeferredLightConeAngleI`** (`:1015-1107`): two
  orthogonal cone samples with `frac`-animated angles and a `p0` swap.

**Outputs** (`:1108-1127`) — the contract the PS depends on:

```hlsl
r1.x = gDeferredLightColourAndIntensity.w * gDeferredLightVolumeParams.x;   // intensity
r1.y = gDeferredLightRadius.x * gDeferredVolumeRadiusScale.x;              // radius
r1.xy = r1.xy * c255.xy;                    // c255 = (0.25, 0.85, 4.0, 0.15915)
r2.xyz = r1.yyy * r0.xyz + gDeferredLightPosition.xyz;                     // world pos
output.oTexCoord1.xyz = r2.xyz - gViewInverse(3).xyz;                      // vs camera
output.oTexCoord2.xyz = r1.xxx * gDeferredLightColourAndIntensity.xyz;     // colour·I
output.oTexCoord0.xyz = gViewInverse(3).xyz - gDeferredLightPosition.xyz;  // light→cam
output.oTexCoord0.w   = max(r0.w, r0.w);                                   // clip w
output.oPos = max(mvp*r2, mvp*r2);          // NaN-clear idiom (Xbox `max a,a`)
output.oPos.xy += g_HalfPixelOffset * output.oPos.w;                       // :1127
```

`c255.y = 0.85` is the *same* `GTA4_LIGHT_VOLUME_SUPPORT_BITS` value as the PS
(constructed at `:883-886` with the comment "The generated pair contract
supplies this exact support radius to both stages").

**B. Portability: PORT-WITH-CHANGES**

- `[[vk::location(0)]]`, `precise`, `SV_ClipDistance`, 18 interpolators — SM3.0
  supports up to 12 output interpolators + POSITION. **Count: the VS actually
  writes `oTexCoord0..2` (+ `oTexCoord0.w`) only** (`:1108-1127`); the other 13
  are dead zero-inits (`:889-907`). Port writes 3 (TEXCOORD0-2) + POSITION → fits.
- `switch(pc)` state machine → **flatten to `if/else` on `gDeferredLightType.x`**.
- `selectWrapper(bool4, ...)` = componentwise ternary → `cmp`, legal in `ps_3_0`/`vs_3_0`.
- `frac` on a scalar, `cos`, `rsqrt` — all `vs_3_0`.

**C. Port plan**

- **New file `shaders/vs/GTAIVLightVolumeVS.hlsl`**, entry `main`, CSO id **`283`**.
- Registers: `c0-c3` = world-view-proj, `c4` = `(type, radius, 0, 0)`,
  `c5` = light pos, `c6` = light dir, `c7` = tangent, `c8` = `(coneAngle,
  coneAngleI, volumeScale, intensity)`, `c9` = colour, `c10` = camera pos,
  `c11` = `(0.25, 0.85, 4.0, 1/2π)`. **`c0-c11`, standalone draw, no collision**
  (this is a new VS, not the PBR VS that already uses `c0-c44`).
- ~120-140 `vs_3_0` slots after flattening (branchy, not measured).
- INI: same keys as §2.4 (`ivLightVolume*`).
- Alternative: if we feed it from `forwardplus` lights, the type enum can be
  simplified to sphere-only (`c4.x > 0`), removing the whole cone state machine
  and ~90 slots.

---

### 2.6 `fog/rage_postfx_exponential_height_fog.hlsl` (166 lines) — `rage_postfx_ps19`

**Verdict: PORT-WITH-CHANGES.** The single most interesting *math* in the set after the grain.

**A. Function breakdown**

| Function | Lines | Details |
|---|---|---|
| `AlphaTestPass` | 10-20 | unused by us |
| `shaderMain` | 48-165 | 2 taps, ~63 ALU |

**Inputs:**

| Slot | Name | Source |
|---|---|---|
| `s0` | depth | `SharedConstants + 0x000` texture idx, `+0x1A0` sampler |
| `s1` | HDR colour | `SharedConstants + 0x004` |
| `c` | `dofProj` `(near, far, ?, ?)` `+0xD00` | |
| `c` | `gDepthFxParams` `(a,b,c,d)` `+0x100` | exponent/scale pair for the stock fog |
| `c` | `globalFogParams` `(near, far, z, w)` `+0x290` | |
| `c` | `globalFogColor`, `globalFogColorN` `+0x2A0`, `+0x2B0` | far / near fog colours |
| `c` | `fog` `(density, falloff, satTerm, exponent)` `+0x260` | height-fog params |
| `c` | camera altitude `+0x270.z` | |
| interp | `TEXCOORD2.xyz` = **normalised world view ray** | written by `rage_postfx_fog_vs.hlsl:62-63` |

**Algorithm — part 1, stock depth/luminance fog (`:87-127`, verbatim):**

```hlsl
r3.xyz = hdr.SampleLevel(hdr_sampler, r0.xy, ...).xyz;   // scene
r0.w   = depth.SampleLevel(depth_sampler, r0.xy, ...).x; // depth
r0.z   = dofProj.y * dofProj.x;                          // near*far
r1.yz  = r0.ww * dofProj.yx;                             // depth*far, depth*near
r0.y   = r1.y - r1.z;                                    // depth*(far-near)
r0.y  += dofProj.x;                                      // + near
r0.y   = rcp(r0.y);
r0.y   = r0.z * r0.y;                                    // near*far / (near + d*(far-near))
const float fog_distance = abs(r0.y);
r3.w   = dot(float4(0.0722, 0.2125, 0.7154, 1.0e-7), r3.zxyw);  // Rec.709 luma
r2.w   = dot(r3.zxy, float3(0.0722, 0.2125, 0.7154));
r1.xyw = saturate(r0.yxz * r1.wxy);                      // dist/near, (far-dist)/T, (dist-near)/(far-near)
r2.xyz = r1.www * r6.yzw + globalFogColorN.xyz;          // lerp(nearColour, farColour, 0..1)
r3.xyz -= r2.www;                                        // chroma = colour - luma
r0.xw  = r5.xy * r5.zz;
r0.xy  = r0.xx * r4.xy;                                  // gDepthFxParams.xy - 1 = exponents
r1.x   = log2(abs(r3.w));
r0.y  *= r1.x;
r1.x   = r0.x + 1.0;
r0.y   = exp2(r0.y);
r1.xyz = (r3.xyz * r1.xxx + r2.www) * r0.yyy;            // (chroma*(1+a) + luma) * luma^b
r3.xyz = globalFogColorN.xyz - r1.xyz;
r0.xyz = r0.zzz * r3.xyz + r1.xyz;
```

Two distinct stages are visible: a **linear near/far colour ramp**
(`r2.xyz`) and a **luminance-preserving exponential** where the fogged colour is
scaled by `luma^(fogAmount)` while the chroma is scaled by `(1 + fogAmount)`.
That is what gives IV its "fog brightens with sky luminance" look — the fog is
**not** a constant colour blend.

**Algorithm — part 2, height-fog override (`:129-150`, verbatim):**

```hlsl
const uint valid = vk::RawBufferLoad<uint>(... + 0x2D0);
const uint required = 0x0000B0F0u;
if ((valid & required) == required && dot(input.iTexCoord2.xyz, input.iTexCoord2.xyz) > 0.5) {
  const float4 fog = ...;                                   // (density, falloff, sat, exponent)
  const float camera_altitude = (... + 0x270).z;
  const float3 view_direction = normalize(input.iTexCoord2.xyz);
  float slope = view_direction.z * max(fog.y, 1.0e-7);
  if (abs(slope) < 1.0e-6) { slope = 1.0e-6; }
  const float altitude_density =
      exp2(-camera_altitude * max(fog.y, 1.0e-7) * saturate(fog.z));
  const float line_integral =
      (1.0 - exp2(-slope * fog_distance)) / slope;
  const float optical_depth =
      max(fog.x, 0.0) * altitude_density * max(line_integral, 0.0);
  r0.w = pow(saturate(1.0 - exp2(-optical_depth)), max(fog.w, 0.0));
}
output.oC0.xyz = lerp(r0.xyz, r2.xyz, r0.w);                 // :153
```

The **height-fog integration** is the closed-form line integral of an
exponential atmosphere along a straight ray:

```
T(d)   = exp2(-cameraAltitude * falloff * satTerm)          // density at the camera
I(d)   = (1 - exp2(-slope*d)) / slope,  slope = viewDir.z * falloff
tau(d) = density * T * I(d)
fog    = (1 - exp2(-tau)) ^ exponent
```

This is exactly the analytic form of `1 - exp(-∫ density·exp(-falloff·h(s)) ds)`.
**`slope → 0` is handled by clamping to 1e-6, not by a series expansion** — that
is a real (small) discontinuity at a perfectly horizontal view; ours must keep
the same guard.

**B. Portability: PORT-WITH-CHANGES**

| Blocker | Change |
|---|---|
| `Texture2D`/`SamplerState`/`SampleLevel` | `tex2D` (our fog pass is full-screen, LOD irrelevant) |
| `uint valid & required == required` | delete; replace with a C++ "fog params valid" bool uniform |
| `vk::RawBufferLoad` | `register(cN)` |
| `pow(saturate(...), exponent)` | fine (`ps_3_0` has `pow`) |
| `input.iTexCoord2.xyz` world ray | **not needed** — our `DrawHeightFog` already reconstructs `viewPos` from `linearDepth` (`shaders/ps/HeightFog.hlsl:62-73`), so `normalize(viewPos)` gives the same ray. Saves the extra interpolator AND the companion VS. |

**C. Port plan**

- **Modify `shaders/ps/HeightFog.hlsl`** (existing `IDR_HEIGHTFOG 250`,
  `resources/resource.h:99`; drawn at `src/render/postfx.cpp:3874-4130`).
  Add an `ivFogMode` branch: when 1, replace the Crytek `heightFactor*distFactor`
  with the IV line integral, and replace the constant-colour `lerp` with the
  near/far-ramp + luminance-exponential of part 1.
- Registers: `c0-c3` are already used (`fogParams`, `fogColor`, `projInfo`,
  `fbParams`); `c4`/`c5` already carry `camPos`/`camAxisZ`. New: **`c6` =
  `(density, falloff, satTerm, exponent)`**, **`c7` = `(nearColour.rgb, nearClip)`**,
  **`c8` = `(farColour.rgb, farClip)`** — pass-local and collision-free (`c0-c5`
  keep their current meaning, so the Crytek path keeps working unchanged).
- 2 taps, **measured 65 `ps_3_0` slots** (2 texture, 63 arithmetic) for the full
  stock chain + height override (Appendix A). Adding it to `HeightFog.hlsl`
  keeps the pass under ~110 slots.
- INI: **`heightFogIVModel=1`** (default 0), **`heightFogIVFalloff`**,
  **`heightFogIVExponent`**, **`heightFogIVNearColor`** / **`heightFogIVFarColor`**
  (or reuse the existing `fogR/fogG/fogB` as far colour and derive near from it).
- Do **not** port `rage_postfx_fog_vs.hlsl`. §2.7.

---

### 2.7 `fog/rage_postfx_fog_vs.hlsl` (77 lines) — `rage_postfx_vs0`

**Verdict: PORT-WITH-CHANGES, but not needed for us.** (Kept only if the full
`PS_GTADepthEffects` chain is reproduced verbatim; our fog pass has the depth
buffer and can derive the ray.)

**A. Function breakdown** — one entry `shaderMain` (`:40-76`). 1 attribute
(`POSITION0`, `:792`-style) + `TEXCOORD0` at `[[vk::location(13)]]` (`:14`).

**Algorithm (`:40-76`, verbatim):**

```hlsl
output.oPos = float4(input.iPosition0.xyz, 1.0);
output.oTexCoord0.xy = input.iTexCoord0.xy;
output.oTexCoord1.xy = -abs(input.iTexCoord0.xx) > 0.0;      // :44  always 0
...
const float4x4 view_inverse = float4x4(load(+0x280), load(+0x290),
                                      load(+0x2A0), load(+0x2B0));   // :50-54
const float2 projection_scale = load(+0x2C0);
const float2 safe_scale = max(abs(projection_scale), 1.0e-6);
float2 projected = input.iTexCoord0.xy * 2.0 - 1.0;
projected.y = -projected.y;
projected /= safe_scale;
const float3 view_ray = float3(-projected, 1.0);
output.oTexCoord2.xyz = normalize(mul(float4(view_ray, 0.0), view_inverse).xyz);  // :62-63
...
output.oPos.xy += half_pixel * output.oPos.w;                        // :75
```

`oTexCoord1.xy` is a decompiler artifact: `-abs(x) > 0` is never true, so the
field is always `0.0`. **Do not reproduce it.**

**B. Portability:** `float4x4(...)` constructed from raw loads, `mul`, `normalize`,
`SV_ClipDistance` (SM4 name; `vs_3_0` uses `oClip0`/`CLPLANE`) — all fixable.
The `precise` keyword is SM5; drop it (we do not need bit-exact reprojection).

**C. Port plan:** none. `HeightFog.hlsl` already has `camPos`/`camAxisZ`/`viewPos`
(`:62-73`) and can build `normalize(viewPos)` for free. If we later port the
verbatim `PS_GTADepthEffects` chain we would need `c49` = `(viewInverse 4×4)` —
16 constants — which is wasteful for one ray.

---

### 2.8 `water/water_ps1_smooth_shorelines.hlsl` (1453 lines) — `water_ps1`

**Verdict: SKIP (container) / PORT-WITH-CHANGES (2 deltas — already landed).**

**A. Function breakdown**

| Function | Lines | Details |
|---|---|---|
| common ABI copy | 1-742 | `cube`, `cubeDir`, `tfetch*`, `getWeights*`, `w0..h1`, `tfetch2DBicubic`, `tfetchR11G11B10`, `computeMipLevel`, atomics |
| constant macros (`__spirv__`/`__air__`/cbuffer) | 744-879 | 30 PS constants: `bottomSkyColour +72`, `gDirectionalColour c18`, `gDirectionalLight c17`, `gInvColorExpBias c46`, `gLightAmbient0 c37`, `gLightAmbient1 c38`, `gShadowMatrix[4] c60`, `gShadowParam0123 c57`, `globalFogColor c42`, `globalScalars c39`, `globalScreenSize c44`, `viewProj c74`, `waterColour c66`, `waterReflectionScale c73` |
| texture slots | 744-778 | Surface, Reflect, DepthBuffer, NormBuffer, `gShadowZSamplerDir` |
| `shaderMain` | 944-1453 | ~250 ALU, 14 taps |

**The full container (what it does, with tap counts):**

1. **Foam / surface detail** — 4 taps of `SurfaceTextureSampler` at
   `r2.xyxy*c248.xxyy`, `r2.xyxy*c246.zzww`, `r6.zw`, `r6.xy`
   (`:1108-1133`), plus the 3 authored octaves (`:1138-1170`):
   ```hlsl
   float2 smoothShoreFoamUv = r5.xy * 2.0;                       // :1138
   ... r5.zw += smoothShoreFoamOctave * 0.5;                     // :1148
   smoothShoreFoamUv *= 2.0; ... r5.zw += smoothShoreFoamOctave * 0.25;   // :1149,1159
   smoothShoreFoamUv *= 2.0; ... r5.zw += smoothShoreFoamOctave * 0.125;  // :1160,1170
   r5.zw = saturate(r5.zw - 0.4375);                             // :1171
   ```
2. **Scene thickness / depth reconstruction** — 1 depth tap (`:1242-1247`),
   shadow-space transform via `gShadowMatrix(0..3)` (`:1202-1206`).
3. **Normal map** — 1 tap (`:1301-1306`).
4. **Shadow PCF** — 4 taps at `gShadowZSamplerDir` (`:1307-1330`), then
   `dot(r15.zwxy, c253.xxxx)` box-filtered visibility (`:1343`).
5. **Reflection** — 1 tap (`:1401-1406`), scaled by `waterReflectionScale.x`.
6. **Fresnel + lighting** — `gDirectionalColour * gDirectionalColour.w`,
   `(ambient1*amt + ambient0)`, `pow(...)`, output
   `oC0.xyz = r1.xyz * globalScalars.yyy`, `oC0.w = gInvColorExpBias.x * r0.y`
   (`:1443-1445`).

**The two authored deltas (the only reason this file exists):**

```hlsl
// (a) exponential shallow-water coverage  — :1420-1425
// r2.x is the stock positive scene-minus-water depth multiplied by
// waterColour.w * 0.25. Scaling by -12 therefore reproduces the Fusion
// exponent of -3 * depth * waterColour.w without disturbing reconstruction.
r0.x = saturate(r1.y + c244.y);
ps = exp2(-12.0 * r2.x);          //  ==> 1 - exp2(-3*depth*waterColour.w)
r0.y = saturate(1.00001001 - ps);
```

```hlsl
// (b) three extra foam octaves at 2x/4x/8x, weights 0.5/0.25/0.125, shifted -0.4375  — :1134-1171
// FusionShaders SmoothShorelines retains the console foam sample above and
// adds three deterministic spatial octaves at 2x, 4x, and 8x frequency.
// The fixed UVs inherit the stock water simulation motion, so no frame-time
// noise or history buffer is introduced.
```

**B. Portability**

| Blocker | Note |
|---|---|
| `gShadowMatrix(INDEX)` with `min(INDEX,163)` (`:839`, `:772`) | constant-index array read; fine if flattened, but it pulls a 4×4 + 164-entry shadow constant block |
| `tfetchR11G11B10` (`:640-658`) | `&`, `>>` bitwise → **not `ps_3_0`**; not needed (bind normal maps as A8R8G8B8) |
| `gShadowParam0123`, `gShadowZSamplerDir` | our shadow maps are not IV-format |
| `GetTextureLodBias(0..15)` | our samplers have no per-slot bias |
| `FLT_MAX` clamps, `selectWrapper`, `int a0/aL` | fine / drop |

**C. Port plan**

- **Nothing to add — both deltas are already in `shaders/ps/Water_Parallax.hlsl`:**
  `WaterShoreCoverage` at `:265-269` implements `saturate(1 - exp2(-2.0*x))`,
  `WaterFoamOctaves` at `:287-295` implements
  `saturate(baseH + o1*0.5 + o2*0.25 + o3*0.125 - 0.4375)` — literally the IV
  formula. Used at `:744` / `:816-827` / `:920` / `:1084` / `:1185-1191`.
- **One real delta we got wrong:** IV ties the ramp to the authored depth scale —
  `1 - exp2(-3 * depth * waterColour.w)` (`:1424`), i.e. the coefficient is
  `3*waterColour.w per metre`. Ours is `2 / waterShoreFade` (`Water_Parallax.hlsl:266-267`),
  a *separate* INI key. Optional follow-up: add
  **`waterShoreExponentFromDepth=1`** so the ramp is driven by the authored
  `waterColour.w` like IV, with `waterShoreFade` as the fallback.
- Do not port the container. `waterPipe.cpp` + `Water_Parallax.hlsl` already
  diverge deliberately (styles `main_iv`/`main_v`, tier system, night gate).

---

### 2.9 Motion-blur composites — 4 files

`gta_composite_mb.hlsl` (1548 ln, `ps11`) · `gta_composite_mb_high_dof.hlsl`
(1551 ln, `ps13`) · `gta_composite_mb_dof_debug.hlsl` (1559 ln, `ps15`) ·
`gta_composite_mb_high_dof_debug.hlsl` (1561 ln, `ps17`)

**Verdicts: `ps11` PORT-WITH-CHANGES · `ps13` PORT-WITH-CHANGES ·
`ps15`/`ps17` SKIP (debug constant layouts only).**

**A. Function breakdown** — one entry `shaderMain` (e.g. `:970-1548`), one helper
`AlphaTestPass`, plus the inlined ABI copy (1-~845).

**Inputs** (`:847-903`):

| Slot | Texture | Used for |
|---|---|---|
| `t0` | `StencilCopySampler` | player-mask / stencil rejection |
| `t1` | `GBufferTextureSampler3` | depth (`.x`) |
| `t2` | `GBufferTextureSampler2` | `.w` = CoC / DoF code |
| `t3` | `HDRSampler` | scene (5 pre-taps + 8 MB taps) |
| `t4` | `BloomSampler` | bloom (added, ×0.25) |
| `t5` | `JitterSampler` | tap-pattern rotation |
| `t6` | `AdapLumSampler` | auto-exposure |

Constants: `ColorCorrect c219, ColorShift c220, Exposure c208, PLAYER_MASK c221,
ToneMapParams c217, deSatContrastGamma c218, dofBlur c211, dofDist c210,
dofProj c209, gDirectionalMotionBlurLength c212, globalScreenSize c44,
motionBlurMatrix[4] c213`; plus the LR-only `LibertyMotionBlurTimeScale`
(`SharedConstants + 592`, `:841`).

**Algorithm, stage by stage:**

**(1) DoF pre-pass: 5 taps at ±1.5 texels** (`:1092-1127`) — and this is the
entire DoF blur, there is no bokeh/wide blur input:

```hlsl
r6.xyz = tfetch2D(HDRSampler, r0.xy, float2(-0.5, -1.5), ...).xyz;   // :1097
r2.yzw = tfetch2D(HDRSampler, r0.xy, float2( 0.0,  0.0), ...).xyz;   // :1103
r7.xyz = tfetch2D(HDRSampler, r0.xy, float2( 1.5, -0.5), ...).xyz;   // :1109
r8.xyz = tfetch2D(HDRSampler, r0.xy, float2( 0.5,  1.5), ...).xyz;   // :1115
r3.xyz = tfetch2D(HDRSampler, r0.xy, float2(-1.5,  0.5), ...).xyz;   // :1121
```

Each tap is reduced to Rec.709 luminance `dot(..., c254.xyz)` with
`c254.xyz = (0.0721, 0.2125, 0.7154)` (`:1130-1134`).

**(2) CoC from the G-buffer + 4-band quantisation** (`:1128-1168`):

```hlsl
r5.xyzw = (float4)((r2.xyzw * c251.xyyy));   // c251 = (256, 0.5, 0.75, -1)  [ps11]
r13.xyzw = (float4)((c255.xyzw >= r5.xxxx)); // c255 = (8, 4, 2, 256)
ps = -r5.x >= 0.0; r0.z = ps;
r12.w = (r0.z + -r13.z);
r12.x = r13.x - r13.w;
r12.y = r13.z - r13.y;
r12.z = r13.y - r13.x;
r0.z  = dot(r12.xzyw, c254.wwww);            // c254.w = -0.5
r1.x  = c252.w + r0.z;                        // c252.w = 0.5
```

`GBuffer2.a × 256` is a 0..255 CoC code compared against `{8,4,2}` and `256`;
the differences build a piecewise-linear weight. **This is a 4-band CoC
quantiser, not a continuous CoC.**

**(3) Motion-blur reprojection** (`:1143-1227`):

```hlsl
r10.xy = r0.yx * c255.zz + c251.ww;      // NDC = uv.yx*2 - 1
r11.xyz = r10.yzz * dofProj.zyx;         // near-plane working value
r11.xyz = (-r5.xxx * motionBlurMatrix(2).zxy + motionBlurMatrix(3).zxy);  // :1179
r11.xyz = (r2.zzz * motionBlurMatrix(1).zxy + r11.xyz);                   // :1180
r11.xyz = (r2.xxx * motionBlurMatrix(0).zxy + r11.xyz);                   // :1185
r9.x = rcp(-r11.x * dofProj.z);
r9.y = rcp( r11.x * dofProj.w);
r9.xyw = r11.yzw * r9.xyw;               // perspective divide -> previous UV
r9.yzw = -r10.yxw + r9.xyz;              // delta = prevUV - curUV
r1.xw = r9.yz * (gDirectionalMotionBlurLength.xx * LibertyMotionBlurTimeScale);  // :1207
```

So the blur vector is `M_prev * (ndc, 1, w) / w - ndc` — **reconstructed from a
4×4 motion-blur matrix, not from a velocity buffer**, then scaled by the
authored length × a host frame-time correction. `ps13` uses `dofBlur.x..y`
instead of `.y..z` (wider band) and switches the HDR scale from `0.375` to
`256.0` (`c252.z`; `ps11` at `:1016-1019` vs `ps13` at `:1021-1024`) — i.e. the
high-dynamic-range variants decode an 8-bit/log encoded HDR buffer by `×256`
before shading and re-encode at the end.

**(4) Jittered tap pattern: 8 HDR taps + 8 stencil taps** (`:1230-1413`):

```hlsl
p0 = r3.w == 0.0;                                    // stencil: skip this pixel?
ps = -c252.w - -r1.y;  r0.w = ps;                    // jitter - 0.5
r5.zw = r4.xy + r4.xy;                               // 2*delta
r5.x  = c251.z * r1.x;                               // 0.75 * len
r1.yz = r4.xy * r0.w + r0.xy;                        // center + jitter*delta
r0.xy = r1.yz + r4.xy;                               // + 1*delta
r13.xyzw = r1.yzyz + r5.xyzw;                        // + 2*delta
r9.xyzw  = r1.xxww * c252.wzzw + r1.yyzz;            // 0.5 / 0.375 taps
r14.xyzw = r1.xwxw * c253.xxyy + r1.yzyz;            // 0.625 / 0.75 taps
```

then **8 `HDRSampler` taps** (`:1290-1341`, at `r14.zw`, `r14.xy`, `r9.xw`,
`r9.yz`, `r13.zw`, `r0.xy`, `r13.xy`, plus the centre) and **8
`StencilCopySampler` taps** (`:1353-1413`) that mask the player out:

```hlsl
r9.xyzw = (r9.xyzw >= PLAYER_MASK.xxxx);        // :1424
r1.yzw = (r6.xyz >= PLAYER_MASK.xxx);           // :1428
r4.x = dot(-r1.zyw, c252.yyy);                  // :1432
r9.xyzw = (-r9.ywxz + c252.yyyy);               // :1444
```

**Total motion-blur taps: 8 HDR + 8 stencil + 1 jitter = 17**, plus the 5 DoF
taps, 1 bloom, 1 depth, 1 GBuffer2, 1 adapt-lum = **25 texture fetches**.

**(5) Blend + full grade** (`:1478-1540`): the blur is mixed by
`mix = 0.25*(1 - coc²)`-shaped weights (`:1217-1219`, `:1220-1221`) and the
result is passed through tone map + desat + colour shift + gamma
(`:1504-1540`) — ours to skip.

**B. Portability: PORT-WITH-CHANGES**

| Blocker | Change |
|---|---|
| 7-entry descriptor heap | 7 concrete `sampler2D` bindings — fine (`ps_3_0` allows 16) |
| `uint` loop counters, `uint4` constants | `int`/float literals |
| `selectWrapper(bool4, a, b)` | `cond ? b : a` → `cmp` (legal) |
| `iFace` stencil side | keep `VFACE` |
| `max(x,x)`, `rcp`, `rsqrt` | legal |
| `LibertyMotionBlurTimeScale` | our own `c` slot |
| the trailing grade | **delete** (we have `TonemapLUT.hlsl`) |

**C. Port plan**

- **New file `shaders/ps/GTAIVCompositeMB.hlsl`** (CSO id **`284`**), one entry per variant is
  unnecessary — one entry, `c` selects the band:
  `c0 = (hdrScale, cocToPx, dofBandLo, dofBandHi)`, `c1 = (mbLength, timeScale, playerMask, jitterScale)`,
  `c2-c5` = `motionBlurMatrix` rows. (`c0-c5`, pass-local: consistent with how
  every other `postfx_newfx` pass uploads, and no collision because nothing else
  is bound in this draw.) `s0` = scene HDR, `s1` = depth,
  `s2` = GBuffer2/CoC, `s3` = stencil copy, `s4` = bloom, `s5` = jitter,
  `s6` = adapt-lum. **`s0-s6` all free in a dedicated full-screen pass; `s9` untouched.**
- **Taps: 17 (MB only, drop the 5 DoF taps and reuse our `DOF_Bokeh`).**
  Estimate ≈ 200-230 `ps_3_0` slots; **not measured** (transcription of
  `:1074-1503` was out of budget for this pass — flagged as the one unverified
  number in this document).
- Recommendation: **port only stage (3)+(4)**, i.e. matrix-reprojected MB with
  jittered 8 taps, and keep our 22-tap `DOF_Bokeh`. Our DoF is strictly better
  (continuous CoC in `DOF_CoC.hlsl:80-99`, 22-tap disc vs their ±1.5 px 5-tap).
- INI: **`motionBlurIVReproject=1`** (default 0), **`motionBlurIVSamples=8`**
  (fixed 8 — do not expose; it is baked into the tap pattern),
  **`motionBlurIVPlayerMask=1`**.
- `ps15`/`ps17`: **SKIP.** Diff vs `ps11`/`ps13` is only the constant block
  (`c251.x` ↔ `c251.y` swapped 0.5/256, `c255` reordered) and register
  allocation; the math is identical (`diff` of the bodies: 187-215 changed lines,
  all constant/register renames). There is no debug *visualisation* — the name
  refers to the game's debug render-target format.

---

### 2.10 Film-grain composites — 4 files

`tlad_composite_noise.hlsl` (1255 ln) · `tlad_composite_mb_noise.hlsl` (1594 ln) ·
`tlad_composite_high_dof_noise.hlsl` (1256 ln) · `tlad_composite_mb_high_dof_noise.hlsl` (1597 ln)

**Verdicts: all four PORT-WITH-CHANGES, but only ONE thing is worth porting: the
`LibertySampleTladFilmGrain` helper (identical in all four) + the amplitude
application.** See §4 for the full formula.

**A. Function breakdown**

| Function | Lines (`..._noise` / `..._mb_noise`) | Details |
|---|---|---|
| `AlphaTestPass` | 10-20 | unused |
| ABI copy | 1-~820 | inlined common |
| **`LibertySampleTladFilmGrain(float2 pixel_position, float2 temporal_offset)`** | **878-906** / **921-949** | 1 tap, 12 ALU — the whole grain system |
| `shaderMain` | 971-1255 / 970-1594 | composite: 5-tap DoF (`noise`) or +8-tap MB (`mb_noise`), then grain, then grade |

**Grain constants:** `NoiseParams` at `c216` (`PS c216`, `:833`) /
`c221` (`:861` for the MB variants); layout `(temporalX, temporalY, intensity, unused)`.

**Amplitude application** (four different register spellings, same math):

| File | Centring | Scale | Line |
|---|---|---|---|
| `tlad_composite_noise` | `r6.x = r3.y + c251.z` (`-0.5`) | `r8.x = NoiseParams.z * c253.w` (`c253.w = 1.1`) | `:1176`, `:1139`, added at `:1217` |
| `tlad_composite_mb_noise` | `r0.y = grain - c252.w` (`0.5`) | `r1.w = r0.y * NoiseParams.z`, then `* c250.w` (`1.1`) | `:1551`, `:1556`, `:1562` |
| `tlad_composite_high_dof_noise` | `r9.xw = r8.yz + c252.zw` (`-0.5`) | `r0.x = c253.w * max(NoiseParams.z,·)` (`1.1`) | `:1178`, `:1187-1190` |
| `tlad_composite_mb_high_dof_noise` | `r0.y = grain - c251.w` (`0.5`) | `r1.w = r0.y * NoiseParams.z`, then `* c249.y` (`1.1`) | `:1554`, `:1559`, `:1565` |

Uniform result: **`grain = (texture.b - 0.5) * NoiseParams.z * 1.1`**, added
*before* `* Exposure` (`:1222` / `:1567`), i.e. in linear pre-tonemap space.

The composites themselves are the same code as §2.9 plus the grain call, and the
`high_dof` variants differ only in the `dofBlur` band (`.x..y` vs `.y..z`) and the
`×256` / `×0.375` HDR decode.

**B. Portability:** same as §2.9. The grain helper itself is **100 % portable**:
1 `tex2Dlod`, `frac`, `floor`, `max` — no bitwise, no loops.

**C. Port plan:** see §4.4. Short version: **add a grain mode to
`shaders/ps/Grain.hlsl`**, do not port a composite.

---

## 3. Comparative table (D)

| IV effect | IV algorithm (this source) | our current equivalent (file:line) | delta | adopt? |
|---|---|---|---|---|
| **Deferred/volume lighting** | One term only: analytic sphere-volume integral `out = lightColour·(chord²/r²)`, `chord` = ray/sphere entry-exit clamped to `±0.66R`, smoothstep boundary `0.85R` (`LR/light_volume/deferred_lighting_ps20_smooth.hlsl:1007-1038`). No albedo / N·L / spec. | Forward+ clustered lights, 7 direct lights + ambient + IBL inside the PBR shaders (`src/render/forwardplus.cpp:493-550`; `shaders/include/PBR_Common.hlsl`; `shaders/ps/VehiclePBR_Modern.hlsl`). No volumetric add pass. | We have full BRDF shading; we have **no** volumetric light contribution. IV's volume integral is a **cheap additive glow** we can add per light without touching the BRDF. | **YES** (set `ivLightVolume=1`) — 34 instr, 1 tap. |
| **Bloom** | Separable *detail* bloom, no threshold/knee: 5-tap centre Gaussian (radius 1-4) then 12-radius ring, only `max(tap - center_blur, 0)` kept, weights sum 1.1002 (`LR/bloom/rage_postfx_scalable_bloom.hlsl:100-119`, constants `:46-60`). 25 unique taps, step = h/540 px. | Bright-pass extract with soft knee + Karis (`shaders/ps/Bloom_Extract.hlsl:33-58`), 4×4-tap downsample (`Bloom_Downsample.hlsl:56-70`), 9-tap tent upsample (`Bloom_Upsample.hlsl:33-45`), additive composite (`Bloom_Composite.hlsl:60-78`). Driven from `postfx_newfx.cpp:629-698`. | **Different family.** Ours = COD bright-pass pyramid (good HDR bloom, threshold configurable). IV = unsharp-based local contrast bloom with **no** threshold, resolution-independent radius, energy ×1.1. | **YES** as a *mode* (`bloomIVScalable=1`); keep ours as default. |
| **Motion blur** | Reconstruct previous UV from a 4×4 `motionBlurMatrix` + per-pixel depth (`:1179-1227`), delta = prevUV−curUV, ×`gDirectionalMotionBlurLength × LibertyMotionBlurTimeScale`; then **8 HDR taps + 8 stencil taps** in a jitter-rotated 0.5/0.75/1/2-delta ladder (`:1248-1413`), player masked by stencil vs `PLAYER_MASK.x`. 17 taps. | Velocity-buffer + Burnout radial/directional blur: 1 centre + 8 directional × 3 RGB-separated taps = 25 fetches (`shaders/ps/MotionBlur_Burnout.hlsl:127-160`), depth-aware (`:99-107`), added CA per sample. Driven from `postfx_motionblur.cpp`. | Ours = **8 samples, isotropic-ish (Burnout radial blend), needs a velocity buffer**. IV = **8 samples, matrix reprojection (no velocity buffer), 1/3 the fetch count (no RGB split), jittered, stencil-aware**. IV's ladder is anisotropic per-axis (0.125/0.75 scales) — a nicer streak shape. | **YES** — matrix reprojection is cheaper (no velocity buffer pass) and matches IV. Keep ours behind `motionBlurEnable`; add `motionBlurIVReproject`. |
| **Depth of field** | CoC = `GBuffer2.a × 256` quantised against `{2,4,8}` with a 4-band piecewise weight (`:1128-1168`); blur = **5 taps at ±1.5 texels** only (`:1092-1126`); final mix `0.25`-scaled, gated by a local-luma² test (`:1200-1221`). No bokeh, no disc, no near/far split. | Signed near/far CoC from linear depth (`shaders/ps/DOF_CoC.hlsl:80-99`), **22-tap golden-spiral disc bokeh** with scatter-as-gather near/far handling (`shaders/ps/DOF_Bokeh.hlsl:27-60`), tent postfilter, full-res combine (`postfx_newfx.cpp:596-619`). | **Ours is strictly better** in every quality dimension (real bokeh disc, continuous CoC, near-field over-blur). IV's "DoF" is a 1.5-px soften, i.e. an anti-alias/defocus hint. | **NO** (only the 4-band quantiser idea is interesting for a *cheap* tier). |
| **Noise / grain** | 1 tap of a 256² noise tile `.b` at a fixed 1.5625-px texel; `temporal_shift = floor(frac(t)*dims)` = **whole-texel sliding**; amplitude `(n-0.5) * intensity * 1.1`, added pre-exposure (`§4`, `LR/film_grain/tlad_composite_noise.hlsl:878-906`, `:1139`, `:1176`, `:1217`). | Procedural IGN, 3 octaves, animated 5.588 px/frame, S-curve luminance response, additive post-tonemap (`shaders/ps/Grain.hlsl:34-63`, `:77-84`, `:138`), `postfx_newfx.cpp:737-763`. | Ours = more "film", no fetch, TAA-friendly, but **no authored amplitude reference** and it is applied **after** tone map (wrong space for IV parity). IV = 1 fetch, exact Rockstar intensity, **pre-tonemap**, temporally *coherent* (slides, does not flicker). | **YES** — port the IV formula as `grainMode=2`; see the verdict in §4.5. |
| **Exponential height fog** | `tau = density · exp2(-camAlt·falloff·sat) · (1-exp2(-slope·d))/slope`; `fog = (1-exp2(-tau))^exponent`; blended into a **luminance-preserving** near/far fog colour ramp `(chroma·(1+a)+luma)·luma^b` (`LR/fog/rage_postfx_exponential_height_fog.hlsl:104-153`). | Crytek height fog: `heightFactor·(1-exp(-density·depth))`, constant colour lerp (`shaders/ps/HeightFog.hlsl:76-86`). | Ours uses the camera-height-only factor (constant along the ray) — **no line integral**, so aerial/tilted views fog uniformly instead of thinning with altitude. Ours also blends to a flat colour (no luminance coupling). | **YES** as `heightFogIVModel=1`; keep Crytek as default (cheaper, already tuned). |
| **Water shoreline** | `1 - exp2(-3·depth·waterColour.w)` + 3 foam octaves weighted `0.5/0.25/0.125`, `saturate(sum - 0.4375)` (`LR/water/water_ps1_smooth_shorelines.hlsl:1424-1425`, `:1138-1171`). | Same two formulas, already ported: `WaterShoreCoverage` (`shaders/ps/Water_Parallax.hlsl:265-269`), `WaterFoamOctaves` (`:287-295`), used at `:744`, `:816-827`, `:1084`, `:1185-1191`. | **Exact match** on the octaves. Coefficient differs: ours `2/waterShoreFade`, IV `3·waterColour.w` per metre. | **Already adopted**; optional `waterShoreExponentFromDepth`. |

---

## 4. The noise formula (E) — `tlad_composite_*_noise.hlsl`

This is the highest-value single function in the whole set, because it is a
**complete, authored, production noise system in 12 lines** and it maps 1:1 onto
the `src/render/noise.cpp` + `shaders/include/Noise.hlsl` design.

### 4.1 The exact function (verbatim, `LR/film_grain/tlad_composite_noise.hlsl:878-906`)

```hlsl
float LibertySampleTladFilmGrain(
    float2 pixel_position, float2 temporal_offset)
{
  uint2 texture_size = getTexture2DDimensions(
      g_Texture2DDescriptorHeap[BlurSampler_Texture2DDescriptorIndex]);
  float2 dimensions = max(float2(texture_size), float2(1.0, 1.0));
  // The original 256-texel noise tiled 3.2 times across a 1280-pixel frame,
  // giving a 1.5625-pixel grain texel. Keep that physical screen-space size
  // instead of stretching the grain as the internal render resolution rises.
  const float grain_texel_size = 1.5625;
  float2 temporal_shift = floor(frac(temporal_offset) * dimensions);
  float2 grain_position = (pixel_position - float2(0.5, 0.5)) / grain_texel_size;
  float2 grain_uv = frac((grain_position + temporal_shift + 0.5) / dimensions);
  return tfetch2D(
      BlurSampler_Texture2DDescriptorIndex, BlurSampler_SamplerDescriptorIndex,
      grain_uv, float2(0, 0), GetTextureLodBias(3)).z;
}
```

### 4.2 What each piece means

| Piece | Value | Meaning |
|---|---|---|
| **Input hash** | *none* — a texture lookup | The "hash" is a **256×256 authored noise tile**, sampled at `.z` (the **blue** channel). There is no procedural hash, no `sin`, no IGN. |
| **Grain texel size** | `1.5625` px | Derived: 1280 px / 256 texels / 3.2 tiles = 1.5625 px per noise texel (`:893-896`). Fixed in *screen pixels*, so grain size is resolution-independent. |
| **Pixel origin** | `pixel_position - 0.5` | `SV_Position` is pixel-centre at `.5`; the `-0.5` re-centres to integer pixel indices before dividing by 1.5625. |
| **Temporal component** | `floor(frac(t) * dimensions)` | The host passes a 2-component `NoiseParams.xy` that **advances per frame**. It is quantised to **whole texels** of the 256² tile → the pattern *slides* by integer texels, never resamples/blurs, never flickers per-pixel. It **wraps** at 1.0 because of `frac`. |
| **UV wrap** | `frac((…) / dimensions)` | Manual wrap (the sampler is also WRAP in practice), guaranteed tileable with no edge smear. |
| **Amplitude** | `(n - 0.5) * NoiseParams.z * 1.1` | `n ∈ [0,1]`, so the grain is bipolar `[-0.5, +0.5] × 1.1 × intensity` (see §4.3). |
| **Application** | added to linear colour **before** `× Exposure` and before tone mapping | `LR/film_grain/tlad_composite_noise.hlsl:1217` (`r0.xyz = r1.www*r7.xyz + r6.xxx`) → `:1222` (`r0.xyz = r0.xyz*Exposure.xxx + r1.xyz`) |
| **Sampler / LOD** | `BlurSampler`, `GetTextureLodBias(3)`, `SampleLevel` | **Point filtering is required** — the UV lands on sub-texel positions (`/1.5625`), so LINEAR would smear each grain dot across 2-3 px and destroy the look. |

### 4.3 The four equivalent amplitude spellings (proof they agree)

| File:line | Centring | Amplitude register | `1.1` register |
|---|---|---|---|
| `tlad_composite_noise.hlsl:1176` / `:1139` / `:1217` | `grain + c251.z` = `grain − 0.5` | `r8.x = NoiseParams.z * c253.w` | `c253.w = 0x3F8CCCCD = 1.1` |
| `tlad_composite_mb_noise.hlsl:1551` / `:1556` / `:1562` | `grain − c252.w` = `−0.5` | `r1.w = (grain−0.5)·NoiseParams.z` then `× c250.w` | `c250.w = 1.1` |
| `tlad_composite_high_dof_noise.hlsl:1178` / `:1187-1190` | `grain + c252.z` = `−0.5` | `ps = c253.w * max(NoiseParams.z,·)` | `c253.w = 1.1` |
| `tlad_composite_mb_high_dof_noise.hlsl:1554` / `:1559` / `:1565` | `grain − c251.w` = `−0.5` | `r1.w = (grain−0.5)·NoiseParams.z` then `× c249.y` | `c249.y = 0x3F8CCCCD = 1.1` |

### 4.4 Mapping onto our planned universal noise system

Our design (`docs/plans/2026-10-25-noise-system-audit.md:25`):
`src/render/noise.h/.cpp` + `shaders/include/Noise.hlsl`, `s9` = utility tile
(**R = Perlin fBm, G = Worley F1, B = white hash, A = blue-noise/Bayer**),
`c49-c52` = noise params.

```
IV grain  ==  (NoiseTex(s9).B - 0.5) * c49.z * 1.1
             ^^^^^^^^^^^^^^^^^^^   ^^^^^^   ^^^
             s9.B = white hash    intensity  authored 1.1
```

| IV input | Maps to | Notes |
|---|---|---|
| 256² noise texture, channel `.z` | `s9.B` | **exact** — our B channel *is* white hash. No new sampler, no new resource. |
| `grain_texel_size 1.5625 px` | `c49.xy = (1/1.5625, 1/1.5625)` | pass the reciprocal so the shader multiplies instead of divides. |
| `NoiseParams.xy` (temporal) | `c50.xy` | C++ advances it per frame; keep it in **tile-texel units** so `floor(frac(t)*256)` still works. Our `noise.cpp` should expose `Noise_TickGrain(float dt)` returning the accumulated offset. |
| `NoiseParams.z` (intensity) | `c49.z` | reuse `grainIntensity` (already `0..1`, `src/core/main.cpp:6039-6041`). |
| texture dimensions | `256.0` compile-time | our tile is exactly 256² (`docs/plans/2026-10-25-noise-system-audit.md:78`), so `dimensions` becomes a constant → saves the `GetDimensions` path (not available in `ps_3_0` anyway). |

**`ps_3_0` version (drop-in, 1 tap):**

```hlsl
// grainMode == 2 (IV): 1 tap of s9.B, whole-texel temporal slide.
float2 dims      = 256.0;
float2 tshift    = floor(frac(c50.xy) * dims);
float2 gpos      = (vpos.xy - 0.5) * c49.x;          // 1/1.5625
float2 guv       = frac((gpos + tshift + 0.5) / dims);
float  gn        = tex2Dlod(noiseTex, float4(guv, 0.0, 0.0)).b;
grain           += (gn - 0.5) * c49.z * 1.1;
```

Bind `s9` with **`D3DTEXF_POINT`** and **`D3DTADDRESS_WRAP`** for this mode.
Estimated: **~14 instructions, 1 texture fetch** — cheaper than our IGN path
(which is ~40 ALU for 3 octaves).

### 4.5 Is IV's formula better than ours?

| Dimension | IV (texture) | ours (`Grain.hlsl`) | Winner |
|---|---|---|---|
| Cost | 1 tap, ~14 instr | ~40 ALU, 0 taps | IV |
| Authored amplitude | yes — `NoiseParams.z × 1.1` is Rockstar's number | no reference; `grainIntensity` is our own 0..1 | IV (for parity) |
| Grain size stability | fixed 1.5625 px at any internal res | fixed screen px (VPOS) | tie |
| Temporal behaviour | **slides by whole texels**, coherent, no flicker | slides 5.588 px/frame, coherent | tie — both are "sliding", IV quantises (≈1.5625-px steps), ours is smooth |
| Spectrum | flat white noise (1 texel) | 3 octaves (1/2/4 px) + IGN low-discrepancy | **ours** |
| Luminance response | none (uniform, including in highlights) | S-curve hides grain in toe/shoulder | **ours** |
| Coloured grain | no | yes (`GrainNoiseColored`) | **ours** |
| TAA interaction | white noise + integer slide = **crawls** under TAA | IGN chosen specifically to be 3×3-friendly | **ours** |
| Application space | linear, pre-exposure, pre-tonemap (**correct**) | display, post-tonemap | **IV** |

**Verdict:** IV's formula is better on two axes that matter for parity —
**it is the authored look**, and **it is applied in the right space**. Ours is
better on quality axes (spectrum, response, TAA). The right move is a **mode**,
not a replacement: `grainMode=0` (IGN, current default) / `1` (IGN, pre-tonemap)
/ `2` (IV texture grain, pre-tonemap). For mode 2 also move the call from the
lens group (`src/render/postfx.cpp:1871-1884`) into the pre-tonemap FX group
(`:1659-1690`), after `DrawBloom()` — that is exactly IV's slot (bloom →
composite → grain → exposure → tonemap).

---

## 5. Build / register integration summary

### 5.1 New files and IDs (proposed)

| File | Entry | CSO id | `Resource.rc` | Profile |
|---|---|---|---|---|
| `shaders/ps/Bloom_IVBlur.hlsl` | `main` | 281 | add | `ps_3_0` |
| `shaders/ps/GTAIVLightVolumePS.hlsl` | `main` | 282 | add | `ps_3_0` |
| `shaders/vs/GTAIVLightVolumeVS.hlsl` | `main` | 283 | add | `vs_3_0` |
| `shaders/ps/GTAIVCompositeMB.hlsl` | `main` | 284 | add | `ps_3_0` |
| `shaders/ps/HeightFog.hlsl` | `main` (existing) | 250 | none | `ps_3_0` |
| `shaders/ps/Grain.hlsl` | `main` (existing) | 261 | none | `ps_3_0` |
| `shaders/ps/Water_Parallax.hlsl` | existing | existing | none | `ps_3_0` |

`fast_build.py` auto-discovers `shaders/ps/*.hlsl` single-entry shaders
(`tools/fast_build.py:356-404`) — **no `multi_entry` entry is needed** as long
as each new file has exactly one `main`.

### 5.2 Register budget

Two different rules apply, and they must not be mixed up:

- **Standalone full-screen postfx passes** (bloom, MB composite, light volume)
  own their constant file for the duration of the draw. `c0..cN` is correct and
  collides with nothing — that is how `postfx_newfx.cpp` already uploads.
- **Shaders that share an existing constant block** with the PBR/vehicle/building
  pipelines must use the project-wide free slots. That is only `HeightFog`
  (already at `c0-c5`) and `Grain` (at `c0-c1`), where `c49+` is the safe choice.
  Their entries below use `c49+` deliberately.

| Pass | Samplers | Constants | Collides? |
|---|---|---|---|
| `Bloom_IVBlur` | `s0` | `c0` | no (standalone pass; per-draw upload) |
| `GTAIVLightVolumePS` | `s0` (depth) | `c0-c2` | no (standalone additive pass) |
| `GTAIVLightVolumeVS` | — | `c0-c12` | no (standalone; not the PBR VS) |
| `GTAIVCompositeMB` | `s0-s6` | `c0-c5` | no (standalone pass) |
| `HeightFog` (IV mode) | `s0,s1` (existing) | `c6-c8` | no (pass-local after the existing `c0-c5`) |
| `Grain` (IV mode) | `s9` | `c49,c50` | **`s9` is the reserved noise slot** — good, not a collision |

### 5.3 Proposed INI keys (all under `[SkyGfx]`, matching
`readint(cfg.get("SkyGfx","…",""), default)` at `src/core/main.cpp:5824-6041`)

```
bloomIVScalable=0            ; 1 = IV 25-tap detail bloom (Bloom_IVBlur) on mip0
ivLightVolume=0              ; 1 = analytic light-volume add pass
ivLightVolumeScale=0.66      ; IV authored volume scale (c255.x in the source)
ivLightVolumeSmooth=0.85     ; IV support-radius boundary (0x3F59999A)
ivLightVolumeIntensity=1.0
motionBlurIVReproject=0      ; 1 = matrix reprojection instead of the velocity buffer
motionBlurIVPlayerMask=1     ; stencil-based player rejection
heightFogIVModel=0           ; 1 = analytic line integral + luminance fog (IV)
heightFogIVFalloff=0.02      ; 1/m, matches IV's globalFogParams.y role
heightFogIVExponent=1.0      ; final pow() exponent (fog.w)
grainMode=0                  ; 0 = IGN post-tonemap (current), 1 = IGN pre-tonemap,
                             ; 2 = IV texture grain pre-tonemap
waterShoreExponentFromDepth=0; 1 = tie the shoreline ramp to waterColour.w like IV
```

Each of these must be added to **all four** places the config system touches:
`config->` field (`src/skygfx.h`), `readint/readfloat` (`src/core/main.cpp:~5824-6041`),
`cfg.set(...)` defaults (`:~6153-6182`), and `ADD_IF_MISSING(...)` (`:~6292-6312`),
plus the save block (`:~6605-6632`). Missing one leaves the key non-persistent.

---

## 6. Recommended sequencing

1. **`HeightFog` IV mode** (65 instr, no new pass, no new resource). Highest
   visual delta per unit risk: our fog is camera-height-only, IV's integrates
   along the ray.
2. **`Grain` mode 2** (14 instr, reuses `s9`, validates the noise contract).
   Do it as part of `noise.cpp` landing, not before.
3. **`Bloom_IVBlur`** (124 instr, new pass, one constant). Note it needs a
   *bright-pass before it* — either keep `Bloom_Extract` or expose
   `bloomThreshold=1.0`.
4. **`GTAIVCompositeMB`** MB-only port (matrix reprojection + jittered 8 taps).
   This is the one unmeasured cost (~200-230 slots) and the one that needs a
   `motionBlurMatrix` hook in `postfx_motionblur.cpp`.
5. **`GTAIVLightVolume` PS+VS pair** — only after `forwardplus.cpp` can emit
   proxy geometry; it must ship as a pair (manifest contract §1.3).
6. **Water coefficient** — one-line change, optional.

Do **not** port: the composite grade chain, the 5-tap IV DoF, the debug
variants (`ps15`/`ps17`), the `water_ps1` container, `rage_postfx_fog_vs.hlsl`,
or any `xenos_shader_common.h` function that touches the descriptor heap.

---

## Appendix A — measured `ps_3_0` instruction counts

Method: the algorithmic bodies were transcribed to plain SM3.0 HLSL (no
`Texture2D`, no `vk::`, no descriptor heaps) in a scratch directory **outside the
repository**, then compiled with the project's FXC:

```
fxc -T ps_3_0 -nologo -E main -Fc <out>.asm <probe>.hlsl
```

| Probe | Source | Result |
|---|---|---|
| `bloom_sm3.hlsl` | `LR/bloom/rage_postfx_scalable_bloom.hlsl:95-122` | **124 slots (25 texture, 99 arithmetic)** |
| `fog_sm3.hlsl` | `LR/fog/rage_postfx_exponential_height_fog.hlsl:87-154` | **65 slots (2 texture, 63 arithmetic)** |
| `dlight_sm3.hlsl` | `LR/light_volume/deferred_lighting_ps20_smooth.hlsl:977-1039` | **34 slots (1 texture, 33 arithmetic)** |

Two facts fell out of the bloom compile that are worth keeping:

- **25 taps, not 33.** FXC proves the four centre-blur radii and the first four
  broad radii share coordinates and reuses the 8 fetches
  (the compiled assembly fetches 8 taps for radii 1-4, then reuses those 8
  registers as the first four broad radii, applying weights `kBroadWeights[0..3]`).
- The 12 broad weights and 5 centre weights collapse to `def` immediates —
  no dynamic array indexing, so the kernel is a pure straight-line program.

Fog and light-volume numbers are exact for the transcribed bodies; the motion
blur composite and the water container are **estimates** (≈200-230 and ≈250-290)
and are marked as such in §2.9 and §2.8.

## Appendix B — files read in full

`manifest.json`, `shader_override_cache.h`, `xenos_shader_common.h`,
`bloom/rage_postfx_scalable_bloom.hlsl`,
`fog/rage_postfx_exponential_height_fog.hlsl`, `fog/rage_postfx_fog_vs.hlsl`,
`light_volume/deferred_lighting_ps20_smooth.hlsl`,
`light_volume/deferred_lighting_vs7_smooth.hlsl`,
`motion_blur/gta_composite_mb.hlsl`, `motion_blur/gta_composite_mb_high_dof.hlsl`,
`motion_blur/gta_composite_mb_dof_debug.hlsl`, `motion_blur/gta_composite_mb_high_dof_debug.hlsl`,
`film_grain/tlad_composite_noise.hlsl`, `film_grain/tlad_composite_mb_noise.hlsl`,
`film_grain/tlad_composite_high_dof_noise.hlsl`, `film_grain/tlad_composite_mb_high_dof_noise.hlsl`,
`water/water_ps1_smooth_shorelines.hlsl` (1453 lines, all read).
`LRC/shader_cache.cpp` parsed programmatically (1,356 entries; hash→name table built in §1.1).
