# Effects + Menu Overhaul Plan (consolidated from pasted research)

Date: 2026-09-29. Supersedes the raw research snippets pasted in chat (they assumed generic
"SkyGFX Plus" paths that DO NOT match this repo — corrected mapping in each section).

## 0. Status snapshot + gates

- Cull fix LANDED (pipeForceCullMode triple-layer sync + Env cb re-assert), build green
  (fast_build.py OK, 87/87 shaders, ASI 1,950,208 Sep 29 20:47). **UNCOMMITTED.**
- White blown ground: OPEN (suspects below). Fixes go in before any commit.
- Commit gate (standing user order): unified fast_build -> user in-game verification -> ONLY THEN
  commit -> then effects finishing + optimization. This plan starts the effects work in parallel
  lanes but the commit gate does NOT move.
- Junk never to commit: n1, qc, skygfx_plus_expIV.7z, tools tabshell/cloudstore ps1. Ours-untracked:
  src/core/crashhandler.cpp (belongs in the eventual commit).

## 1. Open defect: white blown-out ground (FIX FIRST)

Ranked suspects (from diff audit):
1. `buildingPipe.cpp:904` — `surf.diffuse < 1e-4 -> 1.0` fallback. `buildingPBRVS.hlsl:82,86`
   multiplies prelight AND the 7 direct lights by surfDiff. SA road/terrain materials bake lighting
   into vertex colors with diffuse=0 -> this fallback adds full direct-light sum on top of baked
   prelight. Fix direction: only boost when prelight is absent (or clamp combined term), not blanket 1.0.
2. `g_normalBufferTex` written AFTER geometry (`postfx.cpp:2592` in ColourFilter_switch), created
   `D3DPOOL_DEFAULT` with NO initial Clear (`postfx.cpp:4316-4365`) -> stale/garbage normals ->
   GGX blowout (author comment `buildingPipe.cpp:838-846` literally names "white ground patches").
   Gate at `buildingPipe.cpp:847-848` only covers disabled/WIND. Fix: clear RT at creation +
   gate/lerp-out invalid normals (length check) in main_building.
3. `shaders/ps/SSAO.hlsl:66-118,141-182` — fail-open-to-WHITE on invalid input + fixed ProjectToUV
   -> scene x AO no longer darkens the ground. Fix: fail-open to neutral-grey (0.5) or restore
   darkening contract; verify composite in postfx.
   Runner-up (only when pipeChainEnable=1, default 0): `shaders/ps/PipeChain.hlsl:137-178` SSR
   composite with stale `g_pipeChainClassifyTex` (cleared only at creation `postfx.cpp:4609` and
   end-of-chain `:4936`).

## 2. Utility Noise Texture system (foundation for everything else)

One runtime-generated RGBA texture, channel-packed (32x32 default, INI-scalable):
- **R = Perlin** (tilable base shape: clouds macro shape, vehicle dirt large grime)
- **G = Worley/Cellular** (erosion/detail: cloud "cauliflower" edges, water caustics, rain droplets)
- **B = White noise** (jitter: SSAO kernel rotation, raymarch ray-start dither, soft-shadow PCF dither)
- **A = Blue noise / Bayer** (dither: screen-door transparency on fences/bushes in dual-pass)

Implementation:
- NEW `src/render/utilitynoise.cpp` (+ decls in `src/skygfx.h`): `GenerateUtilityTexture(int res)`
  clamps 32..1024, destroys old texture on regen, `RwRasterCreate(w,h,32,rwRASTERTYPETEXTURE|
  rwRASTERFORMAT8888)` -> `RwRasterLock(..., rwRASTERLOCKWRITE)` fill loop (respect
  `RwRasterGetStride()/4`!) -> `RwRasterUnlock` -> `RwTextureCreate` -> filter
  `rwFILTERLINEARMIPLINEAR`, addressing `rwTEXTUREADDRESSWRAP`. Global `RwTexture* g_pUtilityNoise`,
  `int g_CurrentNoiseSize`.
- Globals live where the repo keeps render globals (next to g_iblTex pattern in skygfx.h/main.cpp).
- INI: `[General] NoiseQuality` 0=32, 1=128, 2=512, >=3=1024, parsed in `src/core/main.cpp`
  readIni (repo has NO src/config.cpp). Regenerate ONLY when resolution changed or texture null —
  call from readIni/refreshIni path (F11 reload already exists).
- ARGB8888 packing: pixel = (a<<24)|(r<<16)|(g<<8)|b (D3DFMT_A8R8G8B8 convention of RW PC raster).
- The pasted research's `GenerateNoise2D` integer hash is fine for B; write real Perlin/Worley
  (or high-quality approximations) for R/G — quality matters at 512/1024.
- Bind helper: do NOT use raw `d3dDevice->SetTexture` bypass pattern from research blindly —
  project convention is `dev->SetTexture` for raw slots (g_iblTex s3 style) OR RwTexture* for
  material slots. Use raw bind in postfx passes, RwTexture in pipe cbs.

## 3. Effect channel consumers (wire after §2 lands)

| Effect | Channel | Where (actual repo paths) |
|---|---|---|
| Volumetric clouds shape/erosion | R + G, 3-scale sampling ("Infinite Cloud Trick": 3 fetches at uv*0.05/0.2/0.8, density = base - 0.3*detail + 0.1*micro, smoothstep 0.4-0.8) | clouds pass (see §4) |
| Rain & water caustics | G sampled twice, scrolling uv+time / uv-time | `shaders/ps/Water_Parallax.hlsl` + `src/render/waterPipe.cpp` |
| Rain droplets on lens | G as screen-UV distortion | postfx droplet pass (new, optional) |
| SSAO kernel jitter | B (replaces random 4x4 texture array) | postfx SSAO setup (`postfx.cpp`) + `shaders/ps/SSAO.hlsl` |
| Soft shadow / PCF dither | B | shadow-related passes (if present) |
| Screen-door transparency | A: `clip(alpha - tex2D(noise, screenUV).a)` instead of clip(alpha-0.5) | dual-pass path `D3D9RenderDual` consumers: `buildingPipe.cpp` (fences/bushes), vehicle glass |
| Vehicle procedural dirt | R+G mixed mask, UV offset per VehicleID so every traffic car differs | `shaders/ps/VehiclePBR_Modern.hlsl` (main vehicle entry) |

Shader-side: add `sampler2D utilityNoise : register(sX)` per consumer (unused slots vary; pick
free slots per shader — s4/s5 are taken in VehiclePBR_Modern: s3=IBL, s4=normalBuf, s5=cluster).
SM3.0 rules: no dependent texture reads in loops; dither offsets ray starts to cut 64->16 steps.

## 4. Clouds architecture refactor (RwIm3D -> RpClump/RpAtomic)

Research proposal (adapted):
- `CreateCloudCluster(numSlices, centerPos)`: RpClumpCreate + root RwFrame; per slice: RpAtomicCreate
  sharing ONE `GlobalCloudQuad` RpGeometry (rpGEOMETRYPRELIT|rpGEOMETRYTEXTURED|
  rpGEOMETRYMODULATEMATERIALCOLOR), child RwFrame offset along Z (or arbitrary axis), slice frames
  parented to root. Per-frame: `RwFrameTranslate(root, wind, rwCOMBINEPOSTCONCAT)` + `RpClumpRender`
  (free frustum culling via clump bounds).
- VS camera-facing billboarding: worldPos = worldOrigin + CameraRight*pos.x + CameraUp*pos.y
  (LTM auto-fed from RwFrame; zero CPU billboard math).
- Transparent ordering: `CVisibilityPlugins::SetAtomicRenderCallback` custom cb computing depth
  along camera At vector for back-to-front; OR flag materials `rpMATERIALALPHA` and let the engine
  alpha pass handle it. Sort ATOMIC (cluster) level only, never per-slice vertex.
- Pipeline attach: `RpAtomicSetPipeline(atomic, gCloudPipe)` for the SM3.0 pipeline.
- **CAUTION**: SA's sky/clouds are CClouds Im3D skybox driven by game code — a full clump swap needs
  recon of the current cloud hook first (which function is hooked today, what geometry it feeds).
  Treat as: recon -> prototype on one cluster -> keep RwIm3D fallback behind INI flag.
- Cloud PS skeleton (pasted): CloudNoiseTex s0 density.r, thickness.g, edge.b; clip(density-0.05);
  Beer's law exp(-thickness*2); cheap HG phase 0.5*(1+cosAngle^2); alpha = density*sample.b.

## 5. Godrays as optional postfx (user's Desktop godrays.cpp/hlsl)

Pasted code uses D3DX effects + `RwEngineGetInternalVar(rwENGINEINTERNALD3DDEVICE)` — neither exists
in this project's build (FXC -> CSO -> Resource.rc; device via `d3d9device` extern in `src/rw/gta.h`).
Adaptation:
- HLSL: convert technique/pass file to plain `vs_3_0`/`ps_3_0` entry pair (like other postfx shaders),
  register-uniforms instead of D3DX handles (LightScreenPos c0, Density/Weight/Decay/Exposure c1..).
  Radial-blur occlusion sampling loop (NUM_SAMPLES 44, deltaTexCoord = (uv - sunUV) * Density/N,
  illuminationDecay *= Decay, out * Exposure) is fine for ps_3_0; consider 24-32 samples + B-channel
  jitter (from utility noise) to kill banding.
- Occlusion pass: render scene with flat-black material override into `g_pOcclusionRaster`
  (RwRASTERTYPECAMERATEXTURE, half-res OK) + draw sun corona billboard; restore camera raster.
  Wrap state changes with SaveRawGeomStates/RestoreRawGeomStates discipline; sun screen pos via
  RwCameraTransformPoint + perspective divide (the pasted code MISSES the w divide — fix).
- Wire into `src/render/postfx.cpp` chain as optional pass behind `[PostFX] godraysEnable`,
  plus Density/Weight/Decay/Exposure INI keys; additive blend onto scene (SrcBlend One/Dest One).
  Render after colour filter, before SMAA (so rays get anti-aliased).
- new HLSL -> add to fast_build shader list + `resources/resource.h`/`resource.rc` entries (IDs
  follow the 2xx block), embed CSO.

## 6. Menu overhaul: re3/revc-style in-game settings (INVESTIGATE FIRST)

Goal (user): like modloader exposes settings inside native SA menus, extend SA's own menus the way
re3/revc do, hosting skygfx effects/settings there.
- Phase 1 (research, read-only): how re3/revc patch/extend CMenuManager + menu pages (frontend.txd
  sprites, menu action IDs, PS2/PC menu flow), what SA 1.0 offsets correspond, how settings persist.
- Phase 2: design mapping of our INI keys (pipeline mode, noise quality, godrays, chrome, SSAO...)
  onto new/extended menu pages; reuse existing readIni/refreshIni for apply.
- Deferred until effects stack is stable; do not start Phase 2 coding before Phase 1 report.

## 7. Lane waves (write-scope separated)

- **Wave 1 (now, parallel):**
  A. fixer: white ground fix (buildingPipe.cpp + postfx.cpp normal-buffer clear + SSAO.hlsl).
  B. fixer: utility noise core (NEW src/render/utilitynoise.cpp + skygfx.h + main.cpp INI).
  C. librarian (read-only): re3/revc menu extension research (feeds §6).
- **Wave 2 (after 1A/1B):** godrays postfx (postfx.cpp + new shader + resources); clouds recon
  (explorer) then prototype; channel wiring (water G, SSAO B, transparency A, vehicle dirt R+G).
- Each wave: build via `python tools/fast_build.py`; NO commits; report ASI size/timestamp.
