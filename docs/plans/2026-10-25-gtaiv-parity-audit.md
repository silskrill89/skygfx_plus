# GTA IV Parity Audit — LibertyRecomp GPU vs SkyGFX Plus `MODE_GTAIV`

**Date:** 2026-10-25
**Mode of work:** read-only audit. No source, shader, config, or resource file was modified.
**Only artifact written:** this file.

Reference root: `E:/SDKs/LibertyRecomp-main/LibertyRecomp/`
Our root: `E:/dev(dave)/skygfx_plus_expIV/`

---

## Scope, method, and evidence legend

| Tag | Meaning |
|---|---|
| `[REF]` | Implemented in `LibertyRecomp/gpu/` and readable as source. |
| `[REF-CACHE]` | GTA IV-native shader exists only as a named, compressed binary entry in `LibertyRecompLib/shader/shader_cache.cpp` (outside `gpu/`, consumed by `gpu/video.cpp`). The blob is opaque; no HLSL source exists in the reference. |
| `[OURS]` | Implemented in our tree. |
| `[ABSENT]` | Not found in the reference at all (explicitly checked). Behaviour is **not** guessed. |

Three facts govern every conclusion below:

1. **`gpu/` is a D3D9→Vulkan/D3D12/Metal translation layer, not a reimplementation of RAGE.** The title's shaders are recompiled guest blobs looked up by hash (`gpu/video.cpp:6621-6695`). The only human-readable title-effect shaders in `gpu/shader/hlsl/` are `blend_color_alpha_ps`, `csd_vs`, `csd_no_tex_vs`, `csd_filter_ps`, `enhanced_burnout_blur_vs/ps`, and the `gaussian_blur_*` family; the rest are generic backend utilities/UI and the reference's **custom** post-process pack.
2. **The host cannot bind more than one render target.** `SetRenderTarget` drops every index other than 0 (`gpu/video.cpp:4278-4283`: *"Multiple targets are not currently handled"*), and framebuffer creation attaches exactly one colour texture (`gpu/video.cpp:4595-4596`). GTA IV's deferred/G-buffer path therefore cannot be hosted by this layer.
3. **The custom postfx pack is mostly Vulkan-only, and several passes are never dispatched.** `CreateShaders` creates SSAO/DoF/SSR/bloom/sun-shafts/CA/grain/MB only in the Vulkan branch (`gpu/postprocess_renderer.cpp:301-338`); D3D12 and Metal create only TAA/SMAA/FSR1 (`gpu/postprocess_renderer.cpp:259-298`), and `PostProcess::ApplyAA` has **no caller** anywhere in the tree. Vignette is never compiled or created. This is not a complete IV renderer to copy from; it is a donor for portable pass code.

Consequence for this audit: for IV-native effects we report presence from the shader-cache inventory, and we mark **`not found in reference`** when only the opaque blob exists or when no host implementation exists. We never infer an IV algorithm from a filename.

---

## 0. Executive summary

- **The reference does not contain a readable GTA IV renderer.** It contains (a) a D3D9 state/resource translation layer, (b) a custom postfx shader pack, and (c) 1356 recompiled game-shader cache entries (`LibertyRecompLib/shader/shader_cache.cpp:1366`). IV's deferred lighting, shadows, water, postfx, vehicle and glass shaders are all opaque binary entries (`[REF-CACHE]`).
- **The reference's own postfx is partly dead.** On D3D12/Metal only TAA/SMAA/FSR1 are created and none of them is dispatched; vignette is never created; CA/grain/MB/vignette have no caller. Bloom/sun-shafts/DoF/SSAO/SSR only run on Vulkan.
- **Our `MODE_GTAIV` is a shader swap + bypass + grade overlay, not an effect stack.** It owns 35 fields (`src/core/modeManager.cpp:346-395`), leaves every postfx gate at the pre-mode value, and deliberately does not own the `iv*` grade sliders (`src/core/modeManager.cpp:341-344`).
- **`COLORFILTER_GTAIV` is a no-op.** It unbinds shaders and breaks (`src/render/postfx.cpp:1842-1850`). The IV look today comes from `DrawIVGrade` (`src/render/postfx_colourfilter.cpp:1354-1547`), which is an opaque embedded PS (`:1084-1316`) with no HLSL source.
- **We already own far more IV-relevant code than `MODE_GTAIV` uses:** bloom pyramid, 4-pass DoF, sun shafts, CA, film grain, vignette, FXAA3 (`src/render/postfx_newfx.cpp:517-823`), SSAO (`src/render/postfx_ssao.cpp:518/706`), SMAA (`src/render/postfx_smaa.cpp:441`), auto-exposure (`src/render/postfx_luminance.cpp:168`), a 10-operator tonemap chain (`shaders/ps/TonemapLUT.hlsl`), an IV water entry (`shaders/ps/Water_Parallax.hlsl`, `main_iv`), and an unused IV paint shader (`shaders/ps/VehiclePaint_GTAIV.hlsl`, `IDR_VEHICLEPAINT_GTAIV 200`).
- **The fastest parity win is not a new shader; it is wiring the existing passes into `kValuesGTAIV`** and removing the duplicate cheap bloom/vignette from `DrawIVGrade`.
- **Register headroom:** with PBR PS `c0-c48` and `s0-s8` allocated, the next free PS constant is **`c49`** and the next free PS sampler is **`s9`**. VS is used through `c44` by some variants; first free VS constant is **`c45`**. `ps_3_0` still allows up to `c223` and `s15`.

---

## §1 LibertyRecomp GPU inventory

### 1.1 Module map (non-shader)

| File | Role | Key entry points / symbols | Notes |
|---|---|---|---|
| `gpu/video.cpp` (10,902 ln) | D3D9 device emulation: draw translation, render-state binding, shader-cache lookup, present, HDR/gamma blit, postfx orchestration | `Video::CreateHostDevice`, `Video::Present`, `Video::StartPipelinePrecompilation`, `Video::WaitForGPU`, `Video::ComputeViewportDimensions`, static `DrawPrimitive`, `CreateShader`, `SetRenderTarget`, `SetFramebuffer` | Single-RT only (`:4278-4283`, `:4595-4596`). Shader cache lookup `:6621-6695`. Postfx chain `:3543-3650`. Present blit `:3834-3841`. |
| `gpu/video.h` (680 ln) | Guest resource types (`GuestDevice`, `GuestShader`, `GuestTexture`, `GuestSurface`, `GuestSamplerState`), backend enum | `struct GuestShader : GuestResource` (`:523-527`), `Backend` (`:39-44`) | Device is GTA IV v8 (`0x5780` bytes); offsets verified against PPC. |
| `gpu/gtaiv_device.h` (279 ln) | GTA IV `grcDevice` field offsets, PM4 opcodes, primitive/index enums, draw-state extraction | `GTAIV::DeviceOffset`, `GTAIV::DrawCallState::Extract`, `GTAIV::DetectIndexFormat` | 4 RT slots (`:107`), 19 texture slots (`:110`), `ShaderFlags` bit7 = deferred (`:70`). No rendering algorithm. |
| `gpu/gtaiv_render_state.h/.cpp` (135/261 ln) | Extended IV state extraction: EDRAM/10-bit HDR formats, 4 RTs, 19 textures, sampler descriptors, stats | `GTAIV::RenderState::Extract`, `SamplerState::Extract`, `CommitState`, `IsHDR10Bit`, `IsHDR16Bit` | Extract is called for logging at `video.cpp:5868`. EDRAM formats `R10G10B10A2` / `R16G16B16A16_F` (`gtaiv_render_state.h:45-60`). No host HDR emulation. |
| `gpu/gfx_state.h/.cpp` (190/447 ln) | Guest→host handle maps (shader/buffer/texture/surface), draw-call stats, pipeline cache, pass enum | `RegisterShader/LookupShader`, `RegisterTexture/LookupTexture`, `BeginDrawCall`, `BeginRenderPass`, `LookupPipelineState` | `RenderPassType::GBuffer` exists as a label (`gfx_state.h:35`); no producer. `BeginRenderPass` has no callers in the tree. |
| `gpu/camera_extract.h/.cpp` (194/174 ln) | Extract camera view/proj/inv matrices from guest globals or VS constants | `Camera::ExtractCameraFromGuest`, `Camera::ExtractCameraFromShaderConstants` | Used by `Video::Present` at `video.cpp:3525-3534`. |
| `gpu/postprocess_renderer.h/.cpp` (717/2208 ln) | Custom postfx pass implementations + constants + RTs | `PostProcessRenderer::ApplyTAA/ApplySMAA/ApplyFSR1/ApplyVignette/ApplySSAO/ApplyDoF/ApplySSR/ApplyFilmGrain/ApplyChromaticAberration/ApplyMotionBlur/ApplyBloom/ApplySunShafts` | See §1.5/§2. Vulkan-only for most passes. |
| `gpu/postprocess_aa.h/.cpp` (100/380 ln) | AA state, jitter, dynamic-res, SSAA multiplier; SMAA/TAA/FSR1 dispatch helper | `PostProcess::InitializeAA`, `UpdateAA`, `ApplyAA`, `GetTAAJitter`, `GetSSAAMultiplier` | `ApplyAA` has **no caller**. `InitializeAA`/`UpdateAA` are called at `video.cpp:2308/3445` only. |
| `gpu/shadow_quality.h/.cpp` (76/109 ln) | Shadow resolution/filter config, PCF/PCSS constants | `ShadowQuality::Initialize`, `UpdateFromConfig`, `GetShaderConstants` | Dead scaffold: no caller in the tree. |
| `gpu/sps_preset_table.h` (206 ln) | Auto-generated GTA IV `.sps` → FXC shader + parameter table | `g_spsPresetTable[]`, `s_spsParams_gta_vehicle_*` | **Primary readable IV material evidence**: vehicle paint/chrome/rims/glass specular values. |
| `gpu/smaa_area_tex.h`, `gpu/smaa_search_tex.h` | SMAA lookup tables | static arrays | Used by `CreateSMAATables` (`postprocess_renderer.cpp:705`). |
| `gpu/upscaler.h/.cpp`, `upscaler_dlss.*`, `upscaler_fsr3.*`, `upscaler_xess.*`, `upscaler_metalfx.*` | FSR1/FSR3/DLSS/XeSS/MetalFX integration | `Upscaler::Initialize`, `BeginFrame`, `UpscaleFrame`, `GenerateIntermediateFrame`, `GetCurrentJitter` | Not an IV effect; spatial/temporal upscalers. |
| `gpu/cache/pipeline_state_cache.h`, `vertex_declaration_cache.h`, `vertex_element_cache.h` | Generated hash/declaration tables | static arrays | `pipeline_state_cache.h` is empty in this checkout. |
| `gpu/imgui/*` (6 files) | ImGui backend, font builder, UI snapshot | `AddImGuiCallback`, `ImGuiFontBuilder`, `ImGuiSnapshot` | UI only. |
| `gpu/ffx_a.h` | AMD FidelityFX common helpers | macros/inline | Not a shader. |
| `gpu/shader/hlsl/postprocess_reference/*` | Older/reference copies of postfx shaders (11 files) | — | Not in `CMakeLists.txt` compile list; not consumed by `postprocess_renderer.cpp`. Treat as documentation, not shipped code. |
| `gpu/shader/msl/*` | Metal mirrors of the HLSL shaders (`.metal`, `.ir`, `.metallib`, generated `.h`) | — | Generated counterparts; no independent logic. |

### 1.2 HLSL inventory — every `.hlsl` and `.hlsli`

There are **no `.fx` files** in `gpu/shader/hlsl/`. All compiled entries are named `shaderMain` (`CMakeLists.txt:1718-1828` invokes DXC with `-E shaderMain`).

| # | File | Implements | Stage | Compiled/wired? | Notes |
|---:|---|---|---|---|---|
| 1 | `blend_color_alpha_ps.hlsl` | Recreated guest shader: sample × alpha, alpha blend remap | PS | Yes — `video.cpp:6719`, hash `0x85ED723035ECF535` | Title shader, not postfx. |
| 2 | `bloom_composite_ps.hlsl` | Bloom composite: saturation, intensity, additive/screen blend | PS | Vulkan only | `shaderMain` `:40`; scene + bloom. |
| 3 | `bloom_downsample_ps.hlsl` | 13-tap energy-preserving bloom downsample; Karis on mip 0 | PS | Vulkan only | `shaderMain` `:40`; weights center 4 / inner 2 / outer 1 / 20. |
| 4 | `bloom_extract_ps.hlsl` | Bright-pass with soft knee + Karis average (anti-firefly) | PS | Vulkan only | `shaderMain` `:71`; 4 taps. |
| 5 | `bloom_upsample_ps.hlsl` | 9-tap tent upsample + additive accumulation | PS | Vulkan only | `shaderMain` `:58`; 1-2-1 tent /16. |
| 6 | `chromatic_aberration_ps.hlsl` | Radial per-channel RGB offsets, soft knee, max-offset clamp | PS | Vulkan create only; **no caller** | `shaderMain` `:36`; `ApplyChromaticAberration` `postprocess_renderer.cpp:1686`. |
| 7 | `copy_color_ps.hlsl` | Fullscreen texture `Load` copy | PS | Yes | Used by `StretchRect`/copy pipeline. |
| 8 | `copy_depth_ps.hlsl` | Fullscreen depth `Load` copy | PS | Yes | Used by depth copy path. |
| 9 | `copy_vs.hlsl` | Fullscreen triangle VS from `SV_VertexID` | VS | Yes | `video.cpp:2431`. |
| 10 | `csd_filter_ps.hlsl` | CSD texture filter + derivative-based UV seam clamp + alpha clip | PS | Yes — `video.cpp:2485`, pipeline `:8578` | Title shader. **Not a shadow-map compare**; see §2. |
| 11 | `csd_no_tex_vs.hlsl` | Pre-transformed screen-space quad VS (no texcoord) | VS | Yes — `video.cpp:6721` | Title shader. |
| 12 | `csd_vs.hlsl` | Pre-transformed screen-space quad VS + 16 TEXCOORD + 2 COLOR outputs | VS | Yes — `video.cpp:6723` | Title shader. |
| 13 | `dof_bokeh_ps.hlsl` | 22-tap disk bokeh, separate FG/BG accumulation | PS | Vulkan only | `shaderMain` `:72`; Unity PPSv2 disk. |
| 14 | `dof_combine_ps.hlsl` | Full-res sharp/bokeh mix via signed CoC + FG alpha | PS | Vulkan only | `shaderMain` `:57`. |
| 15 | `dof_postfilter_ps.hlsl` | 4-tap half-texel tent smoothing of bokeh | PS | Vulkan only | `shaderMain` `:34`. |
| 16 | `dof_prefilter_ps.hlsl` | Half-res colour downsample + extreme-of-4 CoC select | PS | Vulkan only | `shaderMain` `:69`; CoC in alpha. |
| 17 | `dof_ps.hlsl` | Alternate single-pass spiral-sampling DoF | PS | **Not wired** | Has `.spv` but no `.spirv.h`; not created in `postprocess_renderer.cpp`. |
| 18 | `enhanced_burnout_blur_ps.hlsl` | Title velocity blur: per-vertex velocity, 4-64 samples, squared blur amount | PS | Yes — `video.cpp:2491` | Title shader; caller/dispatch path not visible in `gpu/`. |
| 19 | `enhanced_burnout_blur_vs.hlsl` | Emits centred UV + velocity/velocity-scale from `c210` | VS | Yes — `video.cpp:2488` | Title shader. |
| 20 | `film_grain_ps.hlsl` | IGN film grain, 3 octaves, luminance S-curve, mono/colored | PS | Vulkan create only; **no caller** | `shaderMain` `:127`; `ApplyFilmGrain` `postprocess_renderer.cpp:1607`. |
| 21 | `fsr1_easu_ps.hlsl` | FSR 1.0 EASU edge-adaptive upscale | PS | Shader compiled; used only if `ApplyFSR1` called | `shaderMain` `:31`; `ApplyFSR1` `postprocess_renderer.cpp:947`. |
| 22 | `fsr1_rcas_ps.hlsl` | FSR 1.0 RCAS sharpening | PS | Shader compiled; used only if `ApplyFSR1` called | `shaderMain` `:20`. |
| 23 | `fullscreen_vs.hlsl` | Fullscreen triangle from vertex ID | VS | Yes | Created per backend in `postprocess_renderer.cpp:269/286/305`; shared by all fullscreen passes. |
| 24 | `gamma_correction_ps.hlsl` | Present blit: `pow(color, gamma)` | PS | Yes — `video.cpp:2495`, pipeline `:2504` | Final SDR gamma. |
| 25 | `gaussian_blur_3x3.hlsl` | Title scalable Gaussian, KERNEL_SIZE=3 | PS | Yes — `video.cpp:2479` | Includes `gaussian_blur.hlsli`; constants at c150/c166. |
| 26 | `gaussian_blur_5x5.hlsl` | Same, KERNEL_SIZE=5 | PS | Yes — `video.cpp:2480` | Title shader. |
| 27 | `gaussian_blur_7x7.hlsl` | Same, KERNEL_SIZE=7 | PS | Yes — `video.cpp:2481` | Title shader. |
| 28 | `gaussian_blur_9x9.hlsl` | Same, KERNEL_SIZE=9 | PS | Yes — `video.cpp:2482` | Title shader. |
| 29 | `hdr_tonemap_ps.hlsl` | Present HDR: ACES SDR, scRGB knee, HDR10 PQ | PS | Yes — `video.cpp:2507`, pipeline `:2512` | `shaderMain` `:98`; ACES at `:80-86`. |
| 30 | `imgui_ps.hlsl` | ImGui pixel shader (gradients, bevels, marquee) | PS | Yes — `video.cpp:1888` | UI. |
| 31 | `imgui_vs.hlsl` | ImGui vertex shader | VS | Yes — `video.cpp:1887` | UI. |
| 32 | `motion_blur_camera_ps.hlsl` | Depth reprojection camera motion blur, bidirectional 2-32 samples | PS | Vulkan create only; **no caller** | `shaderMain` `:65`; `ApplyMotionBlur` `postprocess_renderer.cpp:1776`. |
| 33 | `resolve_msaa_color_2x.hlsl` | MSAA colour resolve, 2 samples | PS | Yes — `video.cpp:2445` | D3D9 path uses StretchRect instead. |
| 34 | `resolve_msaa_color_4x.hlsl` | MSAA colour resolve, 4 samples | PS | Yes — `video.cpp:2446` | Same. |
| 35 | `resolve_msaa_color_8x.hlsl` | MSAA colour resolve, 8 samples | PS | Yes — `video.cpp:2447` | Same. |
| 36 | `resolve_msaa_depth_2x.hlsl` | MSAA depth resolve, 2 samples | PS | Yes — `video.cpp:2455` | Same. |
| 37 | `resolve_msaa_depth_4x.hlsl` | MSAA depth resolve, 4 samples | PS | Yes — `video.cpp:2458` | Same. |
| 38 | `resolve_msaa_depth_8x.hlsl` | MSAA depth resolve, 8 samples | PS | Yes — `video.cpp:2461` | Same. |
| 39 | `smaa_blend_ps.hlsl` | SMAA blend-weight calculation | PS | Created all backends; **no caller** | `shaderMain` `:19`; `ApplySMAA` `postprocess_renderer.cpp:842`. |
| 40 | `smaa_edge_detect_ps.hlsl` | SMAA edge detection (colour/luma) | PS | Created all backends; **no caller** | `shaderMain` `:23`. |
| 41 | `smaa_neighborhood_blend_ps.hlsl` | SMAA neighbourhood blending | PS | Created all backends; **no caller** | `shaderMain` `:23`. |
| 42 | `ssao_blur_ps.hlsl` | Depth-aware separable 9-tap bilateral AO blur | PS | Vulkan only | `shaderMain` `:47`; packs depth in G. |
| 43 | `ssao_composite_ps.hlsl` | Multiply AO onto scene; debug modes | PS | Vulkan only | `shaderMain` `:25`. |
| 44 | `ssao_gtao_ps.hlsl` | GTAO horizon search, depth-derived normals, blue-noise rotation | PS | Vulkan only | `shaderMain` `:203`; 4-12 dirs × 2-8 steps. |
| 45 | `ssr_composite_ps.hlsl` | Blend SSR colour by confidence × intensity | PS | Vulkan only | `shaderMain` `:26`. |
| 46 | `ssr_raytrace_ps.hlsl` | Screen-space reflection march + binary refinement + Fresnel/edge fade | PS | Vulkan only | `shaderMain` `:209`; **not actually Hi-Z** despite header comment. |
| 47 | `sunshafts_composite_ps.hlsl` | Additive sun-shaft tint composite | PS | Vulkan only | `shaderMain` `:23`. |
| 48 | `sunshafts_prepass_ps.hlsl` | Sky mask + sun disk + bright pass + horizon/edge fade | PS | Vulkan only | `shaderMain` `:57`. |
| 49 | `sunshafts_radial_ps.hlsl` | GPU Gems 3 radial scatter, 32 samples toward sun | PS | Vulkan only | `shaderMain` `:29`. |
| 50 | `taa_ps.hlsl` | TAA: YCoCg AABB clamp, motion-vector history, jitter removal | PS | Created all backends; **no caller** | `shaderMain` `:59`. |
| 51 | `vignette_ps.hlsl` | Radial smoothstep vignette, aspect/roundness correction | PS | **Never compiled/created** | No `.spirv.h`; `m_vignettePS` is only tested (`postprocess_renderer.cpp:401-406`) and never assigned. |

Include files:

| File | Purpose |
|---|---|
| `copy_common.hlsli` | Push-constant descriptor index for copy/resolve passes. |
| `gaussian_blur.hlsli` | Shared scalable Gaussian body; reads `g_offsets[2]` (`c150-c157`) and `g_weights` (`c166`) from guest constants. |
| `imgui_common.hlsli` | ImGui descriptor heap + push constants. |
| `resolve_msaa_color.hlsli` | `Texture2DMS` sample loop, `SAMPLE_COUNT` template. |
| `resolve_msaa_depth.hlsli` | Depth MSAA resolve template. |
| `smaa.hlsli` | SMAA presets, area/search LUT sampling, neighbourhood helpers (1,361 ln). |

`postprocess_reference/` copies (all are older/reference variants, **not** in `CMakeLists.txt`, not consumed by `postprocess_renderer.cpp`):

| Reference copy | Mirrors |
|---|---|
| `chromatic_aberration_ps.hlsl` | `chromatic_aberration_ps.hlsl` |
| `dof_ps.hlsl` | Single-pass DoF variant |
| `film_grain_ps.hlsl` | `film_grain_ps.hlsl` |
| `fsr1_easu_ps.hlsl` | `fsr1_easu_ps.hlsl` |
| `fsr1_rcas_ps.hlsl` | `fsr1_rcas_ps.hlsl` |
| `fullscreen_vs.hlsl` | `fullscreen_vs.hlsl` |
| `motion_blur_camera_ps.hlsl` | `motion_blur_camera_ps.hlsl` |
| `motion_blur_enhanced_ps.hlsl` | Velocity/object-blur variant (not in the shipped set) |
| `smaa_blend_ps.hlsl` | `smaa_blend_ps.hlsl` |
| `smaa_edge_detect_ps.hlsl` | `smaa_edge_detect_ps.hlsl` |
| `taa_ps.hlsl` | `taa_ps.hlsl` |

### 1.3 Build/wiring reality

- `CMakeLists.txt:1797-1828` compiles only the title/utility/AA shaders (through `fsr1_rcas_ps`). The SSAO/DoF/SSR/bloom/sun-shafts/CA/grain/MB shaders are **not regenerated by CMake**; they are consumed from checked-in `.spirv.h` files included by `postprocess_renderer.cpp:48-80`. The `.dxil.h`/`.metallib.h` counterparts are also checked in.
- `vignette_ps.hlsl` has no generated header and no `createShader` call. It is dead.
- `dof_ps.hlsl` has a `.spv` but no `.spirv.h`; the four-pass DoF is used instead.
- `postprocess_reference/` (11 files) is not in `CMakeLists.txt`; it is not linked.
- `postprocess_renderer.cpp:274-278` (D3D12) and `:293-297` (Metal) keep SSAO/DoF shader creation commented out. The only backend that creates SSAO/DoF/SSR/bloom/sun-shafts/CA/grain/MB is Vulkan (`:301-338`).
- `PostProcess::ApplyAA` is declared (`postprocess_aa.h:68`) and defined (`postprocess_aa.cpp:169`), but a tree-wide search finds no call. `Video::Present` calls only `InitializeAA` (`:2308`) and `UpdateAA` (`:3445`).
- `ApplyVignette` (`postprocess_renderer.cpp:1038`), `ApplyFilmGrain` (`:1607`), `ApplyChromaticAberration` (`:1686`) and `ApplyMotionBlur` (`:1776`) have no call in `Video::Present` (the present path calls SSAO/DoF/SSR/bloom/sun-shafts only, `:3543-3650`).

### 1.4 Deferred/forward structure and G-buffer layout

**Finding: IV is deferred in guest code; the reference host is single-RT forward. No G-buffer layout exists in the reference.**

Evidence:

- Guest device records **4 render-target slots** (`gtaiv_device.h:107`, `gtaiv_render_state.h:88-92`) and a `ShaderFlags` bit explicitly documented as `bit7=deferred` (`gtaiv_device.h:70`).
- A full `deferred_lighting` shader family exists in the shader cache: **63 entries**, including `deferred_lighting_ps4.bin` (`shader_cache.cpp:19`), `deferred_lighting_vs6.bin` (`:53`), `deferred_lighting_vs5.bin` (`:61`).
- But the host `SetRenderTarget` explicitly discards indices > 0 (`video.cpp:4278-4283`), and `SetFramebuffer` builds framebuffers with `colorAttachmentsCount = 1` (`video.cpp:4595-4596`). There is no MRT attachment array, no G-buffer format table, no normal/depth/velocity G-buffer contract in `gpu/`.
- `RenderPassType::GBuffer` (`gfx_state.h:35`) is a diagnostic enum label; `BeginRenderPass`/`GetCurrentRenderPass` (`gfx_state.cpp:308-320`) are not called from the draw path.
- Xenos 10-bit EDRAM formats are only extracted (`gtaiv_render_state.h:15-20, 45-60`); no host 10-bit/HDR render target is created.

Therefore the reference's rendering structure is **immediate-mode forward translation of each D3D9 draw to the current single RT**, plus optional fullscreen postfx. IV's deferred lighting pass cannot run on this host. Any port of IV's deferred lighting would be a new renderer, not a port.

### 1.5 Actual post-processing chain in the reference

On Vulkan, `Video::Present` runs, in order (all self-gated by `Config`):

| Order | Pass | Call | Implementation |
|---:|---|---|---|
| 1 | SSAO | `video.cpp:3543` | `ApplySSAO` `postprocess_renderer.cpp:1099`; 3 passes + composite. |
| 2 | DoF | `video.cpp:3573` | `ApplyDoF` `:1274`; prefilter → bokeh → tent → combine. |
| 3 | SSR | `video.cpp:3586` | `ApplySSR` `:1461`; raytrace + composite. |
| 4 | Bloom | `video.cpp:3607` | `ApplyBloom` `:1863`; extract → 5 downsamples → 5 upsamples → composite. |
| 5 | Sun shafts | `video.cpp:3641` | `ApplySunShafts` `:2046`; prepass → radial → composite. |
| 6 | UI | `video.cpp:3655-3662` | ImGui. |
| 7 | Present blit | `video.cpp:3834-3841` | HDR tonemap (`hdr_tonemap_ps`) or gamma (`gamma_correction_ps`). |

Native-effect suppression happens alongside: native DoF disabled when custom DoF is on (`video.cpp:3556-3562`), native EAA disabled when modern AA is on (`:3564-3571`), native bloom disabled when custom bloom is on (`:3603-3611`). Native motion blur is **not** disabled here.

On D3D12/Metal the same calls are made, but the SSAO/DoF/SSR/bloom/sun-shaft pipelines are null because their shaders were never created, so each `Apply*` returns false. The effective chain on Windows is: no custom postfx, then the present blit.

### 1.6 Adjacent evidence: GTA IV shader-cache inventory

`LibertyRecompLib/shader/shader_cache.cpp` is 45 MB, holds **1,356 entries** (`:1366`) and is consumed by `gpu/video.cpp:6621-6695`. Each entry is a hash + compressed platform blob + the original path; the blobs are opaque (no HLSL). This is the only place where IV-native effect families are visible by name.

| IV shader family | Entries / evidence | Proves |
|---|---|---|
| `deferred_lighting` | 63 entries; PS at `:19`, VS at `:53/:61` | Deferred lighting pass. |
| `rage_postfx` | 26 `rage_postfx_ps*` + 2 VS; `ps0` at `:49` | Native IV post-processing (grade/bloom/DoF/MB candidates). |
| `rage_postfx_e1.fxc` | 30 entries; first at `:14` | Native postfx effect 1. |
| `rage_postfx_e2.fxc` | 32 entries; first at `:42` | Native postfx effect 2. |
| `shadowz` | 5 entries (`:512`, `:553`) | Shadow depth pass. |
| `shadowzdir` | 5 entries (`:524`) | Directional shadow depth. |
| `shadowsmartblit` | 11 entries (`:178`) | Shadow-map filter/blit. |
| `water` | PS `:119`, VS `:367` | Water surface. |
| `water_e2.fxc` | `:921` | Water effect 2. |
| `watertex` | VS `:194`, PS `:1030` | Water texture/projection. |
| `gta_atmoscatt_clouds` | 18 entries (`:3`) | Sky/atmospheric cloud scattering. |
| `gta_rmptfx_gpurender` | `:111` | GPU particle render. |
| `gta_rmptfx_raindrops` | VS `:5`, PS `:44` | Rain particles. |
| `gta_vehicle_paint1/2/3` | 14 each; paint1 PS at `:198` | Vehicle paint shader family. |
| `gta_vehicle_vehglass` | 11 entries; PS at `:38` | Vehicle glass. |
| `gta_vehicle_lightsemissive` | 14 entries; VS at `:91` | Vehicle light emissive. |
| `gta_emissive` / `gta_emissivenight` / `gta_emissivestrong` | 18 each; `emissivenight` PS at `:47` | Emissive/night-light materials. |
| `gta_glass*` | 17 each; `glass_reflect` PS at `:183` | Glass variants. |
| `gta_reflect`, `gta_cubemap_reflect`, `gta_mirror` | 17/17/… | Reflection families. |
| `runtime_captured` | 24 entries | Captured runtime shader variants. |

This inventory is **name-level only**. The reference contains no source for these families; the import path is explicitly unfinished in `video.cpp:6687-6693` ("shader extraction/recompilation has not been implemented yet" — stale guard; the cache now exists, but it is still binary).

---

## §2 GTA IV effects catalogue

### 2.1 Legend for "IV implementation"

- `[REF]` = the reference's **custom** implementation (not IV-native; portable code).
- `[REF-CACHE]` = IV-native shader present only as an opaque binary cache entry.
- `[ABSENT]` = not found in the reference; no claim about IV behaviour is made.
- `[REF-DOC]` = documented in `LibertyRecomp` docs, not source; used only to disambiguate scope.

### 2.2 Master catalogue

| # | IV feature | Where it lives (reference) | Algorithm summary | Inputs | Output | SM3.0-portable? |
|---:|---|---|---|---|---|---|
| 1 | Deferred lighting / G-buffer | `[REF-CACHE]` `deferred_lighting_ps4.bin` `shader_cache.cpp:19`, VS `:53/:61`; guest flag `gtaiv_device.h:70`; host limitation `video.cpp:4278-4283` | Guest deferred lighting pass. **No G-buffer layout, no host lighting pass, no MRT** in reference. | Opaque guest shader constants + RTs. | Opaque. | **No** for our architecture: requires MRT G-buffer + a lighting pass; our RW 3.6 hooks expose one camera RT. SM3.0 *can* do 4 MRT, but this pipe cannot. |
| 2 | Shadow maps (directional) | `[REF-CACHE]` `shadowz` (`:512/:553`), `shadowzdir` (`:524`), `shadowsmartblit` (`:178`) | Shadow depth render + "smart blit" filter. No host implementation and no PCF/PCSS sampling; `shadow_quality.*` is only a dead parameter scaffold (kernel size / PCSS light size / bias). | Depth-only pass; opaque blobs. | Opaque shadow map. | **Partial**: SM3.0 can render depth + do manual PCF/PCSS; needs a new depth pass and sampler; see §5 P8. |
| 3 | Stencil shadow volumes | `[ABSENT]` | — | — | — | Not found. IV's cache has shadow-map families, not stencil volumes. |
| 4 | Cascaded / spot shadow maps | `[ABSENT]` for cascades. `csd_*` is **not** CSM: `csd_vs.hlsl:23-68` emits a pre-transformed screen quad, `csd_filter_ps.hlsl:19-44` samples a texture and alpha-clips. | CSD = screen-space textured quad/blit with seam clamp; no cascade matrices, no depth compare. | CSD texture + alpha threshold. | Screen-space CSD colour. | **No** as a CSM substitute; the CSD path itself is trivially portable but is not cascaded shadows. |
| 5 | Edge AA / SSAA | `[REF]` SMAA/TAA/FSR1 shaders (`smaa_*.hlsl`, `taa_ps.hlsl`, `fsr1_*.hlsl`), dispatch absent; `SSAA` only a multiplier (`postprocess_aa.cpp:300-309`); native IV EAA is guest `rage_postfx` (`video.cpp:3564-3571`) | SMAA: edge detect → blend weights → neighbourhood blend. TAA: jitter + YCoCg clamp. FSR1: EASU+RCAS. SSAA multiplier exists but no SSAA resolve path. | Colour, depth, motion, LUTs. | AA colour. | **Yes** for SMAA/FXAA; TAA partial (needs velocity); EAA not available (guest shader). |
| 6 | Bloom | `[REF]` `bloom_extract/downsample/upsample/composite_ps.hlsl`; `ApplyBloom` `postprocess_renderer.cpp:1863-2045`; wired `video.cpp:3607` | Jimenez/COD/MiniEngine: soft-knee bright pass + Karis anti-firefly, 6-mip pyramid (1/2…1/64), 13-tap downsample, 9-tap tent upsample, additive or screen composite. | HDR/LDR colour, threshold, intensity. | Bloom colour added to scene. | **Yes**. Fixed taps, no bitwise, no integer. |
| 7 | Depth of field | `[REF]` `dof_prefilter/bokeh/postfilter/combine_ps.hlsl`; `ApplyDoF` `:1274-1460`; wired `:3573` | FusionFix-style: half-res prefilter with extreme-of-4 signed CoC, 22-tap disk bokeh with FG/BG separation, 4-tap tent, full-res combine by `smoothstep` + FG alpha. | Colour + depth, focus/range, near/far, bokeh radius. | Blurred colour. | **Yes**; fixed 22 taps, no loops (our port is `shaders/ps/DOF_*.hlsl`). |
| 8 | Motion blur | `[REF]` `motion_blur_camera_ps.hlsl:65` (reprojection, 2-32 samples); `enhanced_burnout_blur_ps.hlsl:19` (title velocity blur, 4-64 samples); `ApplyMotionBlur` `:1776` | Camera: reconstruct world pos from depth, project with previous VP, bidirectional sample with depth threshold. Burnout: per-vertex velocity scaled, squared blur amount. | Colour, depth, invViewProj, prevViewProj; Burnout: velocity attributes. | Blurred colour. | **Partial**: reprojection is portable with fixed samples; needs previous VP + depth. Burnout is already SM3.0. |
| 9 | Tone mapping | `[REF]` `hdr_tonemap_ps.hlsl:98`; wired `video.cpp:3834-3841` | SDR: ACES filmic (`:80-86`) then sRGB OETF. scRGB: scale by paper-white/80 + soft knee above 1.0. HDR10: BT.709→BT.2020, scale to nits, clip, ST.2084 PQ. | Backbuffer colour, `HDRMode`, paper white, max nits. | Display-encoded colour. | **Yes**. ACES/pow are SM3.0. |
| 10 | Colour grading (IV desaturation/saturation/curve) | `[REF-CACHE]` `rage_postfx_ps0.bin` `shader_cache.cpp:49`; `rage_postfx_e1/e2` `:14/:42` | IV's actual grade is inside the opaque `rage_postfx` shader. **Algorithm not auditable.** No IV grade in readable reference source. | Opaque. | Opaque. | **Yes** as a class (our `IVGrade` already does it), **unknown** as exact IV parity. |
| 11 | Vignette | `[REF]` `vignette_ps.hlsl:25-57`; `ApplyVignette` `:1038-1097` | `dist = length(uv-center)*2`, smoothstep(radius, radius+softness), `lerp(1, vignette, intensity)`, aspect/roundness. | Colour, intensity/radius/softness/roundness. | Darkened corners. | **Yes**; simple PS. But reference never compiles/creates it. |
| 12 | Film grain / noise | `[REF]` `film_grain_ps.hlsl:127`; `ApplyFilmGrain` `:1607-1685` | Interleaved Gradient Noise + animated scroll 5.588238 px/frame, 3 octaves, film S-curve luminance response, mono/colored. | Colour, intensity, frame index, luminance scale. | Grain-added colour. | **Yes**. No bitwise; `frac`/`dot` only. |
| 13 | Sun shafts / god rays | `[REF]` `sunshafts_prepass/radial/composite_ps.hlsl`; `ApplySunShafts` `:2046-2207`; wired `:3641` | GPU Gems 3 / FusionFix: sky-mask + sun-disk prepass at half res, 32-sample radial march toward sun with decay/weight/exposure, additive tint composite. | Colour, depth, sun screen pos/colour, density/decay. | Additive shafts. | **Yes**. Fixed 32 samples. |
| 14 | Lens flare | `[ABSENT]` | — | — | — | Not found. (`doglare` in our tree is SA sun glare, unrelated.) |
| 15 | Water (reflection/refraction/SSR) | `[REF-CACHE]` `water/water_ps0.bin` `:119`, `water_vs0.bin` `:367`, `water_e2.fxc` `:921`, `watertex` `:194/:1030` | Opaque IV water shaders. No host water implementation in `gpu/`. | Opaque. | Opaque. | **Partial**: our `Water_Parallax.hlsl` `main_iv` already implements Fresnel split, refraction, depth and reflection; exact IV parity needs data. |
| 16 | Rain / wet surfaces | `[REF-CACHE]` `gta_rmptfx_raindrops` `:5/:44`; `[ABSENT]` for wet-surface shader | Rain is a GPU particle family; no readable wet-surface model. | Opaque particles. | Rain particles (opaque). | **Partial**: material-level wetness is portable; our `vehWetEnv` path exists in `VehiclePBR_Modern.hlsl:511-517`, not in the IV path. |
| 17 | Vehicle paint / specular model | `[REF-CACHE]` `gta_vehicle_paint1/2/3` (`:198` etc.), `chrome`, `rims`; readable parameters in `sps_preset_table.h:21-44` | IV-native shader is opaque. SPS says paint1-3: `SpecularColor 0.15`, `Specular 180`, `Specular2Factor 40`, `specular2ColorIntensity 1.7`; chrome/rims: `SpecularColor 0.2`, `Specular 180`. | SPS params + env map + colour. | Paint shading. | **Yes** as a class: dual-specular + Fresnel env is SM3.0; our `VehiclePaint_GTAIV.hlsl` already implements it. |
| 18 | Glass | `[REF-CACHE]` `gta_vehicle_vehglass_ps0.bin` `:38`, `gta_glass_reflect_ps4.bin` `:183`, `gta_glass_*`; SPS `sps_preset_table.h:67-70, 90-97` | Opaque IV glass shaders. SPS `vehglass`: `SpecularColor 0.15`, `Specular 180`. | Opaque. | Opaque. | **Yes** as a class; our `Glass_Vehicle.hlsl` exists (IDR 204) and is used in the Env callback (`vehiclePipe.cpp:2576`). |
| 19 | Emissive / night lights | `[REF-CACHE]` `gta_emissive*` (`:47`), `gta_vehicle_lightsemissive` (`:91`); SPS `:84-97` | `gta_emissive`, `gta_emissivenight`, `gta_emissivestrong`; no readable algorithm. | Opaque. | Opaque. | **Yes** as a class; our night prelight exists, but no IV emissive SPS path. |
| 20 | Fog (volumetric or not) | `[ABSENT]` in `gpu/`; `gta_atmoscatt_clouds` (`:3`) is sky/clouds, not fog | No fog shader or host fog pass. | — | — | **Yes**: our `HeightFog`/`DrawHeightFog` already exists (`postfx.cpp:3874`), plus SA fog. |
| 21 | Sky / clouds | `[REF-CACHE]` `gta_atmoscatt_clouds_ps7.bin` `:3` | Atmospheric cloud/scatter shader; opaque. | Opaque. | Opaque. | **Partial**: a sky model is SM3.0-able, but IV data/look is not in the reference. |
| 22 | Particles / soft particles | `[REF-CACHE]` `gta_rmptfx_gpurender` (`:111`), `gta_rmptfx_litsprite`, `gta_rmptfx_mesh`, `gta_rmptfx_raindrops` (`:5`) | Opaque GPU particle shaders; no host particle renderer. | Opaque. | Opaque. | **Partial**: soft-particle depth fade is SM3.0, but requires particle system integration. |
| 23 | SSR (general) | `[REF]` `ssr_raytrace_ps.hlsl:209`, `ssr_composite_ps.hlsl:26`; `ApplySSR` `:1461-1606`; wired `:3586` | Reconstruct view pos/normal from depth, reflect, march screen space with binary refinement, Fresnel/edge/distance confidence, composite lerp. Header says Hi-Z; code is linear march, no Hi-Z texture. | Colour, depth, optional normal, view/proj, near/far. | Reflection colour + confidence. | **Partial**: fixed 32 steps possible; needs depth+normal; no Hi-Z. |
| 24 | Ambient occlusion | `[REF]` `ssao_gtao_ps.hlsl:203`, `ssao_blur_ps.hlsl:47`, `ssao_composite_ps.hlsl:25`; `ApplySSAO` `:1099-1273`; wired `:3543` | GTAO horizon search with depth-derived normals, 4-12 directions × 2-8 steps, blue-noise rotation, bilateral 9-tap blur, multiply composite. | Depth, blue noise, camera near/far/fov, radius/intensity. | AO in R, packed depth in G; multiplied scene. | **Partial**: dynamic loop bounds must become fixed; depth texture is already available in our tree. |
| 25 | HDR / bloom chain | `[REF]` `hdr_tonemap_ps.hlsl`, `video.cpp:441-455`, `:3834-3841`; `gtaiv_render_state.h:45-60` (EDRAM) | Scene is rendered to an LDR `B8G8R8A8_UNORM` backbuffer; HDR mode only changes the **present** target to `R16G16B16A16_FLOAT` and applies ACES/PQ. No scene-referred HDR RT, no 10-bit EDRAM emulation. | Backbuffer colour. | HDR display signal. | **Partial**: present tonemap is portable; true HDR scene RT is not present in either renderer. |
| 26 | Exposure adaptation | `[ABSENT]` in `gpu/`; `hdr_tonemap` uses static paper white | No luminance measurement, no eye adaptation, no temporal exposure. | — | — | **Yes**: our `postfx_luminance.cpp:168` already does 8×8 reduce + 1×1 adapt. |
| 27 | MSAA resolve | `[REF]` `resolve_msaa_color_*`/`resolve_msaa_depth_*`; created `video.cpp:2445-2461` | Per-sample `Texture2DMS.Load` average. | MSAA surface. | Resolved colour/depth. | **N/A** for D3D9: MSAA resolve is a `StretchRect`, not a shader; `video.cpp:4126` even comments out the host MSAA sample count. |

### 2.3 Present-in-reference details (portable donor code)

**Bloom (Jimenez/COD, MiniEngine-style).** `bloom_extract_ps.hlsl:71` does a 2×2 tap, soft-knee threshold and Karis average; `bloom_downsample_ps.hlsl:40` uses 13 taps (Karis on mip 0, weights center 4 / inner 2 / outer 1 / 20); `bloom_upsample_ps.hlsl:58` uses a 9-tap 1-2-1 tent and accumulates; `bloom_composite_ps.hlsl:40` applies saturation, intensity and additive or screen blend. `ApplyBloom` runs six mips (`BLOOM_MIP_COUNT = 6`, header `postprocess_renderer.h:677`) from half-res to 1/64 (`postprocess_renderer.cpp:1863-2045`). Our tree already has the same chain in SM3.0 (`src/render/postfx_newfx.cpp:629-698`, `shaders/ps/Bloom_*.hlsl`).

**DoF (FusionFix-style).** `dof_prefilter_ps.hlsl:69` downsamples colour and selects the extreme of four signed CoC values; `dof_bokeh_ps.hlsl:72` gathers 22 disk taps with separate foreground/background weights; `dof_postfilter_ps.hlsl:34` applies a 4-tap half-texel tent; `dof_combine_ps.hlsl:57` mixes sharp and bokeh with `smoothstep(0.1,1.0,abs(coc))` plus FG alpha. Our tree has a 4-pass SM3 port with a 22-tap golden-spiral disk (`src/render/postfx_newfx.cpp:552-620`, `shaders/ps/DOF_*.hlsl`).

**GTAO.** `ssao_gtao_ps.hlsl:203` reconstructs view position from depth, derives normals from depth derivatives, does horizon search in 4-12 directions × 2-8 steps with blue-noise rotation, packs AO in R and linearized depth in G. `ssao_blur_ps.hlsl:47` is a depth-aware 9-tap bilateral blur; `ssao_composite_ps.hlsl:25` multiplies the scene. The reference marks this a stub on non-Vulkan (`postprocess_renderer.cpp:1117`, `:1475`).

**SSR.** `ssr_raytrace_ps.hlsl:209` reconstructs view position/normal from depth, reflects, marches screen space, does four binary-refinement iterations, and writes `float4(reflectedColor, confidence)`. `ssr_composite_ps.hlsl:26` lerps by `confidence * intensity`. It is **not** Hi-Z despite the header comment. It needs depth + optional normal; our tree has `g_ssaoDepthTex` and a normal buffer, so a fixed-step SM3 port is feasible.

**Sun shafts.** `sunshafts_prepass_ps.hlsl:57` masks sky (`depth >= 0.9999`), bright pixels and the sun disk; `sunshafts_radial_ps.hlsl:29` marches 32 samples toward the sun with `decay`/`weight`/`exposure`; `sunshafts_composite_ps.hlsl:23` adds the tinted shafts. Our tree has a single-pass 24-sample port (`shaders/ps/SunShafts.hlsl`, `postfx_newfx.cpp:517-547`) and a separate `DrawGodRays` (`postfx.cpp:1946`).

**Motion blur.** `motion_blur_camera_ps.hlsl:65` reconstructs world position from depth, projects with `prevViewProj`, computes bidirectional samples with a depth threshold. `enhanced_burnout_blur_ps.hlsl:19` is the title's per-vertex-velocity blur. Neither is dispatched in `Video::Present`.

**Tonemap.** `hdr_tonemap_ps.hlsl:80-86` defines Reinhard and ACES; the SDR path uses ACES. `gamma_correction_ps.hlsl:23` is `pow(color, gamma)`. `video.cpp:3770-3800` sets gamma 0.85 adjusted by brightness for the SDR path.

**Vignette / grain / CA.** All three are simple, portable PS implementations (`vignette_ps.hlsl:25`, `film_grain_ps.hlsl:127`, `chromatic_aberration_ps.hlsl:36`), but none has a caller; vignette is never even compiled.

### 2.4 Explicitly absent from the reference

The following were searched in `gpu/` and are **not implemented** (binary cache names do not count as implementation): stencil shadow volumes; cascaded shadow maps; any G-buffer layout; any host shadow-map renderer; any water renderer; any wet-surface shader; any readable vehicle paint/glass/emissive algorithm; any sky renderer; any particle renderer; any lens flare; any exposure adaptation; any readable IV colour-grade formula; any general SSR wired on D3D12/Metal.

The following exist only as opaque blobs in `LibertyRecompLib/shader/shader_cache.cpp` (see §1.6): `deferred_lighting`, `rage_postfx`/`_e1`/`_e2`, `shadowz`/`shadowzdir`/`shadowsmartblit`, `water`/`water_e2`/`watertex`, `gta_atmoscatt_clouds`, `gta_rmptfx_*`, `gta_vehicle_*`, `gta_glass*`, `gta_emissive*`. **Marked "not auditable" rather than guessed.**

---

## §3 Our current GTA IV mode

### 3.1 What `MODE_GTAIV` owns today

`g_modeProfiles` is indexed by `ModeId` (`src/core/modeManager.cpp:527-537`); the GTA IV entry is `{ MODE_GTAIV, "GTA IV", "GTAIV", kValuesGTAIV, MV_COUNT(kValuesGTAIV) }` (`:532`). The profile is applied by rebuilding from the pre-mode snapshot (`:669-690`), so **every field not listed below keeps its pre-mode value**.

`kValuesGTAIV` (`src/core/modeManager.cpp:346-395`) owns exactly these 35 fields:

| Group | Fields |
|---|---|
| Pipeline identity | `pipeline=PIPELINE_GTAIV`, `buildingPipe=BUILDING_GTAIV`, `vehiclePipe=CAR_GTAIV`, `colorFilter=COLORFILTER_GTAIV`, `preset=PRESET_IV_PC`, `ivMode=1` |
| Building | `ps2ModulateBuilding=0`, `detailMaps=0`, `stochastic=0`, `dualPassBuilding=0` |
| Vehicle / env | `dualPassVehicle=0`, `envMapSize=256`, `envMapFarClipMult=1.0` |
| PostFX | `radiosity=0`, `doRadiosity=0`, `grainFilter=0`, `infraredVision=0`, `nightVision=0`, `ssaoEnable=0`, `smaaEnable=0`, `vcsTrails=0` |
| IV grade (only 2) | `ivSaturation=0.0`, `ivCurves=0.0` |
| Alpha/state | `ps2ModulateGlobal=0`, `ps2ModulateGrass=0`, `dualPassGlobal=0`, `dualPassPed=0`, `dualPassGrass=0`, `dualPassDefault=0` |
| Safety | `stateBaselineEnable=1`, `texAlphaClassify=1`, `pedOpaqueContract=1`, `defaultCbShaderHygiene=1` |
| Screen extras | `doglare=0`, `neoWaterDrops=0` |

**Deliberately not owned** (`src/core/modeManager.cpp:341-344`): `ivDesaturation`, `ivGamma`, `ivVignetteIntensity/Radius/Contrast`, `ivBloomIntensity`, `ivExposure`, `coronaZtest`. **Also not owned** (and therefore inherited): every new postfx gate (`bloomEnable`, `dofEnable`, `sunShaftsEnable`, `caIntensity`, `grainIntensity`, `vignetteIntensity`, `fxaaEnable`), `heightFogEnable`, `godRaysEnable`, `motionBlurEnable`, SSAO sub-params, `waterStyle`/`waterQuality`, tonemap fields, `forwardPlusEnable`, `normalBufferEnable`, `velocityBufferEnable`, `depthHookEnable`.

`ivSaturation` and `ivCurves` are set to 0 here, but **nothing reads them** outside `main.cpp`/`modeManager.cpp`/`debugmenu_ui.cpp`; `custom_mode.cpp:134-135` writes them but no render pass consumes them. They are dead knobs today.

`iv_mode.cpp` is a legacy preset struct (`src/render/iv_mode.cpp:41-60`) whose `IVMode_ApplyDefaults` only raises `c->ivMode=1` and logs that ModeManager now owns the settings (`:62-73`). It is not the mode authority.

### 3.2 `CAR_GTAIV` — what the callback actually does

- Dispatch: `vehiclePipe.cpp:3290-3298` sends `CAR_GTAIV` to `CCustomCarEnvMapPipeline__CustomPipeRenderCB_PS2`, guarded only by `!UG_mod`.
- Inside the PS2 callback, the IV swap is gated by `config->ivMode || config->vehiclePipe == CAR_GTAIV` (`vehiclePipe.cpp:987`). It binds one of three pairs:
  1. **Preferred (non-FP):** `vehiclePBRVS` + `gtaivVehiclePS` (`:998-1002`), with `c21=0` to kill the PBR `COLOR1` env term (`:1003-1005`), `c30-c33` world and `c34` eye (`:1014-1026`), and `matCol` RGB halved (`:1032-1042`).
  2. **Forward+ / fallback:** frozen `gtaivFPVS/gtaivFPPS` or `gtaivVehicleVS/gtaivVehiclePS` (`:1050-1075`), with identity env matrix and zero `fxParams`.
  3. **Non-IV:** `vehiclePipeVS` + `simplePS` (`:1076-1081`).
- The frozen IV PS is documented in-tree as disassembled: it reads **no constants**, derives UVs from the normal, and computes `out = v2.w*(env*v2 - d) + 2*d` (RGB doubled), alpha = `d.a` (`vehiclePipe.cpp:965-973`). Sources were deleted; `GTAIVVehicle_ps.cso` and `GTAIVVehicle_vs.cso` are the binaries under `shaders/ps|vs/`. The PS's doubling is compensated by halving `matCol` (`:1039-1041`).
- After the base draw, the normal MatFX env/spec pass still runs: `hasEnv1/hasEnv2` bind `s1`, `hasSpec` binds `s2`, then `ps2CarFxVS` + `ps2EnvSpecFxPS` with `REG_fxParams`/`REG_envXform` (`vehiclePipe.cpp:1086-1170`). IV mode does **not** use the PBR `main()` / GGX path, `Glass_Vehicle`, `VehiclePaint_GTAIV`, or `CarPaint_Reflections`.

Net effect: `CAR_GTAIV` is **PS2-era forward shading with a frozen IV pixel shader and the SA env/spec FX pass**, not the IV material model.

### 3.3 `BUILDING_GTAIV` — what the callback actually does

- Switch: `buildingPipe.cpp:2697-2698` maps `BUILDING_GTAIV` to the PS2 callback with an IV shader swap.
- Gate: `wantIVBuilding = config->ivMode || config->buildingPipe == BUILDING_GTAIV` (`buildingPipe.cpp:1094`); `useIVFP` if `forwardPlusEnable && ivFPReady()` (`:1095`); `haveIVPair` requires both handles (`:1096`).
- Bind: if `wantIVBuilding && haveIVPair`, bind `gtaivFPVS/gtaivFPPS` or `gtaivBuildingVS/gtaivBuildingPS` (`:1224-1235`), then force PS `c0 = {0.5,0,0,0}` (`:1236-1244`) and bind `s1` to the diffuse texture for a defined env sample (`:1245-1252`).
- The rebuilt VS (`shaders/vs/GTAIVBuilding_vs.hlsl`) documents the contract it feeds the CSO: `TEXCOORD0=diffuse UV`, `TEXCOORD1=normal` (PS env UV = `n.xy*0.5+0.5`), `COLOR0=lit prelight`, `COLOR1.a=env lerp weight` (forced 0, so the IV PS collapses to `tex*color`) (`GTAIVBuilding_vs.hlsl:20-42, 100-140`). Lighting is PS2/Xbox-equivalent prelight + ambient, not the 7-light PBR sum (`:33-38`).
- `BUILDING_GTAIV` does **not** use `buildingPBRVS`/`buildingPBRPS` (`main_building`), normal maps, detail maps, stochastic tiling, or the PBR env path.

Net effect: IV buildings are **PS2 forward prelight + a frozen IV building pixel shader**, with the env term deliberately disabled; visuals lean on the SA timecycle and the PS2 texture/alpha path.

### 3.4 What the IV grade does in `src/render/postfx_colourfilter.cpp`

Two separate things exist:

**(a) `COLORFILTER_GTAIV` is a bypass, not a filter.** In `ColourFilter_switch`, case `COLORFILTER_GTAIV` (`src/render/postfx.cpp:1842-1850`) only unbinds PS/VS and textures 0/1 and `break`s. It does not call `ColourFilter_Modern`, does not tonemap, does not grade, does not re-composite. The comment says: *"Bypass mode - no color filter applied. GTAIV filter removed - we rely on SA's own timecycle/carcols values."*

**(b) `DrawIVGrade` is an overlay gated by `config->ivMode`, not by the colour filter.** It runs in both the normal path (`postfx.cpp:1895-1908`) and the `colorFilterEnable=0` bypass path (`:1757-1760`), after the lens group (CA→grain→vignette) and before motion blur/height fog/god rays. Gate: `if(!config || !config->ivMode) return;` (`postfx_colourfilter.cpp:1378`).

The grade shader is an **opaque embedded ps_3_0 blob**: `g_ivGradePSBytes[]` (`postfx_colourfilter.cpp:1084-1316`), created by `EnsureIVGradePS` (`:1332-1352`) with no `.hlsl` source and no `IDR_*`. Its documented register contract (`:1077-1083`):

```
c0 = {exposure, desaturation, gamma, 0}
c1 = {vignetteIntensity, vignetteRadius, vignetteContrast, bloomIntensity}
c2 = {1/screenW, 1/screenH, 0, 0}
s0 = scene (private camera-sized frame copy)
```

CPU upload (`postfx_colourfilter.cpp:1478-1517`):

- `ex = (ivExposure > 0.01) ? ivExposure : 1`, capped to `s_ivExpCap = 1.5` **only if `ColourFilter_Modern` ran its tonemap this frame** (`:1492-1503`). In IV mode `COLORFILTER_GTAIV` does not run CF_Modern, so the raw slider (default 2.5) is used.
- `gm = (ivGamma > 0.05) ? ivGamma : 1`.
- `c1 = {ivVignetteIntensity, ivVignetteRadius, ivVignetteContrast, ivBloomIntensity}`.
- `c2 = {1/w, 1/h, 0, 0}`.
- Input is a private camera-sized copy `s_ivGradeRas`, not the padded front buffer, to avoid the IV black-band bug (`:1322-1331`, `:1438-1462`). Output is the camera raster via the colourfilter quad; `UpdateFrontBuffer()` resyncs at the end (`:1544`).

Verified fragments from the blob (constants + data flow; the blob is opaque and there is no source):

- Rec.601 luma constants `0.299/0.587/0.114` plus `0.001` epsilon (`def c4`).
- Desaturation is `saturate(0.5 * ivDesaturation)` (comment in `src/core/main.cpp:5357` and `_backup/src/render/iv_mode.cpp:94-99`: *"1.0 maps to 50% grey wash"*).
- Four-tap neighbourhood bloom at ±4-texel offsets: sum of 4 taps × `0.25` − `0.75`, clamped at 0, × `ivBloomIntensity` (c6/c1.w; offsets c2 × 4).
- Radial vignette: `r = length(uv-0.5)`, `d = saturate(r / ivVignetteRadius)`, smoothstep/`3−2d` falloff, `pow(..., max(ivVignetteContrast,0.01))`, mixed by `ivVignetteIntensity`.
- Gamma branch: `pow(max(color,0), 1/gamma)` is only taken for `gamma > ~1.0`; default 1.0 leaves the frame linear.
- Output alpha = 1.0.

Because IV mode bypasses CF_Modern, **there is no sRGB encode and no tonemap** on this path; the IV-grade exposure multiply is applied to the LDR PS2/IV shader output. That is a deliberate LDR path, but it means the `ivExposure=2.5` default is a raw 2.5× gain, not a scene-referred exposure stop.

---

## §4 Gap matrix

Legend: **HAVE** = implemented and active in `MODE_GTAIV` today. **PARTIAL** = code exists but is off/incomplete/different algorithm (delta stated). **MISSING** = no implementation. **N/A-SM2-excluded** = not applicable to the SM2/SM3-class D3D9 pipeline: requires SM4+/compute, UAVs, or features ps_3_0 cannot express (e.g. `Texture2DMS` sampling).
Each verdict cell begins with exactly one of those four labels; text after the em dash is the delta, not a second verdict.
Rows are ordered by visual impact for a player who selects "GTA IV mode".

| # | IV feature | IV implementation | ours | Verdict |
|---:|---|---|---|---|
| 1 | IV colour grade (desaturation/saturation/curve) | `[REF-CACHE]` `rage_postfx` binary; algorithm not auditable | `DrawIVGrade` embedded PS (`postfx_colourfilter.cpp:1084-1316`, `:1354-1547`) with exposure/desat/gamma/vignette/4-tap bloom. `ivSaturation`/`ivCurves` are dead. No timecycle tint, no tonemap. | **PARTIAL** — the grade exists but is an opaque approximation; saturation/curves not wired; runs on a linear LDR frame with raw 2.5× exposure. |
| 2 | Vehicle paint / specular model | `[REF-CACHE]` `gta_vehicle_paint1/2/3`; SPS params (`sps_preset_table.h:26-44`) | `CAR_GTAIV` uses frozen IV PS + PS2 env/spec FX (`vehiclePipe.cpp:987-1170`). `VehiclePaint_GTAIV.hlsl` (IDR 200) is compiled but never bound. | **PARTIAL** — IV shader pair is present but not the IV material model; the matching dual-spec shader is dead. |
| 3 | Glass | `[REF-CACHE]` `gta_vehicle_vehglass`, `gta_glass_*`; SPS `:67-70,90-97` | IV path uses frozen IV PS/PS2 FX. `Glass_Vehicle.hlsl` (IDR 204) is used only by the Env/PBR callback (`vehiclePipe.cpp:2576`), not by `CAR_GTAIV`. | **PARTIAL** — glass shading exists, wrong path for IV. |
| 4 | Bloom | `[REF]` Jimenez 4-pass pyramid (`postprocess_renderer.cpp:1863-2045`) | `src/render/postfx_newfx.cpp:629-698` + `shaders/ps/Bloom_*.hlsl`; profile `bloomEnable` is **not set** (default 0), so off. `DrawIVGrade` adds a cheap 4-tap bloom (`ivBloomIntensity`, default 0.05). | **PARTIAL** — full bloom port exists but is off; only the cheap grade bloom is active. |
| 5 | Directional shadow maps | `[REF-CACHE]` `shadowz`/`shadowzdir`/`shadowsmartblit`; no host renderer | SA `stencilShadows` toggle (not owned by the profile); no shadow-map pass; `shadow_quality.*` is dead. | **MISSING** — IV's shadow-map look is absent; SA stencil shadows are a different feature. |
| 6 | Sun shafts / god rays | `[REF]` 3-pass GPU Gems 3 (`postprocess_renderer.cpp:2046-2207`) | `postfx_newfx.cpp:517-547` + `shaders/ps/SunShafts.hlsl` (24 samples, single pass); `sunShaftsEnable` default 0 and not set by the profile. Separate `DrawGodRays` (`postfx.cpp:1946`) also exists. | **PARTIAL** — port exists but off; single-pass vs 3-pass prepass. |
| 7 | Vignette | `[REF]` `vignette_ps.hlsl`; never compiled | Active via `DrawIVGrade` (`ivVignetteIntensity` default 0.15 from the ULTRA base), plus standalone `Vignette.hlsl`/`DrawVignette` (`postfx_newfx.cpp:767-785`) which is off. | **PARTIAL** — vignette is applied, but by the grade's formula, not the reference's standalone pass. |
| 8 | Film grain | `[REF]` IGN grain (`film_grain_ps.hlsl:127`); no caller | `Grain.hlsl` + `DrawFilmGrain` (`postfx_newfx.cpp:737-763`); `grainIntensity` default 0, `grainFilter=0` in the profile. | **PARTIAL** — TAA-safe grain port exists but is off. |
| 9 | CA | `[REF]` `chromatic_aberration_ps.hlsl`; no caller | `CA.hlsl` + `DrawChromaticAberration` (`postfx_newfx.cpp:703-733`); `caIntensity` default 0. | **PARTIAL** — port exists but off. |
| 10 | Water (reflection/refraction/SSR) | `[REF-CACHE]` `water`/`water_e2`/`watertex`; no host renderer | `Water_Parallax.hlsl` `main_iv` + `Water_IV.cso` (IDR 241); `waterPipe.cpp:50-56, 1271-1284`; `waterStyle` not owned by the profile. | **PARTIAL** — IV water exists but the profile does not select it; exact IV parity not proven. |
| 11 | Tone mapping | `[REF]` ACES/PQ present (`hdr_tonemap_ps.hlsl`) | `ColourFilter_Modern` tonemap/LUT (`postfx_colourfilter.cpp:591-630`) + `TonemapLUT.hlsl`; `COLORFILTER_GTAIV` bypasses it. | **MISSING** — no tonemap/sRGB encode on the IV path. |
| 12 | Exposure adaptation | `[ABSENT]` in reference | `postfx_luminance.cpp:168` (8×8 reduce + 1×1 temporal adapt); only consumed by `ColourFilter_Modern`, which IV bypasses. | **MISSING** — auto-exposure exists but is not on the IV path. |
| 13 | DOF | `[REF]` 4-pass FusionFix (`postprocess_renderer.cpp:1274-1460`) | `postfx_newfx.cpp:552-620` + `DOF_*.hlsl`; `dofEnable` default 0 and not set by the profile. | **PARTIAL** — full port exists but off. |
| 14 | SSAO | `[REF]` GTAO (`postprocess_renderer.cpp:1099-1273`) | `postfx_ssao.cpp:518` / overhaul `:706`; profile sets `ssaoEnable=0`. | **PARTIAL** — SSAO exists but is disabled in IV mode. |
| 15 | Edge AA / SSAA | `[REF]` SMAA/TAA/FSR1 code, dispatch absent; native EAA guest `rage_postfx` | `DrawSMAA` (`postfx_smaa.cpp:441`), FXAA3 (`postfx_newfx.cpp:789-823`); profile sets `smaaEnable=0`, `fxaaEnable` inherited. No EAA. | **PARTIAL** — SMAA/FXAA available but off; no IV EAA. |
| 16 | Motion blur | `[REF]` camera reprojection (`motion_blur_camera_ps.hlsl`), Burnout title blur; no caller | `DrawMotionBlur` (`postfx.cpp:3605`) + velocity buffer (`postfx_motionblur.cpp:23`); `motionBlurEnable` not owned; algorithm is Burnout-style, not reprojection. | **PARTIAL** — radial MB exists; no camera reprojection. |
| 17 | Sky / clouds | `[REF-CACHE]` `gta_atmoscatt_clouds` | SA sky/timecycle path (`src/render/iv_mode.cpp:7-8` says IV relies on SA timecycle); no IV cloud shader. | **PARTIAL** — uses SA sky, not IV's atmospheric cloud shader. |
| 18 | Emissive / night lights | `[REF-CACHE]` `gta_emissive*`, `gta_vehicle_lightsemissive`; SPS `:84-97` | SA day/night prelight (building VS `GTAIVBuilding_vs.hlsl:100-140`; vehicle PS2 path); no IV emissive SPS model. | **PARTIAL** — prelight exists, IV emissive model absent. |
| 19 | Rain / wet surfaces | `[REF-CACHE]` `gta_rmptfx_raindrops`; no wet-surface shader | `vehWetEnv` + `WetRoads` in `VehiclePBR_Modern.hlsl:511-517` (Modern only); `neoWaterDrops=0` in the IV profile. | **PARTIAL** — wet response exists only on the Modern path; IV path dry. |
| 20 | Fog | `[ABSENT]` in `gpu/` | `DrawHeightFog` (`postfx.cpp:3874`, `heightFogEnable`), plus SA fog; not owned by the profile. | **PARTIAL** — height fog exists but off; no volumetric IV fog evidence. |
| 21 | SSR (general) | `[REF]` screen-space march (`ssr_raytrace_ps.hlsl:209`), Vulkan only | Water-only screen-space reflection (`Water_Parallax.hlsl` s3, `waterPipe.cpp:88-101`); no general SSR pass. | **PARTIAL** — water SSR only. |
| 22 | Lens flare | `[ABSENT]` | SA sun glare (`doglare`) disabled by the IV profile (`modeManager.cpp:393`); no IV flare. | **MISSING** — no lens flare. |
| 23 | Particles / soft particles | `[REF-CACHE]` `gta_rmptfx_*` | SA particle system; no soft-particle depth fade in our postfx. | **MISSING** — no IV particle model / soft particles. |
| 24 | Deferred lighting / G-buffer | `[REF-CACHE]` `deferred_lighting`; host MRT unsupported (`video.cpp:4278-4283`) | Forward `vehiclePBRVS`/`buildingPBRVS` + per-object PBR/PS2; no G-buffer. | **MISSING** — architecture-level; not a shader port. |
| 25 | HDR / bloom chain | `[REF]` LDR scene + HDR present (`video.cpp:441-455, 3834-3841`) | `TonemapPass`/`TonemapLUT` + `postfx_luminance` (Modern only); no HDR scene RT. IV path is LDR. | **PARTIAL** — present tonemap exists; no scene-referred HDR; IV bypasses it. |
| 26 | Stencil shadow volumes | `[ABSENT]` (IV cache shows shadow maps) | SA `stencilShadows` toggle exists but is not owned/enabled by IV; no IV stencil volumes. | **MISSING** — not IV's mechanism; not found in reference. |
| 27 | Cascaded shadow maps | `[ABSENT]`; `csd_*` is a screen-space alpha-tested quad (`csd_vs.hlsl:23-68`, `csd_filter_ps.hlsl:19-44`) | No CSM. | **MISSING** — `csd` is not CSM. |
| 28 | MSAA / resolve | `[REF]` resolve shaders, host MSAA sample count commented out (`video.cpp:4126`) | D3D9 resolves via `StretchRect`; `MODE_GTAIV` has no custom MSAA shader path. | **N/A-SM2-excluded** for a shader port; SM3.0 cannot sample `Texture2DMS`; D3D9 resolves via `StretchRect`. |

### 4.1 Verdict roll-up

- **HAVE:** 0 (no IV feature is fully active and parity-complete today).
- **PARTIAL:** 19.
- **MISSING:** 8.
- **N/A-SM2-excluded:** 1.

### 4.2 Top five gaps by visual impact

1. **IV grade is an opaque approximation with a linear LDR frame and raw 2.5× exposure.** It is the strongest identity cue and the least faithful implementation.
2. **Vehicle paint/glass use the frozen IV PS + SA env FX, not the IV material model.** The matching `VehiclePaint_GTAIV` shader is dead code.
3. **The entire atmosphere stack (bloom, god rays, vignette, grain, CA) is present but switched off in `MODE_GTAIV`**, while the grade adds a cheap 4-tap bloom instead.
4. **Shadow maps are absent.** IV's cache has `shadowz`/`shadowzdir`/`shadowsmartblit`; we have SA stencil shadows.
5. **IV water is implemented but not selected by the profile** (`waterStyle` is not owned).

---

## §5 Port plan

### 5.1 Prioritisation (visual impact ÷ risk)

| Priority | Item | Impact (1-5) | Risk | Ratio | New CSO? |
|---:|---|---:|---|---:|---|
| P1 | Wire existing IV postfx into `MODE_GTAIV` | 5 | Low (2) | 2.5 | None |
| P2 | `IVGrade` v2 (HLSL + saturation/curves + correct exposure) | 5 | Medium (3) | 1.67 | Yes (IDR 281) |
| P3 | IV vehicle paint/glass model | 4 | Medium (2) | 2.0 | Reuse 200/204 |
| P4 | SSAO in IV mode | 3 | Low (2) | 1.5 | None |
| P5 | IV water style in profile + reflection | 3 | Low (1) | 3.0 | None |
| P6 | AA: SMAA/FXAA in IV profile | 3 | Low (1) | 3.0 | None |
| P7 | Tonemap + exposure for the IV path | 4 | High (4) | 1.0 | None (reuse 166/269/253/254) |
| P8 | Shadow maps + PCF/PCSS | 4 | High (5) | 0.8 | Yes (IDR 283+) |
| P9 | Camera motion blur (reprojection) | 2 | Medium (3) | 0.67 | Optional (IDR 282) |
| P10 | Lens flare / sun glare | 2 | Low (2) | 1.0 | Yes (IDR 286) |
| P11 | General SSR | 2 | High (4) | 0.5 | Yes (IDR 284-285) |
| P12 | Height fog in IV profile | 2 | Low (1) | 2.0 | None |
| P13 | Soft particles / rain | 2 | High (5) | 0.4 | Yes (IDR 287) |
| P14 | Deferred lighting / G-buffer | 4 | Prohibitive (6) | 0.67 | Reject |
| P15 | IV sky/clouds | 2 | High (4) | 0.5 | Reject without data |

The `P1…P15` numbers are the recommended execution order, not a strict sort of the Ratio column: identity-defining grade and paint work is intentionally promoted above some cheaper wiring items. The Ratio column is the raw impact ÷ risk.

### 5.2 Detailed plans

#### P1 — Wire the existing postfx stack into `MODE_GTAIV` (highest ROI)

- **Files:** `src/core/modeManager.cpp` (`kValuesGTAIV`, `:346-395`); `src/render/postfx_newfx.cpp` (gates `:517-823`); `src/render/postfx_ssao.cpp` (`:518`, `:706`); `src/render/postfx_smaa.cpp` (`:441`); `src/render/waterPipe.cpp` (`:50-56`, `:1271-1284`).
- **Algorithm to adopt:** existing SM3 ports — `Bloom_Extract/Downsample/Upsample/Composite`, `DOF_CoC/Bokeh/Tent/Combine`, `SunShafts`, `CA`, `Grain`, `Vignette`, `FXAA3`, `SSAO`, `SMAA`.
- **Suggested IV-tuned values (initial, A/B):** `bloomEnable=1`, `bloomQuality=2`, `bloomThreshold=0.90`, `bloomIntensity=0.35`; `sunShaftsEnable=1`, `sunShaftsDensity=0.84`, `sunShaftsDecay=0.97`; `vignetteIntensity=0.20`, `vignetteRadius=0.70`; `grainIntensity=0.03`; `caIntensity=0.12`; `ssaoEnable=1`, `ssaoRadius=1.0`, `ssaoPower=2.0`, `ssaoSampleCount=8`; `smaaEnable=1` (or `fxaaEnable=1` for cost); `waterStyle=1`, `waterQuality=3`; `heightFogEnable=1`.
- **Critical cleanup:** set `ivBloomIntensity=0` and `ivVignetteIntensity=0` in the IV profile when P1 is on, otherwise `DrawIVGrade` double-blooms and double-vignettes. (`DrawIVGrade` reads those in c1, `postfx_colourfilter.cpp:1508-1513`.) This deliberately changes the current "profile does not own iv* sliders" decision (`src/core/modeManager.cpp:341-344`); document it as an explicit IV preset choice, not a global default.
- **SM3.0 constraints:** all passes are already ps_3_0; no new limits.
- **New CSO/IDR:** none.
- **Sampler/constant slots:** unchanged (each fullscreen pass uses its own `c0-c3`/`s0-s1`; see §6).
- **INI kill switches:** existing `bloomEnable`, `dofEnable`, `sunShaftsEnable`, `caIntensity`, `grainIntensity`, `vignetteIntensity`, `ssaoEnable`, `smaaEnable`, `fxaaEnable`, `heightFogEnable`, `waterStyle`, `waterQuality`.
- **Risk:** Low. Main risks: LDR bloom threshold (must be <1.0 because the IV path is LDR), double-grade bloom/vignette, and SSAO perf (quarter-res path preferred).
- **Impact:** 5.

#### P2 — `IVGrade` v2: source it, finish it, put it in the right place

- **Files:** new `shaders/ps/IVGrade.hlsl`; `src/render/postfx_colourfilter.cpp` (`:1077-1547`); `resources/resource.h`/`Resource.rc`; `tools/fast_build.py`; `src/render/pipelinecommon.cpp` (`CreateShaders`, `:873` region).
- **Algorithm to adopt:** keep the verified structure — exposure multiply, Rec.601 luma, `saturate(0.5*ivDesaturation)` desaturation, conditional `pow(1/gamma)`, 4-tap neighbourhood bloom with a 0.75 threshold, radial smoothstep vignette — then add:
  - `ivSaturation`: `lerp(float3(luma), color, ivSaturation)` after desaturation.
  - `ivCurves`: a smooth `x*x*(3-2x)` S-curve (or a filmic toe/shoulder) blended by `ivCurves`.
  - Correct exposure: use `ivExposure` only when no tonemap ran; when CF_Modern/P3 runs, force `exposure=1` and `gamma=1` or remove those stages from the grade.
  - Optional timecycle tint: pass a tint in `c4` from `Colorcycle` if a timecycle-driven IV look is desired; keep it neutral by default.
- **New register contract:** `c0={exposure,desat,gamma,0}`, `c1={vigInt,vigRadius,vigContrast,bloomInt}`, `c2={1/w,1/h,0,0}`, `c3={saturation,curves,curveMid,flags}`, `c4={tintR,tintG,tintB,strength}`; `s0=scene`, `s1=bloom pyramid` (optional).
- **SM3.0 constraints:** ps_3_0, ≤64 instructions, no bitwise, fixed taps, guard all divides with `1e-7`. The current blob already fits.
- **New CSO/IDR:** `IVGrade.cso`, **IDR 281** (first free after the 279/280 reservation; see §6.6).
- **INI kill switch:** new `ivGradeEnable` (default 1); existing `ivDesaturation`, `ivSaturation`, `ivCurves`, `ivGamma`, `ivExposure`, `ivVignette*`, `ivBloomIntensity`.
- **Risk:** Medium. Look change; must define ordering against tonemap; must not double-grade when `ColourFilter_Modern` runs (e.g. PBR force). Add an A/B screenshot gate.
- **Impact:** 5.

#### P3 — IV vehicle paint and glass

- **Files:** `shaders/ps/VehiclePaint_GTAIV.hlsl` (exists, IDR 200), `shaders/ps/Glass_Vehicle.hlsl` (IDR 204), `src/render/vehiclePipe.cpp` (`CAR_GTAIV` path `:987-1075`, `:3290-3298`), `resources/resource.h` (reuse 200/204).
- **Algorithm to adopt:** dual-specular paint with `Specular 180`, `Specular2Factor 40`, `SpecularColor 0.15/0.2`, `specular2ColorIntensity 1.7` from `sps_preset_table.h:21-44`; Fresnel env reflection; clear-coat/dirt layers as in `VehiclePaint_GTAIV.hlsl`. Glass: `Glass_Vehicle` tint/opacity + env reflection.
- **Contract fix required:** `VehiclePaint_GTAIV.hlsl` expects `TEXCOORD1=normal`, `TEXCOORD2=viewDir`, `TEXCOORD3=lightDir` (`:31-34`), while `vehiclePBRVS` emits `TEXCOORD1=WorldNormal`, `TEXCOORD2=WorldPos`, `TEXCOORD3=ViewDir`, `TEXCOORD4=SunDir` (`shaders/vs/buildingPBRVS.hlsl:56-61`). Either add a small IV VS entry or change the PS input mapping. Do not bind as-is.
- **SM3.0 constraints:** already ps_3_0; no new limits.
- **New CSO/IDR:** none — reuse `IDR_VEHICLEPAINT_GTAIV 200` and `IDR_GLASS_VEHICLE 204`. Recompile the existing CSOs in place.
- **Slots:** the paint/glass passes have their own `c0-c4`/`s0-s3`; they do **not** collide with PBR `c22/c23`/`s0-s8` because they are separate draws. If folded into `main_vehiclePBR`, use `c49+` and `s9+` (see §6).
- **INI kill switch:** new `ivPaintModel` (0/1), `ivGlassModel` (0/1).
- **Risk:** Medium. `VehiclePaint_GTAIV` is untested/dead; needs per-material SPS lookup and an env-map source. Start with paint only, keep glass on the existing path until A/B.
- **Impact:** 4.

#### P4 — SSAO in IV mode

- **Files:** `src/render/postfx_ssao.cpp` (`DrawSSAO` `:518`, `DrawSSAO_Overhaul` `:706`), `src/core/modeManager.cpp`.
- **Algorithm:** existing SSAO; prefer the quarter-res temporal + bilateral path (`DrawSSAO_Overhaul`) at low intensity, using `g_ssaoDepthTex` (INTZ, `postfx_ssao.cpp:6, 48-49`).
- **SM3.0:** existing; fixed taps. No new limits.
- **New CSO/IDR:** none.
- **Slots:** existing SSAO constants/samplers (c0-c? / s0-s2); no PBR collision.
- **INI:** `ssaoEnable`, `ssaoRadius`, `ssaoPower`, `ssaoSampleCount`, `ssaoTemporalEnable`, `ssaoBlurPasses`.
- **Risk:** Low-Medium (perf; depth-hook dependency).
- **Impact:** 3.

#### P5 — IV water in the profile

- **Files:** `src/core/modeManager.cpp` (add `waterStyle=1`, `waterQuality=3`); `src/render/waterPipe.cpp` (`:50-56` style selection, `:1271-1284` IV preset); `shaders/ps/Water_Parallax.hlsl` (`main_iv`).
- **Algorithm:** existing IV core — Fresnel split, depth thickness/refraction, dual scrolling normals, foam/shore, timecycle tint, s3 screen-space reflection copy (`Water_Parallax.hlsl:1-120`, `waterPipe.cpp:88-101`).
- **SM3.0:** existing ps_3_0; c0-c15 + s0-s3 in its own draw. No PBR collision.
- **New CSO/IDR:** none (`IDR_WATER_IV 241`).
- **INI:** `waterStyle`, `waterQuality`, `waterFresnelSplit` (existing).
- **Risk:** Low (already implemented; profile wiring).
- **Impact:** 3.

#### P6 — AA in IV profile

- **Files:** `src/core/modeManager.cpp`, `src/render/postfx_smaa.cpp:441`, `src/render/postfx_newfx.cpp:789-823`.
- **Algorithm:** SMAA (3 passes) for quality, or FXAA3 for cost. IV's native EAA is guest-side and not available.
- **SM3.0:** existing.
- **New CSO/IDR:** none.
- **Slots:** existing SMAA/FXAA contracts.
- **INI:** `smaaEnable`, `fxaaEnable` (SMAA wins when both on; `postfx_newfx.cpp:795-801`).
- **Risk:** Low.
- **Impact:** 3.

#### P7 — Tonemap + exposure for the IV path

- **Files:** `src/render/postfx.cpp` (`COLORFILTER_GTAIV` case `:1842-1850`), `src/render/postfx_colourfilter.cpp` (`ColourFilter_Modern` `:150`, tonemap `:591-630`), `src/render/postfx_luminance.cpp:168`.
- **Algorithm:** route IV through a tonemap-only `ColourFilter_Modern` call exactly like the `colorFilterEnable=0` bypass does (`postfx.cpp:1694-1725`: `s_tonemapIdentityGrade=true`, `ColourFilter_Modern(rgb1,rgb2)`, `UpdateFrontBuffer()`), or add a dedicated `IVTonemap` pass using `TonemapPass.hlsl`/`TonemapLUT.hlsl`.
- **Ordering:** tonemap **before** `DrawIVGrade`; then set IVGrade's `exposure=1`, `gamma=1`, and remove its bloom/vignette if P1 is active. This avoids the raw 2.5× stack and the double vignette.
- **SM3.0:** existing ps_3_0 (`TonemapPass`/`TonemapLUT`).
- **New CSO/IDR:** none — reuse `IDR_TONEMAPPASS 166`, `IDR_TONEMAPLUT 269`, `IDR_LUMINANCEREDUCE 253`, `IDR_LUMINANCEADAPT 254`.
- **Slots:** c5-c9 + s0-s1, own pass.
- **INI:** new `ivTonemap` (0/1), `ivTonemapOperator`; existing `tonemapAutoExposure`, `tonemapAdaptSpeed`, `tonemapMinExposure`, `tonemapMaxExposure`, `tonemapKeyStrength`.
- **Risk:** High — double tonemap/exposure; IV shaders are LDR; cutscene interiors wash out (the existing cap `s_ivExpCap=1.5` is a band-aid). Gate with screenshots of day/night/interior/cutscene.
- **Impact:** 4.

#### P8 — Shadow maps + PCF/PCSS

- **Files:** new `src/render/shadowmap.cpp/.h`, `shaders/ps/ShadowDepth.hlsl`; bind in `buildingPipe.cpp`/`vehiclePipe.cpp`; config in `main.cpp`; optionally reuse `shadow_quality.cpp`'s constants (`GetPCFKernelSize`, `GetPCSSLightSize`).
- **Algorithm:** one or more directional shadow maps; depth-only pass from the sun's view; manual PCF 3×3/5×5/7×7 or PCSS in the material PS. Reference `shadowz`/`shadowzdir`/`shadowsmartblit` names confirm the pass family, but the algorithm is opaque.
- **SM3.0:** possible; needs a depth RT (D24S8 or R32F), `tex2Dproj`/manual compare; no `sampler2DShadow` in D3D9.
- **New CSO/IDR:** `ShadowDepth.cso` IDR 283 (or 284+ after P2's 281), plus a PBR/IV PS permutation for sampling.
- **Slots:** material PS `c53-c57` (matrix + bias/PCF params), `s10` (shadow map); VS `c45-c48` if the shadow UV is computed per-vertex.
- **INI:** new `ivShadowMapEnable`, existing `shadowResolution`, `shadowFilter`.
- **Risk:** High — engine integration (shadow casters, cascades, bias, perf), and our hook path renders the game's own passes; adding a sun-depth pass requires a new render phase.
- **Impact:** 4. Only attempt after P1-P3 are stable.

#### P9 — Camera motion blur (reprojection)

- **Files:** `src/render/postfx_motionblur.cpp` (`:23` velocity buffer), `src/render/postfx.cpp` (`DrawMotionBlur` `:3605`); new `shaders/ps/MotionBlur_Camera.hlsl` adapted from `motion_blur_camera_ps.hlsl:65`.
- **Algorithm:** reconstruct world position from depth, project with previous VP, bidirectional fixed samples with depth threshold; clamp radius.
- **SM3.0:** fixed 16 samples, no dynamic loop; `ps_3_0` fine.
- **New CSO/IDR:** optional `MotionBlur_Camera.cso`, IDR 282; otherwise reuse `IDR_VECTORMOTIONBLUR 264`.
- **Slots:** c0-c4/s0-s1, own pass.
- **INI:** `motionBlurEnable`, `motionBlurCameraAware` (existing), new `ivMotionBlurReproject`.
- **Risk:** Medium (matrices, ghosting, cutscene velocity).
- **Impact:** 2.

#### P10 — Lens flare / sun glare

- **Files:** new `src/render/lensflare.cpp` or extend `DrawGodRays` (`postfx.cpp:1946`); `shaders/ps/LensFlare.hlsl`.
- **Algorithm:** screen-space sun disk + depth occlusion + 2-4 ghosts/halo; additive.
- **SM3.0:** trivial; `c0-c3`, `s0` depth/`s1` scene.
- **New CSO/IDR:** `LensFlare.cso`, IDR 286.
- **INI:** new `ivLensFlare`; existing `doglare` (SA sun glare) can be re-enabled as a stopgap.
- **Risk:** Low-Medium.
- **Impact:** 2.

#### P11 — General SSR

- **Files:** new `src/render/postfx_ssr.cpp`, `shaders/ps/SSR_Raytrace.hlsl`/`SSR_Composite.hlsl` adapted from `ssr_raytrace_ps.hlsl:209`/`ssr_composite_ps.hlsl:26`.
- **Algorithm:** fixed 32-step screen-space march + 4 binary refinements; Fresnel/edge fade; composite by confidence. No Hi-Z (not possible without compute).
- **SM3.0:** `Partial`; needs `g_ssaoDepthTex` + normal buffer (`s4`/`s1`); fixed loop.
- **New CSO/IDR:** 284/285.
- **Slots:** s0 colour, s1 depth, s2 normal; c0-c4.
- **INI:** new `ssrEnable`, `ssrIntensity`.
- **Risk:** High (noise, perf, depth/normal quality, screen-edge artifacts).
- **Impact:** 2. Defer; water SSR already covers the most visible case.

#### P12 — Height fog in IV profile

- **Files:** `src/core/modeManager.cpp` (`heightFogEnable=1`), `src/render/postfx.cpp:3874` (`DrawHeightFog`).
- **Algorithm:** existing height fog; no new shader.
- **New CSO/IDR:** none.
- **Slots:** existing.
- **INI:** `heightFogEnable`.
- **Risk:** Low.
- **Impact:** 2.

#### P13 — Soft particles / rain

- **Files:** requires particle-system hooks not present in the current postfx architecture; new `SoftParticles.hlsl`.
- **Algorithm:** depth-fade particles against `g_ssaoDepthTex`, rain particles via the game's particle system.
- **SM3.0:** possible but needs per-particle depth and blending; high integration risk.
- **New CSO/IDR:** 287.
- **INI:** new `ivSoftParticles`.
- **Risk:** High.
- **Impact:** 2. Defer/reject.

### 5.3 Rejected / not worth doing

- **P14 — Deferred lighting / G-buffer.** Our RW 3.6 hook pipeline renders through the game's camera RT and exposes one render target; a G-buffer + lighting pass is a new renderer. SM3.0 can do 4 MRT, but our draw hooks, material system and depth ownership would all need replacing. Reject for this project; the forward PBR path is the asset.
- **P15 — IV sky/clouds.** `gta_atmoscatt_clouds` is an opaque shader with no readable parameters; reproducing it without the authored timecycle data would be guessing. Reject unless data is extracted.
- **Cascaded shadow maps.** The reference has no cascade setup; only `shadowz`/`shadowzdir` pass names. A CSM port would be speculative. Use a single directional map (P8) first.
- **Stencil shadow volumes.** IV's cache indicates shadow maps, and our SA stencil path is a different mechanism. Reject as "IV parity".
- **MSAA shader resolve.** D3D9 cannot sample MSAA surfaces in ps_3_0; use `StretchRect`. Marked N/A.

### 5.4 Sequencing

1. **P1 + P5 + P6 + P12** (profile-only wiring; no new shaders) → immediate visual gain, easy rollback.
2. **P2** (IVGrade source/v2) → fix the core grade and kill the dead `ivSaturation`/`ivCurves` knobs.
3. **P3** (paint/glass) → the car is the focal object in IV mode.
4. **P4** (SSAO) → contact grounding.
5. **P7** (tonemap/exposure) → only after P2/P1 are A/B-stable; highest regression risk.
6. **P8** (shadows) → separate feature branch; benchmark before/after.
7. **P9-P13** as time permits; **P14/P15 rejected**.

Test gates for every step: day/night, interior, cutscene, rain, coast/water, and a car close-up. Log gates already exist (`[FX]` in `postfx_newfx.cpp`, `[PostFX] IVGrade GATE` in `postfx_colourfilter.cpp:1370-1377`).

---

## §6 Constants/register contract needed

### 6.1 Existing PBR material contract (must not be disturbed)

`shaders/ps/VehiclePBR_Modern.hlsl` declares the PS contract (`:29-72`):

| Range | Use |
|---|---|
| `c0` | `surfProps` (ambient, 0, diffuse, prelit flag) |
| `c1` | `fxParams` (fresnel, shininess, specularity, lightmult) |
| `c2` | `eyePos` |
| `c3` | `iblParams` |
| `c4` | `cloudShadow` |
| `c5` | `directCol` |
| `c6-c11` | `lightCol[6]` |
| `c12` | `directDir` |
| `c13-c18` | `lightDir[6]` |
| `c19` | `matCol` |
| `c20` | `iblAmbient` (x=sky ambient weight, y=vehEnvFallback, z=WetRoads, w=vehEnvMask) |
| `c22` | `pbrParams` (glossiness, specular, specTint, metallicness) — **must use `pipeUploadPBR()`** (`src/render/pipelinecommon.cpp:997`) |
| `c23` | `paintNoise` — **must use `pipeUploadPBR()`** |
| `c24` | `ambientColor` (vehicle + building entry) |
| `c25-c27` | view matrix rows (world→view) |
| `c28` | `skyParams` |
| `c29` | `screenSize` |
| `c44` | `chromeParams` (chromeClearcoat, envSpecularityMult, envStrengthMode, envGlint) |
| `c45` | `clusterParams` (Forward+; s5-s7) |
| `c46` | `layerCfg` (vehPBRLayers bitmask) |
| `c47` | `bldEnv` |
| `c48` | `nmParams` (normal-map bind/intensity/gloss-from-alpha/alpha override) |

Samplers: `s0` diffuse, `s1` env/reflection, `s2` mask/spec, `s3` IBL cubemap, `s4` screen-space normal buffer, `s5-s7` Forward+ atlases (tile/pos/colour), `s8` normal map. This matches the project contract in `AGENTS.md`; `s5-s7` are documented in `VehiclePBR_Modern.hlsl:57-79`.

**Consequence:** `c30-c43` are not declared by `VehiclePBR_Modern.hlsl` but sit inside the reserved `c0-c48` band (the band is a project contract, and other shaders/VS permutations use pieces of it). Treat them as reserved. **First uncontested PS constant: `c49`.** **First uncontested PS sampler: `s9`.** `ps_3_0` allows up to `c223` and `s15`, so there is ample room.

### 6.2 Proposed reservations for IV effects

| Register | New use | Notes |
|---|---|---|
| `c49` | IV paint params 1: `{SpecularColor, SpecularPower, Specular2Factor, specular2ColorIntensity}` | Matches SPS (`sps_preset_table.h:26-44`). |
| `c50` | IV paint params 2: `{clearCoat, flakeScale, flakeStrength, dirtLevel}` | For `VehiclePaint_GTAIV`/`CarPaint_Reflections` enhancements. |
| `c51` | IV wet params: `{wetness, waterF0, glossBoost, darkening}` | Only if wetness is added to the IV material path. |
| `c52` | IV glass params: `{tintR, tintG, tintB, opacity}` | If glass is folded into PBR; `Glass_Vehicle` already uses `c22/c23` in its own draw. |
| `c53-c56` | IV shadow matrix (4 vectors) | Only if P8 is implemented. |
| `c57` | IV shadow params: `{bias, normalBias, pcfRadius, strength}` | P8. |
| `c58-c60` | Reserved for future IV emissive/timecycle | Do not allocate without a consumer. |
| `s9` | IV paint flake/noise map | First free sampler. |
| `s10` | IV shadow map | P8. |
| `s11` | IV screen-space depth/normal (SSR/contact shadows) | P11/P8. |
| VS `c45-c48` | Shadow matrix / IV VS extras | Some VS variants use `c44` (`mobileBuildingVS.hlsl:12`); first free VS is `c45`. |

Do **not** reuse `c0-c2` in a material shader for IV grade values: `DrawIVGrade` owns `c0-c2` in its own fullscreen draw, and `c0-c2` are the PBR surf/fx/eye contract in material draws. The two draws are separate; the registers are global device state, so a material shader must never assume the grade's values are present.

### 6.3 Fullscreen pass contracts (no PBR collision)

Fullscreen IVGrade v2 (recommended):

```
s0 = scene (camera-sized copy, as today)
s1 = bloom pyramid mip0 (optional; bind a defined texture even if unused)
c0 = {exposure, desaturation, gamma, 0}
c1 = {vignetteIntensity, vignetteRadius, vignetteContrast, bloomIntensity}
c2 = {1/screenW, 1/screenH, 0, 0}
c3 = {saturation, curves, curveMid, flags}
c4 = {tintR, tintG, tintB, tintStrength}
```

This is a separate fullscreen draw (the project uses `s_ffQuad`, `RwIm2DRenderIndexedPrimitive`; see `postfx_colourfilter.cpp:1464-1526`), so it does not collide with `c22/c23` or `s0-s8` of the PBR draw. It does require saving/restoring render state (already done by `SaveRawGeomStates`/`ImmediateModeRenderStatesStore`, `postfx_colourfilter.cpp:1467-1469`).

Existing fullscreen passes use their own small contracts (e.g. Bloom `c0` + `s0/s1`, DOF `c0/c1/c3` + `s0-s2`, SunShafts `c0-c2` + `s0`, SSAO `c0-c2` + `s0-s2`). Keep them self-contained; never rely on constants left by a previous pass.

### 6.4 Collision analysis

| Existing allocation | New IV use | Collision? |
|---|---|---|
| PS `c0-c48` | `c49+` | No; next free is `c49`. |
| PS `s0-s8` | `s9+` | No; next free is `s9`. |
| `c22/c23` PBR | Do not touch | If adding paint into `main_vehiclePBR`, keep using `pipeUploadPBR()`; add paint params at `c49+`. |
| `c44-c48` reflection/NM | Do not touch | `c44` chrome, `c45` Forward+, `c46` layers, `c47` bldEnv, `c48` nmParams. |
| `s3-s8` | Do not touch | `s3` IBL, `s4` normal buffer, `s5-s7` Forward+, `s8` normal map. |
| VS `c0-c44` | `c45+` | No; `c44` is used by `mobileBuildingVS`. |
| Fullscreen pass `c0-c3` | IVGrade `c0-c4` | No; separate draw, but all constants are device-global — upload everything the pass reads. |

### 6.5 `ps_3_0` hard limits (what the port must respect)

- **Constants:** 224 float4 registers (`c0-c223`); samplers 16 (`s0-s15`).
- **Temps:** 32 (`r0-r31`); instruction slots: 512 (static shader limit enforced by FXC), plus texture instructions; keep passes well below the cap.
- **No bitwise/integer ALU** in ps_3_0: no `&`, `|`, `^`, `<<`, `>>`; no integer textures/UAVs. Bitmasks must be unpacked with float compares (as `layerCfg` does; `VehiclePBR_Modern.hlsl:114`).
- **No compute/UAV:** no GPU Hi-Z build, no tile-based light culling, no compute auto-exposure. Use fullscreen reduce passes (our `LuminanceReduce` 8×8 + `LuminanceAdapt` 1×1, `postfx_luminance.cpp:168`).
- **MRT:** D3D9 allows 4 simultaneous RTs, but this project's RW hooks expose one camera RT. A G-buffer would need a new render path, not just MRT.
- **MSAA:** ps_3_0 cannot sample `Texture2DMS`; D3D9 resolves with `StretchRect`. The reference's MSAA resolve shaders are for its host backends, not portable to our plugin.
- **Depth:** sample as `INTZ`/`R32F`; our existing `g_ssaoDepthTex` is the INTZ alias (`postfx_ssao.cpp:6, 48-49`). Use point sampling for depth; guard `1e-7`.
- **Dynamic loops:** FXC permits them, but this project standardizes on fixed/unrolled loops (all existing postfx shaders do). Convert the reference's `g_sampleCount`/`g_stepsPerSample`/`stepCount` loops to compile-time constants or quality permutations.
- **No `sampler2DShadow`:** do manual PCF with `tex2Dproj`/`tex2D` and a compare in the shader.

### 6.6 New CSO/IDR ledger

`resources/resource.h` currently uses up to **279** (`IDR_TREEALPHA`), with `280` annotated for W8-3 in the comment but not defined (`resources/resource.h:124-135`). Therefore:

| IDR | New asset | Needed by |
|---:|---|---|
| 281 | `IVGrade.cso` | P2 |
| 282 | `MotionBlur_Camera.cso` | P9 (optional) |
| 283 | `ShadowDepth.cso` | P8 |
| 284 | `SSR_Raytrace.cso` | P11 |
| 285 | `SSR_Composite.cso` | P11 |
| 286 | `LensFlare.cso` | P10 |
| 287 | `SoftParticles.cso` | P13 |
| 288 | `IVPaint.cso` | Only if not reusing `IDR_VEHICLEPAINT_GTAIV 200` |
| 289 | `IVGlass.cso` | Only if not reusing `IDR_GLASS_VEHICLE 204` |

Reuse first: `IDR_VEHICLEPAINT_GTAIV 200` (`resources/resource.h:46`), `IDR_GLASS_VEHICLE 204` (`:50`), `IDR_TONEMAPPASS 166`, `IDR_TONEMAPLUT 269`, `IDR_LUMINANCEREDUCE 253`, `IDR_LUMINANCEADAPT 254`, `IDR_WATER_IV 241`, `IDR_SUNSHAFTS 274`, `IDR_BLOOM_* 270-273`, `IDR_DOF_* 265-268`, `IDR_GRAIN 261`, `IDR_CA 262`, `IDR_VIGNETTE 263`, `IDR_SSAOPS 120`/`SSAO_TEMPORAL_PS 131`, `IDR_SMAAPS 121`/`SMAA* 122-129`.

---

## Appendix A — Evidence index (most-cited lines)

**Reference (`LibertyRecomp/gpu/`)**

- `video.cpp:4278-4283` — MRT index > 0 dropped.
- `video.cpp:4595-4596` — one colour attachment.
- `video.cpp:6621-6695` — guest shader lookup; empty-cache guard.
- `video.cpp:3543-3650` — postfx chain order.
- `video.cpp:3834-3841` — HDR/gamma present blit.
- `video.cpp:3564-3571` — native EAA disable.
- `video.cpp:4126` — host MSAA sample count commented out.
- `postprocess_renderer.cpp:259-338` — backend-specific shader creation.
- `postprocess_renderer.cpp:1863-2045` — bloom.
- `postprocess_renderer.cpp:1274-1460` — DoF.
- `postprocess_renderer.cpp:1099-1273` — SSAO.
- `postprocess_renderer.cpp:1461-1606` — SSR.
- `postprocess_renderer.cpp:2046-2207` — sun shafts.
- `postprocess_renderer.cpp:752/842/947` — TAA/SMAA/FSR1.
- `postprocess_renderer.cpp:1038/1607/1686/1776` — vignette/grain/CA/MB (no callers).
- `postprocess_renderer.cpp:401-406` — vignette pipeline only created if `m_vignettePS`, which is never assigned.
- `postprocess_aa.cpp:169` — `ApplyAA` definition; no caller.
- `shadow_quality.cpp:24-67` — dead shadow config.
- `gfx_state.h:35`, `gfx_state.cpp:308-320` — GBuffer enum/label only.
- `gtaiv_device.h:70` — `ShaderFlags` bit7 deferred.
- `gtaiv_render_state.h:45-60` — Xenos HDR/EDRAM formats.
- `sps_preset_table.h:21-44, 67-70, 84-97` — IV paint/chrome/glass/emissive SPS parameters.
- `LibertyRecompLib/shader/shader_cache.cpp:19/49/53/119/178/512/524/921/1366` — IV shader families/count.

**Our tree**

- `src/core/modeManager.cpp:341-344, 346-395, 527-537, 669-690` — profile ownership/rebuild.
- `src/render/vehiclePipe.cpp:965-986, 987-1075, 1086-1170, 3290-3298` — IV vehicle path.
- `src/render/buildingPipe.cpp:1094-1096, 1224-1252, 2697-2698` — IV building path.
- `shaders/vs/GTAIVBuilding_vs.hlsl:33-42, 100-140` — IV building VS contract.
- `src/render/postfx.cpp:1842-1850` — `COLORFILTER_GTAIV` bypass.
- `src/render/postfx.cpp:1895-1908` — `DrawIVGrade` call site.
- `src/render/postfx_colourfilter.cpp:1077-1083, 1084-1316, 1354-1547` — IV grade contract/blob/implementation.
- `src/render/postfx_colourfilter.cpp:1478-1517` — IV grade constant upload.
- `src/render/postfx_newfx.cpp:517-823` — sun shafts/DoF/bloom/CA/grain/vignette/FXAA.
- `src/render/postfx_ssao.cpp:518/706` — SSAO paths.
- `src/render/postfx_smaa.cpp:441` — SMAA dispatch.
- `src/render/postfx_luminance.cpp:168` — auto-exposure.
- `src/render/waterPipe.cpp:50-56, 1271-1284` — IV water style.
- `shaders/ps/Water_Parallax.hlsl:1-120` — IV water contract.
- `shaders/ps/VehiclePaint_GTAIV.hlsl:1-80` — unused IV paint shader.
- `shaders/ps/Glass_Vehicle.hlsl:1-50` — glass shader.
- `src/render/pipelinecommon.cpp:873/889/892/997` — paint/glass/reflection/`pipeUploadPBR`.
- `resources/resource.h:46/50/60/106-135` — shader IDs and next free IDR.
- `src/skygfx.h:462-470, 814-879` — config fields; `src/core/main.cpp:5357-5365` — ULTRA defaults.

## Appendix B — Binary/unreadable material (explicit)

- `LibertyRecompLib/shader/shader_cache.cpp` — 45 MB, 1,356 entries; the actual shader payloads are compressed DXIL/SPIR-V/AIR blobs, **not HLSL**. Names are readable; algorithms are not.
- `shaders/ps/GTAIVVehicle_ps.cso`, `shaders/vs/GTAIVVehicle_vs.cso`, `shaders/ps/GTAIVBuilding_ps.cso`, `shaders/vs/GTAIVForwardPlus_vs.cso`, `shaders/ps/GTAIVForwardPlus_ps.cso` — binary CSOs; vehicle PS/VS sources were deleted (documented at `vehiclePipe.cpp:965-973`). `GTAIVBuilding_vs.hlsl` is the only rebuilt IV source.
- `src/render/postfx_colourfilter.cpp:1084-1316` — `g_ivGradePSBytes` is embedded ps_3_0 bytecode; no HLSL source and no `IDR`.
- `resources/cso/*.cso` — compiled shader blobs; the `.hlsl` sources are authoritative where present.

No files were modified. This audit writes only this document.
