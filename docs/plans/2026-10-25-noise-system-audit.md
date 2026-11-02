# Noise System Audit — 2026-10-25

**Status:** READ-ONLY audit. No source, shader, resource, or build file was modified. The only file written is this report.

> **Working-tree caveat:** this audit was performed against the working tree as found. At audit time `git status` already showed unrelated modifications to `build/skygfx.vcxproj`, `build/skygfx.vcxproj.filters`, `resources/Resource.rc`, `resources/resource.h`, and several `resources/cso/*.cso` files (none of them made by this audit). All line numbers below refer to the working tree, not to `HEAD`.

**Requirement (m0492, verbatim):** *"replace all the stochastic map with proper perlin based data, and use the file we generated with pregenrated noise types, to execute the stoachastic shit better more acomainbtle and wihtout issues but with using p[roepr mordern perlin code so that we can have a dedicated noise.cpp file that can be read from adn can conncety to otehr thigns that require some kind of noise generator for effects thank you"*

**Method used for this audit**

- Read every noise/stochastic producer and consumer under `src/` and `shaders/`.
- Grepped every `register(sN)` / `register(cN)` declaration in `shaders/` to build the sampler/constant budget.
- Ran `python tools/pack_helper_tex.py --verify` to read the packed TXD (read-only; it prints the texture table, writes nothing).
- Compiled the three noise headers with the project's FXC (`June 2010`, `ps_3_0` / `ps_2_0`) in `%TEMP%` to test whether they are actually usable. Results are in Appendix A.
- Grepped `src/` for `helper_tex`, `utility_noise`, `blue_noise`, `cloud_noise`; all hits are tools/docs, zero runtime hits.

**Executive summary**

1. **There is no dedicated C++ noise module today.** The only real C++ noise generator is `src/render/utilitynoise.cpp`; it is a texture baker, not a reusable service. Its Perlin is actually correct (quintic fade + gradient noise + wrapped lattice), but it is resolution-baked, unseedable, 8-gradient, over-octaved at low resolution, and has no mip chain.
2. **Shader-side "Perlin" is dead code and does not compile.** `shaders/include/PerlinNoise.hlsl` is not included by any live shader, and FXC rejects it on `ps_3_0` (`X3082` at line 76; after a cast, `X3535` bitwise-ops-not-supported). See Appendix A.
3. **"Stochastic texturing" today is not stochastic texturing.** It is a single-tap ±1 % UV offset produced by a `sin`/`fmod` hash (`shaders/stochasticBuildingPS.hlsl:8-17`). No tile rotation, no multi-tap blend, no blue-noise dither, no temporal component, no derivative handling.
4. **Stochastic is building-only and PS2/Xbox-only.** It is selected only by `buildingPipe_selectPS` (`src/render/buildingPipe.cpp:943-966`). The PBR building callback binds `buildingPBRPS` unconditionally (`:2115`), the GTA IV branch swaps to IV shaders before selection (`:1215-1274`), and the vehicle path has no detail/stochastic concept (`vehiclePipe.cpp` has zero `stochastic`/`detail` hits).
5. **The pre-generated noise assets exist but are neither embedded nor loaded.** `assets/helper_tex/helper_tex.txd` is a valid 6-texture RW 3.6 TXD (verified), but there is no `IDR_` for it in `resources/resource.h` / `resources/Resource.rc`, no loader in `src/`, and no runtime bind of `g_pUtilityNoise`. The only way it can currently affect rendering is if someone manually copies it into the game and writes a loader.
6. **The pre-generated tile and the runtime tile are not the same data.** Python bakes the G channel as `1.0 - Worley` (`tools/pack_helper_tex.py:609`) while C++ bakes `Worley` directly (`src/render/utilitynoise.cpp:220`); Python uses cubic smoothstep (`:519-520`) while C++ uses quintic (`:48`); Python normalizes each fBm octave independently (`:560-565`). Swapping between "asset" and "runtime" will change the look.
7. **Recommended shape:** one `src/render/noise.h/.cpp` service (pure math + texture cache + loader + GPU upload), one `shaders/include/Noise.hlsl` (SM3.0-safe, no bitwise ops, no 512-entry arrays), `s9` = utility noise, `s10` = blue noise, `c49-c52` = noise parameters (project-wide free), and new CSO IDs `281-283` for the Perlin stochastic variants so the old `227/231/278` remain as a rollback.

---

## §1 Inventory: every noise/stochastic producer

| File | What it does | Algorithm quality | Call sites | Verdict |
|---|---|---|---|---|
| `src/render/utilitynoise.cpp` (1-251) | CPU bake of one RGBA utility tile: R = 6-octave tileable Perlin fBm, G = tileable Worley F1, B = white hash, A = Bayer 8x8. API: `GenerateUtilityTexture(int)` (decl `src/skygfx.h:939`), globals `g_pUtilityNoise` / `g_CurrentNoiseSize` (`src/skygfx.h:937-938`). | **Actually correct Perlin**: `hash2u` avalanche (`:36-43`), quintic fade `fadef` (`:45-49`), 8 unit gradients (`:57-70`), wrapped lattice (`:75-104`), normalized 6-octave fBm (`:109-121`). Weaknesses: only 8 gradient directions; no seed; periods 4..128 regardless of resolution (aliasing at 32/128 px); no mip chain despite `rwFILTERLINEARMIPLINEAR` (`:194`, `:241`); Worley normalization `*1/0.75` (`:220`) clamps; Bayer A is ordered, not blue noise (`:150-160`). | `GenerateUtilityTexture` called from `src/core/main.cpp:762` (`refreshIni`) and `src/core/debugmenu_ui.cpp:2018`, `:2350`, `:3855`. **Nothing binds the texture**: no shader/C++ consumer; only status text at `debugmenu_ui.cpp:3505-3506`, `:3851-3852`. | **WRAP** — keep the math, move it into `noise.cpp`, add seed/band-limit/mips. |
| `shaders/include/PerlinNoise.hlsl` (1-303) | Intended 2D/3D Perlin, fBm, turbulence, domain warp, stochastic edge mask, surface height, water height, terrain blend, height→normal. | Classic `PERM[512]` + 12 `GRAD3`, quintic fade (`:51-54`), 2D (`:74-92`), 3D (`:95-126`), fBm (`:132-162`). **Does not compile on `ps_3_0`**: `int xi = floor(x) & 255;` at `:76` → FXC `X3082`; after an explicit cast, `&` still fails with `X3535` "Bitwise operations not supported on legacy targets". `DoubleWarpUVs` also passes `float2` to scalar `Perlin2D` at `:199-200`, `:204-205` (implicit truncation warnings). `HeightToNormal2D` is a stub (all four heights = `height`, `:290-301`). | **No include sites**. Grep for `PerlinNoise.hlsl` finds only the file itself and a stale docs claim (`docs/Shader Architecture.md:311`). | **REPLACE** — do not repair in place. Write an SM3.0-safe header; keep the math ideas. |
| `shaders/include/CarPaintNoise.hlsl` (1-79) | Car-paint metallic flake: `hash22` (`:6-10`), `perlinNoise` (`:13-25`), `fbm` (`:28-41`), `metallicFlake` (`:44-56`), `carPaint` (`:59-79`). | `hash22` is a `sin` hash; fade is **cubic smoothstep** `f*f*(3-2f)` (`:17`), not quintic; `fbm` does not divide by amplitude sum (`:28-41`); not tileable; not seeded. Compiles `ps_3_0` (Appendix A). | **No include sites** (grep). `docs/Shader Architecture.md:310` claims `CarPaint_Reflections, VehiclePBR_Modern`, but neither includes it; `CarPaint_Reflections.hlsl` has its own duplicate (`:50-76`). | **REPLACE/WRAP** — keep `metallicFlake` API, route to `Noise.hlsl`. |
| `shaders/include/StochasticSamplerPS.hlsl` (1-15) | `hash2D2D` (`:4-7`) + `tex2DStochastic` (`:9-15`): `UV + hash(UV*100)*0.01`. | `sin`/`fmod` hash; single tap; no derivatives; no blue noise; not tileable. Compiles `ps_2_0` (Appendix A). | **No include sites** (grep); the same code is inlined in the live stochastic shader. `docs/Building Pipeline.md:40` actually admits "no `StochasticSamplerPS.hlsl` include". | **REPLACE** — delete or make it a thin wrapper over `Noise.hlsl`. |
| `shaders/stochasticBuildingPS.hlsl` (1-77) | **Live** consolidated stochastic PS: `main_simpleStochastic` (`:31-34`), `main_simpleDetailStochastic` (`:52-57`), `main_xboxBuildingStochastic` (`:73-76`). | Inline `hash2D2D_stoch` (`:8-11`) + `tex2DStochastic` (`:13-17`); single tap; ±1 % UV jitter; diffuse sampled at `UV*1.2` (`:33`, `:54`, `:75`); detail at `UV*detailtile` (`:55`). | Built by `tools/fast_build.py:440-442`; embedded by `resources/Resource.rc:90`, `:94`, `:139`; loaded by `src/render/pipelinecommon.cpp:942`, `:946`, `:949`; selected by `src/render/buildingPipe.cpp:943-966`; bound by `:993-995`, `:1760-1762`. | **REPLACE internals** — same entry names or new Perlin entries (see §6). |
| `shaders/ps/simpleDetailStochasticPS.hlsl` (1-32) | Legacy standalone stochastic+detail PS (duplicate of the consolidated entry). | Same hash/jitter (`:4-13`); declares `tex1_detail` (`:16`) but never samples it; samples s0 and s2 (`:29-30`). | **Excluded from build**: `tools/fast_build.py:383-388` skips it because the root file owns the CSO. Not loaded by `pipelinecommon.cpp`. | **DELETE** (dead duplicate) or make it a one-line include of the consolidated file. |
| `shaders/ps/xbox/xboxBuildingStochasticPS.hlsl` (1-29) | Legacy standalone Xbox stochastic PS. | Same hash/jitter (`:4-13`); jitters diffuse s0 only (`:27`), env s1 straight (`:28`). | **Excluded from build**: `tools/fast_build.py:383-388`. Not loaded. | **DELETE** (dead duplicate). |
| `shaders/include/CommonMath.hlsl:79-90` | `Hash2D` / `Hash2DSeed` (sin hash, value noise, not gradient). | Cheap value hash; not tileable; no band limit. | **No include sites** (grep); `docs/Shader Architecture.md` does not list it. | **KEEP as fallback** or route to `Noise.hlsl`; it is not live. |
| `shaders/include/colorSpace.hlsl:233-247` | `HashNoise` + `ComputeCloudShadow`: hash value noise at `rotUV*8`, `smoothstep(0.3,0.7)`. | Value hash, not Perlin; not band-limited; 8× high-frequency cloud mask; aliases badly. `ComputeShadowMerge` (`:252-272`) is the consumer. | Only included by `shaders/unifiedPipe.hlsl:9`; `unifiedPipe.*` CSOs are built (`tools/fast_build.py:445-452`) but **not embedded** in `Resource.rc` and **not loaded** (`pipelinecommon.cpp` has no `unifiedPipe`). So not live. | **REPLACE** — use `Noise_FBMTile2D` for cloud shadow. |
| `shaders/include/PBR_Common.hlsl:77-94` | CloudWorks value noise: `PBR_chash`, `PBR_cnoise3d`, `PBR_cloudFBM` (4 octaves). | 3D value noise with `sin` hash; not gradient; no seed; no tile guarantee. `PBR_cloudFBM` / `PBR_cnoise3d` are **not called** anywhere (grep only definitions). | Header is included by live shaders: `shaders/ps/VehiclePBR_Modern.hlsl:16`, `CarPaint_Reflections.hlsl:25`, `Glass_Vehicle.hlsl:22`, `Rubber_Vehicle.hlsl:21`; but the cloud functions are dead. | **WRAP** — keep names as thin wrappers over `Noise.hlsl` to avoid touching callers later. |
| `shaders/ps/DynamicSky.hlsl:33-57` | Duplicate CloudWorks `hash` / `noise3d` / `fbm` used for cloud clumps, stars, sky noise. | Value noise, not gradient; duplicated constants. | **Live**: `IDR_DYNAMICSKY` 209 (`resources/resource.h:55`), `Resource.rc:70`, `pipelinecommon.cpp:895`. `noise3d` calls at `:149`, `:176-177`; `fbm` at `:67`. | **WRAP** — replace internals with `Noise.hlsl`; keep local names. |
| `shaders/ps/IBL_SkyCloud.hlsl:22-64` | Duplicate CloudWorks `hash` / `noise2d` / `noise3d` / `fbm`. | Same as DynamicSky. | CSO exists (`resources/cso/IBL_SkyCloud.cso`) but **not embedded** in `Resource.rc` and **not loaded** by `pipelinecommon.cpp` (no `IDR_IBL_SKYCLOUD`). Not live. | **WRAP/REPLACE** — only if it is wired later. |
| `shaders/ps/CarPaint_Reflections.hlsl:50-76` | Inline `hash22`, `perlinNoise` (cubic fade), `metallicFlake`; used at `:114`. | Same defects as `CarPaintNoise.hlsl`; duplicate code. | **Live**: `IDR_CARPAINT_REFL` 206 (`resources/resource.h:52`), `Resource.rc:67`, `pipelinecommon.cpp:892`. | **WRAP** — include `Noise.hlsl`, delete the inline copy. |
| `shaders/ps/Grain.hlsl:30-62` | Interleaved Gradient Noise (IGN) film grain, 3 octaves, TAA-safe. | Good, deliberate, resolution-independent; **not** a Perlin customer. | **Live**: `IDR_GRAIN` 261 (`resources/resource.h:111`), `Resource.rc:115`, `pipelinecommon.cpp` via `postfx_newfx.cpp:103`; drawn at `postfx_newfx.cpp:737-763`. | **KEEP** — do not replace with Perlin. |
| `src/render/waterPipe.cpp:727-790` | CPU `DetailHash` + periodic `DetailValueNoise` + two ripple trains → bakes a 256² water detail normal map. | Value noise (smoothstep), tileable by wrapped lattice; not gradient; no seed API; local duplicate. | Live water path (`EnsureDetailNormalTex`), texture consumed by `shaders/ps/Water_Parallax.hlsl:115` (s1). | **WRAP** — call `Noise_ValueTile2D` / `Noise_PerlinTile2D` from `noise.cpp`. |
| `src/render/postfx_ssao.cpp:8`, `:67-88`, `:590`, `:794` | Creates a 4×4 A8R8G8B8 white-noise texture; binds it to SSAO s1. | White noise, no blue-noise distribution, tiny 4×4 (visible tiling). | Live SSAO paths (`DrawSSAO`, `DrawSSAO_Overhaul`). | **WRAP** — use the blue-noise tile / utility B channel. |
| `src/render/treeWind.cpp:143-160` | Deterministic per-instance wind pseudo-noise from the atomic pointer and a 4096 ms clock. | Not Perlin; intentionally game-parity; deterministic. | Live wind lean (PBR/IV tilt path and VS wind). | **KEEP as-is / WRAP optional**; if a seeded noise service is wanted, port the same formula with `Noise_HashU32(atomic)`. |
| `shaders/vs/ps2/ps2BuildingWindVS.hlsl:30-46`, `shaders/vs/xbox/xboxBuildingWindVS.hlsl:38-62` | Hash-based wind value noise in the VS. | Cheap value hash; vertex-stage only. | Live wind VS. | **KEEP / WRAP optional** — outside the PS noise system. |
| `shaders/ps/PipeChain.hlsl` (classify pass) | Classify pack mode (`pipeParams.x > 8.5`) writes `packParams.rgb` + diffuse alpha (`:243-251`). | **No dither/noise at all.** The "screen-door dither" idea exists only in `docs/plans/effects-menu-overhaul.md:71`; it was never implemented. | Live classify bracket: `buildingPipe.cpp:2536-2541`, `postfx.cpp:2993-...`. | **CUSTOMER** (future), not an existing noise source. |
| `src/render/postfx_underwater.cpp` / `shaders/ps/Water_Underwater.hlsl` | Underwater fog; no noise/dither. | — | Live. | **CUSTOMER** (caustics/dither), not an existing source. |
| `shaders/ps/SMAA_*.hlsl` | SMAA edge/blend; no noise. | — | Live. | **CUSTOMER** (blue-noise jitter), not an existing source. |
| `tools/pack_helper_tex.py` (750 lines) | Packs `assets/helper_tex/*.{png,dds,bmp}` into a RW 3.6 TXD; writes `assets/helper_tex/helper_tex.txd` (`:103-104`). Also `--gen-placeholders` produces the noise PNGs from NumPy (`:568-647`). | Packer is byte-correct (verified). The **art** is placeholder: cubic smoothstep Perlin (`:519-520`), per-octave min-max normalization (`:560-565`), inverted Worley G (`:609`). | Build hook: `python tools/fast_build.py --helper-tex` (`tools/fast_build.py:819-836`). No runtime consumer. | **KEEP tool**, regenerate canonical art / align semantics. |

### Quality verdict on the one real Perlin (CPU)

`utilitynoise.cpp` is the only implementation that is both correct and live. It uses:

- integer avalanche hash `hash2u` (`:36-43`);
- Perlin quintic fade `t*t*t*(t*(t*6-15)+10)` (`:45-49`);
- 8 unit gradients (axes + diagonals, normalized) (`:57-70`);
- wrapped lattice indices (`:75-104`);
- 6-octave fBm with persistence 0.5, periods 4,8,16,32,64,128 (`:109-121`).

That is proper gradient noise, not value noise. The defects are engineering defects (no seed, no mips, over-octaving, 8 directions) rather than "not Perlin".

---

## §2 The pre-generated noise files

### 2.1 Staging PNGs

Measured with Pillow (dimensions/mode) and `ls -l` (bytes):

| Path | Dimensions | Mode/format | Bytes | Channels / packing | Tileable? | Mips |
|---|---|---:|---:|---|---|---|
| `assets/helper_tex/utility_noise.png` | 256×256 | PNG RGBA (Pillow mode `RGBA`) | 196,041 | R = tileable Perlin fBm (6 octaves); G = tileable Worley F1 **inverted** (`1 - F1`); B = white noise (`numpy rng`); A = Bayer 8×8 (`_bayer(8)` tiled) | R/G/A yes by construction; **B no** (independent random edges) | 1 (manifest `mips:false`, `manifest.json:8`) |
| `assets/helper_tex/cloud_noise.png` | 256×256 | PNG RGBA | 150,939 | R = density fBm (5 octaves, base 4); G = thickness = `1 -` Worley (8 cells); B = edge detail (2 octaves, base 16); A = Bayer 8×8 `*4+2` | R/G/B/A yes by construction | 1 (`manifest.json:7`) |
| `assets/helper_tex/blue_noise_64.png` | 64×64 | PNG `L` (L8) | 4,228 | Single 8-bit blue-noise slice; used for dither/TAA | Not guaranteed (see generation) | 1 (`manifest.json:9`) |
| `assets/helper_tex/cloud_layer0.png` | 512×512 | PNG RGBA | 122,522 | Cloud card (not noise); RGB grain + alpha shape | Clamp addressing, not tiled | 10 (`manifest.json:4`) |
| `assets/helper_tex/cloud_layer1.png` | 512×512 | PNG RGBA | 118,012 | Cloud card | Clamp | 10 (`manifest.json:5`) |
| `assets/helper_tex/cloud_layer2.png` | 512×512 | PNG RGBA | 112,250 | Cloud card | Clamp | 10 (`manifest.json:6`) |

Generation code: `tools/pack_helper_tex.py:596-613` (utility/cloud noise) and `:615-647` (blue noise). The blue-noise slice is extracted from `E:\SDKs\GTAIV.EFLC.FusionFix\source\resources\bluenoisevolumeL8.dds` if present (`:616-634`); otherwise a crude high-pass-filtered white-noise proxy is generated (`:636-646`). The current file is 4,228 bytes; the extract path and the proxy path both write the same 64×64 L8.

**Why the PNGs are not yet "proper modern Perlin":**

- `_tileable_perlin` uses cubic smoothstep `tx*tx*(3-2*tx)` (`tools/pack_helper_tex.py:519-520`), not the quintic fade used by the C++/modern implementation (`utilitynoise.cpp:45-49`).
- `_fbm` min-max normalizes **every octave independently** (`:560-565`), then sums; that destroys the 1/f amplitude relationship. `out /= total` does not recover it.
- `_tileable_worley` returns `1 - F1` for utility_noise's G channel (`:609`) while `utilitynoise.cpp:220` stores `F1` directly. The channel semantics are opposite.
- The PNGs have no seed metadata. Regenerating with a different seed changes the look with no version marker.

### 2.2 Packed TXD (the shipped artifact)

`assets/helper_tex/helper_tex.txd` — **4,723,600 bytes**, RW 3.6 / GTA SA PC stream TXD, deviceID 2. Read-back with `python tools/pack_helper_tex.py --verify`:

```
== E:\dev(dave)\skygfx_plus_expIV\assets\helper_tex\helper_tex.txd (4723600 bytes)
  texdict: 6 textures, deviceID=2
  name           format  dims     mips  alpha  flt/u,v  texels
  blue_noise_64  LUM     64x64    1     no     1/1,1    4096B
  cloud_layer0   8888    512x512  10    yes    6/3,3    1398100B
  cloud_layer1   8888    512x512  10    yes    6/3,3    1398100B
  cloud_layer2   8888    512x512  10    yes    6/3,3    1398100B
  cloud_noise    8888    256x256  1     yes    6/1,1    262144B
  utility_noise  8888    256x256  1     yes    6/1,1    262144B
  verify: OK (6 textures, 0 errors)
```

- **Format:** RGBA textures are packed to `A8R8G8B8` (D3DFMT 21, BGRA byte order in the TXD; `tools/pack_helper_tex.py:107-118`, `:275-296`, `:390-417`). `blue_noise_64` is `LUM8` (D3DFMT 50).
- **Packing:** `utility_noise` / `cloud_noise` are one mip only; `cloud_layer*` have 10 box-filtered mips; `blue_noise_64` one mip.
- **Filter/addressing in the TXD:** `utility_noise`/`cloud_noise` = filter 6 (`linearmip`) + wrap (`6/1,1`); `blue_noise_64` = filter 1 (`nearest`) + wrap (`1/1,1`); cloud layers = filter 6 + clamp (`6/3,3`). Manifest: `manifest.json:4-9`.
- **Mip/filter mismatch:** the noise tiles ask for a mip filter with `mips:false`. D3D9 will have only level 0; minification cannot select a lower mip. This is a real aliasing source if these are sampled minified.

**Is it embedded in the DLL?** **No.**

- `resources/resource.h` has no `IDR_HELPER_TEX` (full ID list ends at `IDR_SIMPLESTOCHASTICPS` 278, `resource.h:138`).
- `resources/Resource.rc` has no `helper_tex` line (grep: zero hits).
- `src/` has no `helper_tex` reference (grep: zero hits).
- `docs/helper_tex.md:126-131` explicitly says: *"Status: no C++ code loads `helper_tex.txd` yet (verified: no `helper_tex` reference in `src/`)."*

**Is it loaded at runtime from disk?** **No.** The loader would have to be written. The documented pattern is `RwStreamOpen` → `RwStreamFindChunk(rwID_TEXDICTIONARY)` → `RwTexDictionaryStreamRead` (`docs/helper_tex.md:104-124`, live examples `src/extras/texdb.cpp:146-177`, `src/render/neo.cpp:8-32`). Deployment is manual: copy to `models\helper_tex.txd` or modloader (`docs/helper_tex.md:133-140`).

### 2.3 Other "noise-looking" files that are not noise

- `resources/textures/env_*.png` / `.raw` (`env_chrome`, `env_glass`, `env_paint`, `env_rubber`) are environment maps, not noise (directory listing).
- `resources/textures/vehicles_effects.txd` is a vehicle FX TXD, not a noise tile.
- `resources/cso/*.cso` contains no textures; CSOs are compiled shaders.
- `SMAA_reference/Textures/*.dds` are SMAA area/search LUTs, not noise; they are reference assets, not loaded by this project.
- `resources/textures.txd` exists but is not referenced by `Resource.rc` (grep: zero hits); not a noise source.

### 2.4 Packing tool

`tools/pack_helper_tex.py`:

- Input: `assets/helper_tex/*.{png,dds,bmp}` (`:314-315`), manifest `assets/helper_tex/manifest.json` (`:303-307`).
- Output: `assets/helper_tex/helper_tex.txd` (`DEFAULT_OUT`, `:103-104`).
- Formats: PNG → RGBA32 (BGRA byte order), L8, A8L8; DDS → DXT1/3/5 or uncompressed 8888/565/1555/4444/L8/A8L8 (`:29-38`, `:107-118`).
- Mips: only when `"mips": true` in the manifest (`:341`, `:288-296`).
- It writes a native RW 3.6 stream TXD with 88-byte D3D9 native headers, per-mip size + data, empty extension chunks (`:390-417`), and always round-trip verifies its output (`:745-746`).
- It does **not** deploy the TXD to the game and does **not** embed it in the DLL.

---

## §3 Today's stochastic pipeline, end to end

### 3.1 Material classification (CPU)

1. `initTexDB()` opens `models\Mobile_details.txd` and sets it current at startup (`src/extras/texdb.cpp:146-177`).
2. `readTxt()` opens `models\texdb.txt` (`:55`), parses tokens:
   - `stochastic=` → `int stochastic` (`:57`, `:88-89`);
   - `hasdetail=`, `detailtile=`, `alphamode=`, `affiliate=`, `hassibling=`, `dualPass=`, `zwriteThreshold=` (`:80-100`);
   - any non-empty `hasdetail || alphamode || hassibling || affiliate || stochastic` allocates a `TexInfo` and stores it in the `texdb` map (`:103-124`).
3. `TexDbFindCB` (the `CTxdStore` find hook) attaches the `TexInfo*` to the `RwTexture` plugin slot; it preserves the source's `stochastic` flag across affiliate/sibling resolution (`:214-240`).
4. `RwTextureGetTexDBInfo(tex)` returns the attached `TexInfo*`, or `&faketexinfo` (`:180-189`). `faketexinfo` is zeroed and `dualPass=1` (`:170-178`), so **absent a `texdb.txt` entry, `stochastic` is false and no detail/detailtile exists**.

`TexInfo::stochastic` is a `bool` (`src/skygfx.h:1096`).

### 3.2 Shader selection (CPU)

`buildingPipe_selectPS(TexInfo *texinfo, bool hasEnvMap)` at `src/render/buildingPipe.cpp:943-966`:

```c
if(hasEnvMap){
    if(texinfo->stochastic && config->stochastic)
        return xboxBuildingStochasticPS;   // :946-947
    else
        return xboxBuildingPS;             // :949
}
if(config->detailMaps && texinfo->detail){
    float tile = texinfo->detailtile/10.0f;
    RwD3D9SetPixelShaderConstant(1, &tile, 1);   // :953  PS c1 = detailtile
    pipeSetTexture(texinfo->detail, 2);          // :954  s2 = detail map
    if(texinfo->stochastic && config->stochastic)
        return simpleDetailStochasticPS;         // :955-956
    else
        return simpleDetailPS;                   // :958
}
if(texinfo->stochastic && config->stochastic)
    return simpleStochasticPS ? simpleStochasticPS : simplePS; // :963-964
return simplePS;                                 // :965
```

Call sites:

- PS2 callback: `buildingPipe_bindSimplePS(atomic, material, texinfo, false)` in the wind branch (`:1257`) and the normal branch (`:1262`); `buildingPipe_bindSimplePS` calls `selectPS` at `:993` and binds at `:994-995`.
- Xbox callback: the env branch calls `buildingPipe_selectPS(texinfo, true)` directly at `:1760`, binds at `:1761-1762`; the non-env branch calls `buildingPipe_bindSimplePS(..., false)` at `:1764`.
- `treeAlphaPS` may replace the selected PS after the fact (`:1006-1012`).
- **PBR callback:** no `selectPS`; `RwD3D9SetPixelShader(buildingPBRPS)` at `:2115` is unconditional. `texinfo` is fetched only later for dual-pass/classify (`:2527`, `:2536`).
- **GTA IV branch:** `if(wantIVBuilding && haveIVPair)` swaps to `gtaivFPPS`/`gtaivBuildingPS` before any `selectPS` (`:1215-1240`); only the `else` branches reach `bindSimplePS` (`:1244-1262`). So stochastic is dead under `BUILDING_GTAIV`.
- **Vehicle path:** zero `stochastic`/`detail` hits in `src/render/vehiclePipe.cpp` (grep). Stochastic is building-only.

### 3.3 Loading the shaders

| Resource | ID | Declared | Embedded | Loaded |
|---|---:|---|---|---|
| `xboxBuildingStochasticPS` | 227 | `resources/resource.h:84` | `resources/Resource.rc:90` | `src/render/pipelinecommon.cpp:942` |
| `simpleDetailStochasticPS` | 231 | `resources/resource.h:88` | `resources/Resource.rc:94` | `src/render/pipelinecommon.cpp:946` |
| `simpleStochasticPS` | 278 | `resources/resource.h:130-138` | `resources/Resource.rc:138-139` | `src/render/pipelinecommon.cpp:947-949` |

`simpleStochasticPS` is a global (`src/render/buildingPipe.cpp:18`) and is logged by name (`:770`). If the makePS fails, the handle is NULL and `selectPS` silently falls back to `simplePS` (`:963-964`); only the *successful* no-detail selection logs a one-shot line (`:996-1004`).

### 3.4 Build path

- `tools/fast_build.py:439-442` compiles the three entries from `shaders/stochasticBuildingPS.hlsl` as `ps_3_0`:
  - `main_simpleStochastic` → `resources/cso/simpleStochasticPS.cso`
  - `main_simpleDetailStochastic` → `resources/cso/simpleDetailStochasticPS.cso`
  - `main_xboxBuildingStochastic` → `resources/cso/xboxBuildingStochasticPS.cso`
- The standalone `shaders/ps/simpleDetailStochasticPS.hlsl` and `shaders/ps/xbox/xboxBuildingStochasticPS.hlsl` are explicitly skipped (`tools/fast_build.py:383-388`) to avoid two producers for the same CSO.
- FXC is invoked with `/T <profile> /E <entry> /Fo <cso> <hlsl>` and **no `/I`** (`tools/fast_build.py:316`), so includes are relative to the source HLSL. A new `#include "../include/Noise.hlsl"` from the root file works; from `shaders/ps/*.hlsl` use `../include/Noise.hlsl`.

### 3.5 Runtime shader behavior

`shaders/stochasticBuildingPS.hlsl`:

```hlsl
float2 hash2D2D_stoch(float2 s)                                  // :8-11
{
    return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)),
                               dot(s, float2(269.5, 183.3))), 3.14159))
                * 43758.5453);
}

float4 tex2DStochastic(sampler2D tex, float2 UV)                 // :13-17
{
    float2 jitter = hash2D2D_stoch(UV * 100.0) * 0.01;
    return tex2D(tex, UV + jitter);
}
```

Entries:

- `main_simpleStochastic`: `tex2DStochastic(tex_simple, IN.texcoord0.xy * 1.2)` (`:31-34`).
- `main_simpleDetailStochastic`: `tex0_detail` at `UV*1.2`; `tex2_detail` at `UV*detailtile` (`:52-57`).
- `main_xboxBuildingStochastic`: `tex0_xbox` at `UV*1.2`; `tex1_xbox` straight (`:73-76`).

So the "stochastic" effect is exactly: **a deterministic, texture-space, single-tap UV offset of at most 1 % (0.01 in UV units), with the same offset for every pixel that shares a UV value.** It is not a stochastic *sampling* algorithm.

### 3.6 Configuration surface

- `[SkyGfx] stochasticTexturing` parsed at `src/core/main.cpp:5421`, written at `:6450`, commented template at `:6086`; documented at `docs/INI Configuration.md:56`.
- Preset defaults: LOW `0` (`main.cpp:5242`), MEDIUM `0` (`:5269`), HIGH `1` (`:5299`), ULTRA `1` (`:5349`).
- Mode profiles: PS2 `0` (`src/core/modeManager.cpp:88`), Xbox `1` (`:190`), PC Vintage `1` (`:293`), GTA IV `0` (`:358`), PBR `1` (`:425`).
- Debug menu row: `src/core/debugmenu_ui.cpp:1029-1030` ("Stochastic texturing", "config->stochastic - tiled-texture noise randomisation.").
- `[General] NoiseQuality` (utility tile resolution) parsed at `src/core/main.cpp:5156-5159`, applied at `:754-763`, documented in `docs/plans/effects-menu-overhaul.md:52-53`; menu at `debugmenu_ui.cpp:1613-1615`, `:2342-2350`, `:3850-3855`, accessors at `main.cpp:6909-6914`.

---

## §4 Defects: why stochastic texturing flickers / seams / breaks

> Terminology: there is **no "stochastic map"** in this repo. The only stochastic asset-like things are the noise PNGs/TXD (§2). The renderer uses a hash jitter, not a map.

1. **It is not stochastic texturing; it is a static 1 % UV warp.**
   - Evidence: `shaders/stochasticBuildingPS.hlsl:13-17`.
   - Consequence: there is no multi-tap averaging, no per-pixel tile rotation, no blend weight, no blue-noise dither. It cannot hide tiling; it only shifts the whole texture slightly. At 1 % of UV, the visual effect is nearly invisible on a 256² texture (≈2.5 texels at 256, ≈10 texels at 1024).
2. **The hash is a `sin`/`fmod` hash with a π modulus, not an integer hash.**
   - Evidence: `stochasticBuildingPS.hlsl:8-11`.
   - Consequence: precision-dependent across GPUs and shader models; can produce banding/rings; `fmod(x, 3.14159)` on negative UVs is negative, then `sin` is negative, then `frac` maps it back — the pattern is not a well-distributed random field. There is no seed.
3. **The jitter poisons the derivative used for mip selection.**
   - Evidence: `tex2D` on `UV + hash(UV*100)*0.01` (`:13-17`). `hash` is discontinuous in UV at every pixel.
   - Consequence: `ddx/ddy` of the sampled UV become high-frequency noise. The hardware LOD selection becomes pixel-dependent and unstable, especially under minification. This is the primary cause of **shimmer/aliasing under camera motion**. The shader never uses `tex2Dgrad` or an analytic derivative.
4. **Non-integer UV scaling creates wrap seams on non-seamless textures.**
   - Evidence: diffuse sampled at `UV*1.2` (`:33`, `:54`, `:75`); detail at `UV*detailtile` where `detailtile = texinfo->detailtile/10.0f` (`buildingPipe.cpp:952-954`) can be non-integer (e.g. 7.5).
   - Consequence: with `rwTEXTUREADDRESSWRAP` (the default for TXD textures), the texture's last fractional row/column wraps to the first. If the source art is not seamless (most SA building textures are not authored for a 1.2× non-integer repeat), a visible seam appears at the 1/1.2 boundary. The jitter does not fix the seam; it moves it.
5. **No mip chain on the noise tiles, but mip filtering is requested.**
   - Evidence: `utilitynoise.cpp:194` creates `rwRASTERTYPETEXTURE | rwRASTERFORMAT8888` with no `rwRASTERFORMATMIPMAP`; `:241` sets `rwFILTERLINEARMIPLINEAR`. `utility_noise.png` and `cloud_noise.png` are packed with `mips:false` (`assets/helper_tex/manifest.json:7-8`) but filter `linearmip`.
   - Consequence: when these tiles are minified, D3D9 has no lower mip to pick; the shader samples level 0 at a high rate. High-frequency B/A channels alias into shimmer/patterns. If the intent is a dither tile, use `nearest` or `linear` with 1 mip; if it is a texture, generate mips.
6. **Over-octaving at the default 32 px tile.**
   - Evidence: `utilitynoise.cpp:109-121` always does 6 octaves with periods 4,8,16,32,64,128; `main.cpp:738-742` maps quality 0 → 32 px. At 32 px, periods 64 and 128 are below Nyquist (sub-pixel cells); at 128 px, period 128 is one pixel per cell.
   - Consequence: wasted generation time and aliasing in the R channel. The tile cannot represent the highest two octaves; they become noise/ringing.
7. **The pre-generated tile and the runtime tile disagree semantically.**
   - Evidence: Python G = `1.0 - _tileable_worley(...)` (`tools/pack_helper_tex.py:609`); C++ G = `worleyTile(...) * (1/0.75)` (`utilitynoise.cpp:220`). Python fade = cubic (`:519-520`); C++ fade = quintic (`:48`). Python fBm normalizes each octave independently (`:560-565`); C++ normalizes the summed fBm (`:120`).
   - Consequence: switching the source from "runtime generated" to "pre-generated TXD" changes the channel values and the look. Any shader authored against one will be wrong against the other. This is the biggest blocker to "use the file we generated" without visual regressions.
8. **The B channel is not tileable in `utility_noise.png`.**
   - Evidence: B is `rng.integers(0,256,(256,256))` (`tools/pack_helper_tex.py:610-611`), independent per pixel. The file is addressed `wrap` (`manifest.json:8`).
   - Consequence: the left/right and top/bottom edges of the B channel do not match, so any wrap-sampled use (SSAO jitter, raymarch dither) gets a visible seam every 256 texels.
9. **The A channel is ordered Bayer, not blue noise.**
   - Evidence: `s_bayer8` (`utilitynoise.cpp:150-160`), Python `_bayer(8)` (`tools/pack_helper_tex.py:498-505`, `:600-601`, `:612`).
   - Consequence: an 8×8 repeating pattern is visible at low resolution and produces structured dither, not the low-discrepancy blue-noise spectrum. The repo already ships a 64×64 blue-noise tile (`blue_noise_64.png`) but nothing uses it.
10. **Stochastic is dead under PBR, GTA IV, and all vehicle paths.**
    - Evidence: `config->stochastic` is read only in `buildingPipe_selectPS` (`buildingPipe.cpp:946`, `:955`, `:963`). PBR binds `buildingPBRPS` unconditionally (`:2115`); GTA IV swaps to IV shaders before selection (`:1215-1240`); `vehiclePipe.cpp` has no `stochastic`/`detail` code (grep).
    - Consequence: the flagship `BUILDING_PBR` mode ignores `stochasticTexturing` completely. Users who enable it under PBR see no change, which reads as "the feature is broken".
11. **Stochastic requires an external file and is opt-in per material.**
    - Evidence: `texinfo->stochastic` is only set from `stochastic=` in `models\texdb.txt` (`texdb.cpp:57`, `:88-89`, `:103`, `:121-122`). `faketexinfo` is zeroed (`:170-178`) and returned when no entry exists (`:180-189`).
    - Consequence: if `texdb.txt` is missing (or has no `stochastic=1` lines), **no material is stochastic**, even with `stochasticTexturing=1`. There is no fallback heuristic (e.g., "all building materials with a detail map"), so the feature can appear entirely absent.
12. **The detail-stochastic PS declares `s1` but never samples it.**
    - Evidence: `simpleDetailStochasticPS.hlsl:16` and `stochasticBuildingPS.hlsl:41` declare `tex1_detail`, but the body samples only `s0` and `s2` (`:29-30`, `:54-55`). The selection binds the detail map to `s2` (`buildingPipe.cpp:954`).
    - Consequence: `s1` is dead in this shader. It may hold an env map from an earlier mesh (the env branch binds `s1` at `:1748`, `:2475`, `:2496`), so any future code that assumes `s1` is free/unbound will read stale data.
13. **`c1` is only uploaded in the detail branch.**
    - Evidence: `buildingPipe.cpp:953` uploads `c1 = detailtile` only inside `if(config->detailMaps && texinfo->detail)`. The no-detail `simpleStochasticPS` only gets `c0` (`:1182`). `c1` therefore holds whatever the previous mesh left (a detail tile, or an env upload).
    - Consequence: a new noise shader that uses `c1` for noise parameters would read stale values on the no-detail path. Use `c49+` (see §6) and upload unconditionally per draw.
14. **The fallback silently disables the effect.**
    - Evidence: `return simpleStochasticPS ? simpleStochasticPS : simplePS;` (`buildingPipe.cpp:963-964`). If `makePS` failed (`pipelinecommon.cpp:947-949`), there is no log at selection time (the one-shot `bld_stoch` log at `:996-1004` only runs when the PS is actually selected).
    - Consequence: a missing/renamed CSO produces "stochastic texturing does nothing" with no diagnostic. This is a support/debugging hazard.
15. **The shader file is duplicated in three places.**
    - Evidence: live `stochasticBuildingPS.hlsl:8-17`; dead `shaders/ps/simpleDetailStochasticPS.hlsl:4-13`; dead `shaders/ps/xbox/xboxBuildingStochasticPS.hlsl:4-13`; plus `StochasticSamplerPS.hlsl:4-15`.
    - Consequence: fixes must be applied 4×; drift is inevitable. The dead files are skipped by the build (`fast_build.py:383-388`) but still mislead readers and docs (`docs/Shader Architecture.md:307`, `docs/Building Pipeline.md:40`).
16. **The documentation is stale and claims includes that do not exist.**
    - Evidence: `docs/Shader Architecture.md:310-311` claims `CarPaintNoise.hlsl` is used by `CarPaint_Reflections, VehiclePBR_Modern` and `PerlinNoise.hlsl` by `DynamicSky, IBL_SkyCloud, Water_Parallax`. Grep for `#include` shows none of those. `docs/Building Pipeline.md:40` correctly says "no `StochasticSamplerPS.hlsl` include", which contradicts `docs/Shader Architecture.md:307`.
    - Consequence: an engineer trusting the docs will assume a working Perlin library exists. It does not.

**Summary of the visible failure modes**

- **No temporal animation** in the stochastic shader (UV-only hash), so the effect does not "flicker" by itself. The shimmer seen in motion is **LOD/derivative aliasing** (defect 3) and **lack of mips** (defect 5).
- **Seams** come from `UV*1.2` / non-integer `detailtile` on non-seamless art (defect 4) and the non-tileable B channel (defect 8).
- **"Breaks"** (feature appears absent) come from the external `texdb.txt` gate (defect 11), the PBR/GTAIV/vehicle bypass (defect 10), the silent fallback (defect 14), and the fact that `PerlinNoise.hlsl` cannot compile (defect 16 + Appendix A).

---

## §5 Proposed architecture

### 5.1 Design goals

1. **One C++ service** (`src/render/noise.h` / `noise.cpp`) that any effect can call: CPU bakes, water detail, SSAO, tree wind, cloud shadow, grain, future effects.
2. **One shader header** (`shaders/include/Noise.hlsl`) with the same primitives and the same parameter conventions.
3. **One canonical noise definition** so CPU and GPU cannot drift:
   - **Baked path (exact):** `Noise_BakeUtilityTile()` writes the same channel layout as the shipped `utility_noise`; the shader samples that texture. CPU and GPU see the same texels — bit-exact by construction.
   - **Procedural path (visual parity):** CPU and GPU use the same gradient set (8 unit directions), the same quintic fade, the same lattice period/wrap rule, and the same fBm normalization. The hash differs (integer avalanche on CPU, float hash on GPU), so parity is visual, not bit-exact. This is documented in both headers.
   - **Optional exact-procedural path:** upload a 256-entry permutation/gradient LUT (L8 or RGBA8) on `s11`; the GPU indexes it with `tex2Dlod` instead of hashing. This makes even the procedural path bit-comparable, at the cost of one sampler and one 1 KB texture. Recommended if the user wants exact CPU previews.
4. **SM3.0-safe by construction:** no `&`, `|`, `%`, no 512-entry int arrays, no dynamic indexing of large arrays, no `while`, fixed upper bound on octaves with `[unroll]` + `if`, `tex2Dgrad` for warped UVs.
5. **Seeded and deterministic:** every function takes an explicit `uint32 seed`; a global default seed is set once; the seed is passed to the shader in a constant.

### 5.2 C++ API (`src/render/noise.h`)

Proposed public surface (names are suggestions; keep the `Noise_` prefix to avoid collisions with `PBR_*`, `Detail*`, etc.):

```c++
#pragma once
#include <stdint.h>

namespace Noise {

// ---- lifecycle / seed ----
void     Init(uint32_t seed = 0);          // idempotent; resets the default seed
void     Shutdown();                       // releases cached RwTextures (if any)
void     SetSeed(uint32_t seed);
uint32_t GetSeed();

// ---- hashes (integer, deterministic) ----
uint32_t HashU32(uint32_t x);
uint32_t Hash2U32(uint32_t x, uint32_t y);
uint32_t Hash3U32(uint32_t x, uint32_t y, uint32_t z);
float    Hash01(uint32_t x);
float    Hash201(uint32_t x, uint32_t y);
float    Hash301(uint32_t x, uint32_t y, uint32_t z);

// ---- value noise ----
float Value2D(float x, float y, uint32_t seed = 0);
float ValueTile2D(float u, float v, int period, uint32_t seed = 0);

// ---- Perlin (gradient) noise: 8 unit gradients + quintic fade ----
float Perlin2D(float x, float y, uint32_t seed = 0);
float Perlin3D(float x, float y, float z, uint32_t seed = 0);
float PerlinTile2D(float u, float v, int period, uint32_t seed = 0);

// ---- fBm / turbulence ----
float FBM2D(float x, float y, int octaves, float lacunarity,
            float gain, uint32_t seed = 0);
float FBMTile2D(float u, float v, int basePeriod, int octaves,
                float lacunarity, float gain, uint32_t seed = 0);
float Turbulence2D(float x, float y, int octaves, float lacunarity,
                   float gain, uint32_t seed = 0);
float Ridged2D(float x, float y, int octaves, float lacunarity,
               float gain, uint32_t seed = 0);

// ---- cellular ----
float Worley2D(float x, float y, uint32_t seed = 0);          // F1
float WorleyTile2D(float u, float v, int cells, uint32_t seed = 0);

// ---- domain warp with analytic derivatives ----
struct WarpResult { float2 uv; float2 ddx; float2 ddy; };
WarpResult WarpUVGrad(float2 uv, float strength, float scale, uint32_t seed = 0);

// ---- tile baking (canonical channel packing) ----
// R = Perlin fBm, G = Worley F1, B = white hash, A = blue-noise/Bayer dither.
void BakeUtilityTile(uint8_t *rgba, int res, int quality, uint32_t seed = 0);
void BakeCloudNoise(uint8_t *rgba, int res, uint32_t seed = 0);

// ---- texture service (RW/D3D9) ----
bool       LoadHelperTxd(const char *path = "models\\helper_tex.txd");
RwTexture *GetUtilityTexture();     // cached; NULL until loaded/baked
RwTexture *GetCloudNoiseTexture();
RwTexture *GetBlueNoiseTexture();
int        GetUtilitySize();        // 0 = not resident

// ---- GPU binding ----
// Uploads c49-c52 and binds s9/s10. `enabled=0` writes c52.x=0 and binds white.
void UploadShaderParams(int enabled, float scale, float strength,
                        float seed, float time, float tileScale,
                        float blend, float jitter, int flags);
void BindTextures(int sampler9 = 9, int sampler10 = 10);
void UnbindTextures(int sampler9 = 9, int sampler10 = 10);

} // namespace Noise
```

Notes:

- The pure math functions must not depend on `RwTexture`, `d3d9device`, or `skygfx.h`. Put the texture service at the bottom of `noise.cpp` behind an `#include "skygfx.h"`; keep `noise.h` lightweight (`stdint.h` only) so effects can include it freely.
- `BakeUtilityTile` must use the **same parameters** as `Noise.hlsl` and the packer so the fallback looks like the shipped asset. Add a `NoiseTileDesc` struct if the parameter list grows.
- `LoadHelperTxd` should first try `getpath("models\\helper_tex.txd")`; if absent, try `getpath("helper_tex.txd")`; if absent, return false and let the caller bake. Use the TXD-read pattern from `src/extras/texdb.cpp:146-177` / `src/render/neo.cpp:8-32`. Do **not** permanently call `RwTexDictionarySetCurrent` after the game has initialized; either use `RwTexDictionaryGetTexture` if available, or save/restore the current dictionary around `RwTextureRead`.
- `BindTextures` uses `RwD3D9SetTexture` for `RwTexture*` (project convention, `src/render/pipelinecommon.cpp` / `buildingPipe.cpp`) and sets `D3DSAMP_ADDRESSU/V = WRAP`, `MIN/MAG = LINEAR` (or `NEAREST` for the blue-noise tile), `MIPFILTER = NONE` if the tile has 1 mip.
- `UploadShaderParams` writes `c49-c52` (see §6.2) with `RwD3D9SetPixelShaderConstant`.

### 5.3 HLSL API (`shaders/include/Noise.hlsl`)

```hlsl
#ifndef NOISE_INCLUDED
#define NOISE_INCLUDED

#define NOISE_PI 3.14159265
#define NOISE_EPS 1e-7

// ---- hashes (float, SM3.0-safe: no bitwise ops, no int arrays) ----
float  Noise_Hash11(float n);
float2 Noise_Hash22(float2 p);
float  Noise_Hash21(float2 p);
float3 Noise_Hash33(float3 p);

// ---- fade / helpers ----
float  Noise_Fade(float t);                 // quintic 6t^5-15t^4+10t^3
float2 Noise_Fade2(float2 t);
float2 Noise_Grad2(float2 p, float seed);   // 8-direction unit gradient
float3 Noise_Grad3(float3 p, float seed);   // 12-direction 3D gradient

// ---- value noise ----
float  Noise_Value2D(float2 p, float seed);
float  Noise_ValueTile2D(float2 uv, float period, float2 seed);

// ---- Perlin (gradient) ----
float  Noise_Perlin2D(float2 p, float seed);
float  Noise_Perlin3D(float3 p, float seed);
float  Noise_PerlinTile2D(float2 uv, float period, float2 seed);

// ---- fBm (fixed max octaves; `octaves` is clamped at compile time) ----
float  Noise_FBM2D(float2 p, int octaves, float lacunarity,
                   float gain, float seed);
float  Noise_FBMTile2D(float2 uv, float basePeriod, int octaves,
                       float lacunarity, float gain, float2 seed);
float  Noise_Turbulence2D(float2 p, int octaves, float lacunarity,
                          float gain, float seed);
float  Noise_Ridged2D(float2 p, int octaves, float lacunarity,
                      float gain, float seed);

// ---- cellular ----
float  Noise_Worley2D(float2 p, float seed);
float  Noise_WorleyTile2D(float2 uv, float cells, float2 seed);

// ---- domain warp with derivatives (use with tex2Dgrad) ----
void   Noise_WarpUVGrad(float2 uv, float strength, float scale, float seed,
                        out float2 outUV, out float2 outDDX, out float2 outDDY);

// ---- texture decoders (the shader owns the sampler) ----
float  Noise_UtilR(float4 t);       // R = Perlin fBm
float  Noise_UtilG(float4 t);       // G = Worley F1
float  Noise_UtilB(float4 t);       // B = white
float  Noise_UtilA(float4 t);       // A = dither (Bayer/blue)
float  Noise_BlueNoiseL8(float texel); // 0..1

// ---- stochastic helpers ----
// 2-tap stochastic blend: returns a blended float4; caller supplies both taps.
float4 Noise_StochasticBlend(float4 a, float4 b, float dither, float blend);
// Per-pixel dither in [0,1): blue-noise texture preferred; IGN fallback.
float  Noise_Dither(float2 pixel, float frame, float blueTexel);
// Rotate a UV around 0.5 by a blue-noise-chosen angle.
float2 Noise_RotateUV(float2 uv, float angleRad);

#endif
```

Design rules baked into the header:

- **No `&`, `|`, `%`** anywhere. Use `fmod`, `floor`, `frac`, `step`, `lerp`.
- **No dynamic indexing of large arrays.** The 8-gradient set is implemented as a `cos/sin` rotation (validated in Appendix A) or a small `if` chain; the 12-gradient 3D set can use a 12-way chain if needed.
- **Fixed maximum octaves** (e.g. 6) with `[unroll] for(int i=0;i<6;i++){ if(i>=octaves) break; ... }`; the compiler can fully unroll and the runtime `octaves` is a uniform.
- **`tex2Dgrad`** for warped UVs; the header's `Noise_WarpUVGrad` returns the derivatives.
- **All divides guarded** with `max(x, NOISE_EPS)`, matching the repo's NaN rule (AGENTS.md "Division safety").
- **No sampler declaration in the header.** The consumer declares `sampler2D noiseTex : register(s9);` and passes texels to `Noise_UtilR` etc. This avoids duplicate sampler declarations when multiple headers are included.

### 5.4 How the texture plugs in without duplicating definitions

There are two consumption modes, both using the same header:

1. **Texture mode (cheap, exact, recommended for stochastic and dither):**
   - CPU: `Noise::LoadHelperTxd()` loads `utility_noise` / `blue_noise_64`; if missing, `Noise::BakeUtilityTile()` creates the same layout.
   - CPU binds s9/s10 in the draw callback (`Noise::BindTextures`).
   - Shader: `float4 n = tex2D(noiseTex, uv); float perlin = Noise_UtilR(n); float worley = Noise_UtilG(n); float dither = Noise_UtilA(n);`
   - No shader-side Perlin cost; the same texels are visible to CPU tools.
2. **Procedural mode (unbounded domain, animated/3D, no texture budget):**
   - Shader: `float n = Noise_Perlin2D(worldUV * scale, seed);`
   - CPU: `Noise::Perlin2D(x, y, seed)` uses the same gradient/fade/period definition for previews and bakes.
   - For exact parity, use the optional `s11` permutation LUT or the baked tile.

The **single definition** lives in two mirrored header comments:

- `noise.h` documents: gradient count (8 for 2D, 12 for 3D), fade polynomial (quintic), period wrap rule, fBm defaults (lacunarity 2.0, gain 0.5, normalized), Worley cell convention (F1, direct distance, not inverted), channel packing (R/G/B/A).
- `Noise.hlsl` repeats the same constants and conventions at the top of the file. A unit test (`Noise_SelfTest`) can compare a few CPU values against a GPU readback of a 1×1/8×8 debug RT; if they differ, the test fails.

### 5.5 Seeded deterministic mode

- `Noise::SetSeed(uint32_t)` sets the default; every function also takes an explicit seed.
- Fold the seed into the hash: `h = Hash2U32(x ^ seed, y + seed * 0x9E3779B9u)`; do not simply add the seed to the final float (that creates a DC shift).
- Shader: seed arrives in `c49.z`; `Noise_Grad2(p, seed)` mixes it into the angle/hash.
- The tile metadata should record the seed: add `"seed": <int>` to `assets/helper_tex/manifest.json`; the packer writes it into a small sidecar or the `note`. At minimum, `[General] NoiseSeed` makes the generated tile reproducible.
- Determinism scope: CPU integer hash is bit-exact across runs/compilers; GPU float hash is deterministic per driver but may differ across vendors. For cross-vendor exactness, use the baked texture or the `s11` LUT.

### 5.6 Band-limiting and quality

- `BakeUtilityTile(res, quality, seed)` computes `maxOctaves = max(1, floor(log2(res / basePeriod)))` with `basePeriod = 4`; e.g. 32 px → 3 octaves (periods 4,8,16); 128 px → 5 (4..64); 512 px → 7 (4..256); 1024 px → 8 (4..512). This removes the current sub-pixel octaves.
- `Noise_FBM2D` normalizes by the sum of amplitudes (`norm`), so the result is ~[-1,1] and stable across octave counts.
- `Noise_WorleyTile2D` returns `F1 / cellSize` (0..~1.2), not an inverted value; the shader decides whether to invert. This matches `utilitynoise.cpp:220` and makes the channel semantics explicit.
- Mips: for a texture that will be minified, bake a box-filtered mip chain. For a dither tile, use 1 mip + `nearest`/`linear` and no mip filter. Do not set `rwFILTERLINEARMIPLINEAR` on a 1-mip texture.

### 5.7 Recommended Perlin parameters for stochastic texturing

The stochastic effect should not be "Perlin noise per pixel" — that would be a different effect (procedural texture). The right split is:

- **Low-frequency Perlin** (2-4 octaves, scale 1-8) selects a macro-region / warp field. This breaks the repetition of the tiled building texture.
- **Blue-noise dither** (A channel or `blue_noise_64`) selects between 2-4 rotated/offset taps per pixel. This is the actual "stochastic sampling" step.
- **Analytic derivatives** keep mip selection correct (`tex2Dgrad`).
- **Static in time** by default (a building facade should not crawl). If animated, use a very slow time term (`time * 0.01`) with the same seed; never a per-frame random.

This is what makes it "more comfortable and without issues": the Perlin provides structure, the blue noise provides decorrelation, and the derivatives preserve LOD.

---

## §6 Integration plan

### 6.1 Sampler budget (PS registers)

Measured from every `register(sN)` in `shaders/` (all files, including VS/POSTFX). The highest existing PS sampler is **s8**; there are **zero** `register(s9)` or higher declarations.

Known assignments:

| Register | Owner(s) / examples | Notes |
|---|---|---|
| `s0` | diffuse everywhere (`buildingPipePS.hlsl:11`, `simplePS.hlsl:1`, `VehiclePBR_Modern.hlsl:18`, `Grain.hlsl:27`, `PipeChain.hlsl:14`) | Not free in any family. |
| `s1` | env / mask / detail (`buildingPipePS.hlsl:29`, `VehiclePBR_Modern.hlsl:19`, `Water_Parallax.hlsl:115`) | In `simpleDetailPS`/`simpleDetailStochasticPS` it is **declared but unused** (`simpleDetailPS.hlsl:5`, `stochasticBuildingPS.hlsl:41`), but the env branch does bind it. Do not standardize on it. |
| `s2` | detail / mask (`buildingPipePS.hlsl:30`, `stochasticBuildingPS.hlsl:42`, `VehiclePBR_Modern.hlsl:20`) | Detail map is bound here (`buildingPipe.cpp:954`). |
| `s3` | IBL (`VehiclePBR_Modern.hlsl:21`), water reflection (`Water_Parallax.hlsl:117`), vehicle dirt (`CarPaint_Reflections.hlsl:30`) | **Not** free project-wide, even though the simple building family does not declare it. |
| `s4` | normal buffer (`VehiclePBR_Modern.hlsl:22`), PipeChain classify pack (`PipeChain.hlsl:18`), SMAA normal (`SMAA_Temporal.hlsl:34`) | Not free project-wide. |
| `s5` | Forward+ cluster tile (`VehiclePBR_Modern.hlsl:58`) | Not free. |
| `s6` | Forward+ light positions (`VehiclePBR_Modern.hlsl:76`) | Not free. |
| `s7` | Forward+ light colors (`VehiclePBR_Modern.hlsl:77`) | Not free. |
| `s8` | normal map `_n` (`VehiclePBR_Modern.hlsl:27`) | Not free. |
| **`s9`** | **none** | **Free in every PS.** Recommended: utility noise (`utility_noise` RGBA). |
| **`s10`** | **none** | **Free in every PS.** Recommended: blue noise (`blue_noise_64` L8). |
| `s11` | none | Free; optional exact-parity permutation LUT. |
| `s12-s15` | none | Free (SM3.0 exposes s0-s15). |

**Per-family free slots:**

| Family / entry | Declared samplers | Free for noise (besides s9/s10) |
|---|---|---|
| `simplePS` (`main_simple`) | s0 | s1-s8 are unused by this shader; but s1 is bound by `buildingPipe_cleanup`/env code. Prefer s9. |
| `simpleDetailPS` (`main_simpleDetail`) | s0,s1(decl),s2 | s3-s8 |
| `simpleFogPS` (`main_simpleFog`) | s0 | s1-s8 |
| `xboxBuildingPS` (`main_xboxBuilding`) | s0,s1 | s2-s8 |
| `xboxBuildingStochasticPS` | s0,s1 | s2-s8 |
| `normMapBuildingPS` (`main_normMapBuilding`) | s0,s1 | s2-s8 |
| `simpleStochasticPS` (`main_simpleStochastic`) | s0 | s1-s8 |
| `simpleDetailStochasticPS` | s0,s1(decl),s2 | s3-s8 |
| `BuildingPBRPS` (`main_building`) | s0-s8 | **s9-s15 only** |
| `VehiclePBR_Modern` all entries | s0-s8 | **s9-s15 only** |
| PostFX (`Grain`, `PipeChain`, `SSAO`, `SMAA`) | s0-s4 (max) | s5-s15 |

**Decision: use `s9` for `utility_noise` and `s10` for `blue_noise_64` in all families.** This is the only pair that is free in both the simple building family and the PBR/vehicle family. Do not use s3 for noise even though the simple family has it free; it is IBL in PBR and water reflection in water.

**Collision note:** `s1` is declared but unused in `simpleDetailPS`/`simpleDetailStochasticPS`; do not "reclaim" it without removing the declaration and confirming no env bind path. `s2` is the detail map. `s3` is IBL/normal/reflection. `s4` is normal buffer/classify. `s5-s7` are Forward+. `s8` is the NM lane.

### 6.2 Constant budget (PS registers)

Measured from every `register(cN)` in `shaders/` (all files). The highest PS constant is **c48** (`VehiclePBR_Modern.hlsl:55`); there are **zero** `register(c49)` or higher declarations.

Known PAS (pixel shader) usages:

| Family / shader | Used PS constants | Free for noise |
|---|---|---|
| `simplePS` | c0 | c1-c48, c49+ |
| `simpleDetailPS` | c0,c1 | c2-c48, c49+ |
| `simpleFogPS` | c0,c1,c2 | c3-c48, c49+ |
| `xboxBuildingPS` | none | c0-c48, c49+ |
| `xboxBuildingStochasticPS` | none | c0-c48, c49+ |
| `normMapBuildingPS` | c0,c1,c4,c5-c11,c12-c18,c19,c20 | c2,c3,c21-c48, c49+ (c6-c10/c13-c17 are inside declared arrays; don't use individually) |
| `simpleStochasticPS` | c0 | c1-c48, c49+ |
| `simpleDetailStochasticPS` | c0,c1 | c2-c48, c49+ |
| `BuildingPBRPS` (`main_building`) | c0-c5, c6-c11, c12-c18, c19, c20, c22, c23, c24, c25-c27, c28, c29, c44-c48 | **c21, c30-c43, c49+** |
| `VehiclePBR_Modern` `main` | c0-c29, c44-c48 | c30-c43, c49+ |
| `main_rubber` | c19,c20,c22,c23,c24,c45,c48 | c30-c43, c49+ |
| `main_ps2EnvSpecFx` / `main_specCarFx` / `main_mobileVehicle` / `main_normMapVehicle` | `main_ps2EnvSpecFx` c45; `main_specCarFx` c45; `main_mobileVehicle` c1,c45; `main_normMapVehicle` c2,c25-c27,c45 | c49+ (c30-c43 are free but the file declares them globally for other entries) |
| PostFX (`Grain`, `SSAO`, `PipeChain`, `SMAA`, `Tonemap`) | c0-c4 (max) | c5-c48, c49+ |
| `colorSpace.hlsl` (only `unifiedPipe.hlsl`, not wired) | c30-c43 (declared at `colorSpace.hlsl:28-40`) | **latent collision** — do not use c30-c43 project-wide |
| VS domain (`vehiclePipe.cpp:18-38`, `vehiclePipeVS.hlsl:57-58`, `buildingPBRVS.hlsl:24-33`) | c30-c43 are **VS** registers | Separate space; not a PS collision, but avoid confusion |

**Decision: use `c49-c52` for the noise block.** No existing PS shader declares them. `ps_3_0` supports up to 224 float constants, so c49-c52 are legal. Do not use c40-c43: `colorSpace.hlsl:37-40` declares them (latent) and `vehiclePipe.cpp:38` uses them in the VS domain. Do not use c30-c33: `colorSpace.hlsl:28-32` and many VS files use them; keeping the noise block above all existing PS constants makes the collision audit trivial.

Proposed layout:

| Register | Contents |
|---|---|
| `c49` | `{ noiseScale, noiseStrength, noiseSeed, noiseTime }` |
| `c50` | `{ basePeriod, octaves, lacunarity, gain }` |
| `c51` | `{ stochasticTileScale, stochasticBlend, stochasticRotation, stochasticJitter }` |
| `c52` | `{ modeFlags, blueNoiseScale, ditherStrength, reserved }` — `modeFlags.x`: 0 = disabled (legacy/plain), 1 = Perlin + blue noise, 2 = procedural (no texture), 4 = debug |

Upload semantics:

- Upload `c49-c52` once per callback (or per mesh if the material scale varies) whenever the noise feature is enabled. When disabled, upload `c52.x = 0` (and optionally `c49.y = 0`); the shader must branch on `c52.x` and take the plain path. Do not leave stale constants.
- `c49.z` is a `float` seed in `[0, 2^24)` (float can represent all integers exactly up to 2^24; use that range).
- `c49.w` is a slow time in seconds; the shader uses it only if `c52.x` has the animate bit. For buildings, keep it 0 to avoid crawling.

### 6.3 Collision summary (explicit)

| Proposed | Collision? | Evidence |
|---|---|---|
| `s9` | **None** | Highest existing PS sampler = s8 (`VehiclePBR_Modern.hlsl:27`); grep for `register(s9+)` = 0. |
| `s10` | **None** | Same. |
| `c49` | **None** | Highest existing PS constant = c48 (`VehiclePBR_Modern.hlsl:55`); grep for `register(c49+)` = 0. |
| `c50-c52` | **None** | Same. |
| `c1` | **Collision** | `simpleDetailPS`/`simpleDetailStochasticPS` c1 = detailtile (`buildingPipe.cpp:953`; `simpleDetailPS.hlsl:8`). Do not use c1 for noise. |
| `c2` | **Collision** | `simpleFogPS` c2 = fogdist (`simpleFogPS.hlsl:7`); `buildingPipe.cpp:1832/1836` uploads fog to c2. |
| `c3` | **Collision (latent)** | `VehiclePBR_Modern.hlsl:32` c3 = iblParams; `Water_Parallax` uses c3 for other data. |
| `c4` | **Collision** | `normMapBuildingPS` c4 = ambient; `VehiclePBR_Modern.hlsl:33` c4 = cloudShadow (declared). |
| `c5-c18` | **Collision** | PBR lights (`VehiclePBR_Modern.hlsl:34-37`), normMap building lights (`normMapBuildingPS.hlsl:10-11`). |
| `c19-c20` | **Collision** | PBR matCol/surfProps (`VehiclePBR_Modern.hlsl:38`, `:83`), normMap (`:12-13`). |
| `c22-c24` | **Collision** | PBR `pbrParams`/`paintNoise`/`ambientColor` (`VehiclePBR_Modern.hlsl:39-41`); AGENTS.md c22/c23 contract. |
| `c25-c29` | **Collision** | PBR view rows/sky/screen (`VehiclePBR_Modern.hlsl:42-46`). |
| `c30-c43` | **Latent collision** | `colorSpace.hlsl:28-40`; VS domain `vehiclePipe.cpp:38`, `vehiclePipeVS.hlsl:57-58`. |
| `c44-c48` | **Collision** | PBR chrome/cluster/layer/bldEnv/nmParams (`VehiclePBR_Modern.hlsl:55-72`). |
| `s3` | **Collision** | IBL (`VehiclePBR_Modern.hlsl:21`), water reflection (`Water_Parallax.hlsl:117`), dirt (`CarPaint_Reflections.hlsl:30`). |
| `s4` | **Collision** | normal buffer (`VehiclePBR_Modern.hlsl:22`), classify pack (`PipeChain.hlsl:18`). |
| `s5-s7` | **Collision** | Forward+ (`VehiclePBR_Modern.hlsl:58`, `:76-77`). |
| `s8` | **Collision** | NM lane (`VehiclePBR_Modern.hlsl:27`). |

### 6.4 File-by-file integration

| File | Change | Why |
|---|---|---|
| `src/render/noise.h` | **New.** Public API (§5.2). | Dedicated service. |
| `src/render/noise.cpp` | **New.** Hash/Perlin/fBm/Worley/warp/bake/TXD loader/GPU upload. | Dedicated service. |
| `src/render/utilitynoise.cpp` | **Refactor.** `GenerateUtilityTexture` calls `Noise::LoadHelperTxd()` first; on failure calls `Noise::BakeUtilityTile`; fix mips (`rwRASTERFORMATMIPMAP`) and band-limit octaves. Keep `g_pUtilityNoise`/`g_CurrentNoiseSize` for existing call sites (`main.cpp:762`, `debugmenu_ui.cpp:2018/2350/3855`). | Removes the duplicate Perlin; makes the pre-generated asset authoritative. |
| `src/skygfx.h` | Include `noise.h`? Prefer not; keep `noise.h` lightweight. Keep `g_pUtilityNoise` externs (`:937-939`); add externs for `g_pBlueNoiseTex` if not inside `namespace Noise`. | Avoid header cycles. |
| `src/core/main.cpp` | Parse `[General] NoiseSystem` (default 1) and `[General] NoiseSeed` (default 0); call `Noise::Init(seed)` after RW/device are up; call `Noise::LoadHelperTxd()` before `GenerateUtilityTexture`; upload `c49-c52` in `refreshIni`? No — upload per draw, not per init. Add a `NoiseSystem` comment/template near `:6186-6187` and save near `:6777-6779`. | Kill switch + seed + loader. |
| `src/render/buildingPipe.cpp` | In `buildingPipe_selectPS` / `buildingPipe_bindSimplePS`: if `config->stochastic && texinfo->stochastic`, call `Noise::BindTextures(9,10)` and `Noise::UploadShaderParams(...)` before binding the stochastic PS; extend `buildingPipe_cleanup` (`:371-378`) and the PBR cleanup (`:2557-2590`) to unbind s9/s10 and reset sampler state. | Wire the noise texture/constants; prevent state leaks. |
| `src/render/pipelinecommon.cpp` | `CreateShaders`: no new CSO if reusing IDs; add `Noise::Init` call after device creation (or in `main.cpp`); if new IDs, add `makePS`. | Lifecycle. |
| `shaders/include/Noise.hlsl` | **New.** §5.3. | Shared GPU definition. |
| `shaders/stochasticBuildingPS.hlsl` | **Rewrite** the three entries to include `../include/Noise.hlsl` and use Perlin warp + blue-noise 2-tap blend (`tex2Dgrad`). Keep `c0` colorscale; use `c49-c52`; bind s9/s10. | Replaces the hash jitter. |
| `shaders/ps/simpleDetailStochasticPS.hlsl`, `shaders/ps/xbox/xboxBuildingStochasticPS.hlsl` | **Delete** or replace with a one-line include of the consolidated file. They are already build-skipped (`fast_build.py:383-388`). | Removes duplicate definitions. |
| `shaders/include/StochasticSamplerPS.hlsl` | **Replace** with `#include "Noise.hlsl"` + a `tex2DStochastic` wrapper that calls the new helper (for any external reader), or delete. | Removes the dead hash copy. |
| `shaders/include/PerlinNoise.hlsl` | **Replace** with a header that either (a) is deleted, or (b) becomes a thin compatibility layer including `Noise.hlsl` and aliasing `Perlin2D` → `Noise_Perlin2D` etc. Do not keep the broken `PERM[512]` code. | It cannot compile on SM3.0 (Appendix A). |
| `shaders/include/CarPaintNoise.hlsl` | **Replace internals** with `#include "Noise.hlsl"` and wrappers for `metallicFlake`/`fbm`/`perlinNoise`. | Deduplicate. |
| `shaders/ps/CarPaint_Reflections.hlsl` | Remove inline `hash22`/`perlinNoise` (`:50-66`); include `../include/Noise.hlsl`; call `Noise_Perlin2D`/`Noise_FBM2D`. | Live duplicate. |
| `shaders/ps/DynamicSky.hlsl`, `shaders/ps/IBL_SkyCloud.hlsl` | Replace `hash`/`noise3d`/`fbm` bodies with `Noise_Hash11`/`Noise_Value2D`/`Noise_FBM2D` wrappers (keep local names). | Two duplicates. |
| `shaders/include/PBR_Common.hlsl` | Optionally replace `PBR_chash`/`PBR_cnoise3d`/`PBR_cloudFBM` with `Noise::` wrappers. They are currently dead; low priority. | Future cloud shadow. |
| `src/render/waterPipe.cpp` | Replace `DetailHash`/`DetailValueNoise` (`:727-751`) with `Noise::ValueTile2D`/`Noise::PerlinTile2D`; keep the same periods/seeds so the water looks unchanged. | Deduplicate. |
| `src/render/postfx_ssao.cpp` | Replace the 4×4 white `g_ssaoNoiseTex` (`:8`, `:67-88`) with a bind of `Noise::GetBlueNoiseTexture()` or the utility B channel; keep s1. | Better dither, less tiling. |
| `src/core/debugmenu_ui.cpp` | Add "Noise source: TXD / baked / missing", "Seed", "Reload helper_tex" rows near `:3504-3506`, `:3850-3855`. | Operability. |
| `docs/helper_tex.md`, `docs/Building Pipeline.md`, `docs/Shader Architecture.md`, `docs/INI Configuration.md` | Update after implementation: actual include graph, new INI keys, new slots, new CSO IDs. | Docs are currently stale (defect 16). |
| `docs/plans/2026-10-25-noise-system-audit.md` | This report. | — |

### 6.5 New HLSL entry points / CSO / ID plan

There are two viable options. **Recommended: Option A (new IDs) for rollback.**

**Option A — new Perlin entries + new CSOs (safe rollback):**

- Add three entries to `shaders/stochasticBuildingPS.hlsl`:
  - `main_simpleStochasticPerlin`
  - `main_simpleDetailStochasticPerlin`
  - `main_xboxBuildingStochasticPerlin`
- New CSOs:
  - `resources/cso/simpleStochasticPerlinPS.cso`
  - `resources/cso/simpleDetailStochasticPerlinPS.cso`
  - `resources/cso/xboxBuildingStochasticPerlinPS.cso`
- New IDs: `IDR_SIMPLESTOCHASTICPERLINPS 281`, `IDR_SIMPLEDETAILSTOCHASTICPERLINPS 282`, `IDR_XBOXBUILDINGSTOCHASTICPERLINPS 283`.
  - `280` is reserved for W8-3 (`resources/resource.h:135`); `281+` are free.
- `resources/Resource.rc`: three `RCDATA` lines.
- `src/render/pipelinecommon.cpp`: three `makePS` calls; new globals in `src/skygfx.h` / `buildingPipe.cpp` (or a `Noise`-owned handle).
- `src/render/buildingPipe.cpp`: `buildingPipe_selectPS` chooses the Perlin variant when `config->noiseSystem != 0`; otherwise the old ID (227/231/278). This is the runtime rollback.
- `tools/fast_build.py`: three rows in `multi_entry` after `:442`.
- `bpipeBindPS` name mapping (`buildingPipe.cpp:766-772`) needs three new names.
- Pros: old CSOs remain untouched; `NoiseSystem=0` is a true A/B rollback; no risk of losing the legacy look.
- Cons: 3 more CSOs/IDs; ~3 extra makePS entries; slightly larger DLL.

**Option B — reuse the existing entries/CSOs (no rollback):**

- Rewrite the existing `main_simpleStochastic`, `main_simpleDetailStochastic`, `main_xboxBuildingStochastic` in place.
- No new IDs/RC/fast_build rows.
- Pros: minimal build churn.
- Cons: no runtime rollback to the old look; if the new shader is wrong, the only fallback is `stochasticTexturing=0` (which disables the effect entirely, not just the new algorithm).

**Recommendation:** Option A. The cost is small, and it satisfies the requirement to "execute the stochastic shit better ... without issues" while keeping a proven fallback.

### 6.6 How `simpleStochasticPS` becomes a proper Perlin-based effect

Pseudo-code (illustrative; not applied):

```hlsl
#include "../include/Noise.hlsl"

sampler2D tex_simple  : register(s0);
sampler2D noiseTex    : register(s9);   // utility_noise: R Perlin, G Worley, B white, A dither
sampler2D blueTex     : register(s10);  // blue_noise_64 (L8)
float4 colorscale_simple : register(c0);
float4 noiseA : register(c49);  // scale, strength, seed, time
float4 noiseB : register(c50);  // basePeriod, octaves, lacunarity, gain
float4 noiseC : register(c51);  // tileScale, blend, rotation, jitter
float4 noiseD : register(c52);  // mode, blueScale, ditherStrength, reserved

float4 main(PS_INPUT_SIMPLE IN) : COLOR
{
    float2 uv = IN.texcoord0.xy * 1.2;

    // 1) Low-frequency Perlin warp (macro variation, breaks tiling).
    float2 wUV, wDX, wDY;
    Noise_WarpUVGrad(uv, noiseA.y, noiseA.x, noiseA.z, wUV, wDX, wDY);

    // 2) Two stochastic taps: original and a rotated/offset copy.
    float4 a = tex2Dgrad(tex_simple, wUV, wDX, wDY);
    float2 uv2 = Noise_RotateUV(wUV, noiseC.z * 6.2831853);
    float2 d2 = float2(noiseC.w, -noiseC.w);
    float4 b = tex2Dgrad(tex_simple, uv2 + d2, wDX, wDY);

    // 3) Blue-noise dither chooses the blend per pixel (no temporal flicker).
    float dither = Noise_BlueNoiseL8(tex2D(blueTex, IN.texcoord0.xy * noiseD.y).r);
    float4 c = Noise_StochasticBlend(a, b, dither, noiseC.y);

    // 4) Kill switch: c52.x == 0 -> plain sample (legacy look).
    if (noiseD.x < 0.5)
        c = tex2D(tex_simple, uv);

    return c * IN.color * colorscale_simple.x;
}
```

The detail variant does the same for `s0` and `s2`; the detail sample uses `uv * detailtile` warped by a higher-frequency Perlin (same seed) so the detail does not swim relative to the diffuse. The Xbox variant does the same for `s0`; `s1` env is unchanged.

### 6.7 PBR / vehicle / other effects (phase 2)

- **PBR building (`main_building`)**: add `sampler2D noiseTex : register(s9);` and `c49-c52` to `VehiclePBR_Modern.hlsl`; inside `main_building`, after `baseColor`, add `baseColor *= lerp(1.0, Noise_UtilR(tex2D(noiseTex, IN.texcoord0 * c49.x)), c49.y * c52.x);` or use the Perlin to modulate roughness/env reflection. Gate with `texinfo->stochastic` in the PBR cb. This makes `config->stochastic` meaningful under `BUILDING_PBR`.
- **Vehicles**: `main()` already has `whiteNoise` (`VehiclePBR_Modern.hlsl:110-112`) and an unused `paintNoise.y/z` (`:520`). Replace `whiteNoise` with `Noise_Value2D`/`Noise_Perlin2D` for wheel roughness and reflection breakup. This is a low-risk customer.
- **SSAO**: bind `blue_noise_64` or `utility_noise` to s1 instead of the 4×4 white texture; the SSAO shader already samples s1 (`SSAO.hlsl:19`, `:56`, `:126`). No shader change if the texture format/layout is compatible (RGB dither); a small shader change can use `Noise_UtilA`.
- **Cloud shadow**: replace `ComputeCloudShadow` (`colorSpace.hlsl:238-247`) with `Noise_FBMTile2D` + a slow sun azimuth; wire `cloudShadow` c4 in `VehiclePBR_Modern.hlsl` (currently declared but unused, `:33`) and the building PBR if desired.
- **Car paint**: `CarPaint_Reflections.hlsl:114` already uses `metallicFlake`; route to `Noise_FBM2D` with a seed per vehicle.
- **Water**: replace the local `DetailValueNoise` with `Noise::PerlinTile2D`/`ValueTile2D`; keep the same periods (8/16/32) and ripple trains so the look is unchanged.
- **Grain**: keep `Grain.hlsl`; do not replace IGN with Perlin. If a blue-noise grain is desired, use `blue_noise_64` as a lookup, but IGN is already TAA-safe.

### 6.8 Render-state protocol (important)

The existing code has a three-layer render-state doctrine (`rw` cache / driver / raw device) and explicit cleanup at cb exit (`buildingPipe.cpp:371-378`, `:2557-2590`). Any new sampler must follow it:

1. **Bind:** `RwD3D9SetTexture(noiseTex, 9)` (or `dev->SetTexture(9, tex)` for a raw `IDirect3DTexture9*`) and set `D3DSAMP_ADDRESSU/V = WRAP`, `MIN/MAG = LINEAR`, `MIPFILTER = NONE` (1-mip tile) or `LINEAR` (mips). Do this once per cb, before the mesh loop, or per mesh if the texture changes.
2. **Unbind:** extend `buildingPipe_cleanup` to `RwD3D9SetTexture(NULL, 9)` / `(NULL, 10)` and raw `dev->SetTexture(9/10, NULL)` (the same two-step pattern as `:2557-2574`), and reset the sampler state to the cb-entry values.
3. **PBR cleanup:** the PBR cb already unbinds s2-s5/s8 at `:2557-2574`; add s9/s10 there too.
4. **No stale constants:** upload `c49-c52` every time the noise PS is bound; set `c52.x = 0` on the plain path.

---

## §7 Build-file changes

### 7.1 `build/skygfx.vcxproj`

Current source list includes `utilitynoise.cpp` at line 232 and the `ClInclude` group around lines 127-156. Add:

```xml
<ClInclude Include="..\src\render\noise.h" />
<ClCompile Include="..\src\render\noise.cpp" />
```

- Put the `ClInclude` near the other render headers (e.g. after `postfx.h` at line 145 or after `profiler.h` at line 147).
- Put the `ClCompile` next to `utilitynoise.cpp` at line 232.
- `build/skygfx.vcxproj` is checked in and uses explicit `<ClCompile>` entries (not a glob) — see lines 160-241. A premake regeneration would also add the file because `premake5.lua:19` globs `src/render/*.*`, but do not rely on regeneration; edit the vcxproj or run `premake5` deliberately.

### 7.2 `build/skygfx.vcxproj.filters`

Add matching entries with `<Filter>src</Filter>` in both ItemGroups:

```xml
<ClInclude Include="..\src\render\noise.h">
  <Filter>src</Filter>
</ClInclude>
<ClCompile Include="..\src\render\noise.cpp">
  <Filter>src</Filter>
</ClCompile>
```

- The filters file is partly stale: it still references pre-restructure paths (e.g. `..\src\buildingPipe.cpp` at line 93 while the vcxproj uses `..\src\render\buildingPipe.cpp` at line 161; `..\src\pipelinecommon.cpp` at line 132 vs `..\src\render\pipelinecommon.cpp` at line 213). Add the new entries with the current `..\src\render\...` paths; do not "fix" unrelated stale paths in this lane.
- Put the new `ClInclude` near the `profiler.h`/`renderstate_monitor.h` entries (lines 78-87) and the `ClCompile` near `utilitynoise.cpp` (line 172).

### 7.3 `premake5.lua`

`premake5.lua:19` already has `files { "src/render/*.*" }`, so a regeneration will pick up `noise.cpp`/`noise.h` automatically. No edit strictly required, but if the project is regenerated, verify the new file appears in the vcxproj and filters.

### 7.4 `tools/fast_build.py`

**If Option A (new entries) is chosen:**

- Add three rows to `multi_entry` after the existing stochastic rows (`tools/fast_build.py:439-442`):
  ```python
  ('ps_3_0', 'stochasticBuildingPS.hlsl', 'main_simpleStochasticPerlin', 'simpleStochasticPerlinPS.cso'),
  ('ps_3_0', 'stochasticBuildingPS.hlsl', 'main_simpleDetailStochasticPerlin', 'simpleDetailStochasticPerlinPS.cso'),
  ('ps_3_0', 'stochasticBuildingPS.hlsl', 'main_xboxBuildingStochasticPerlin', 'xboxBuildingStochasticPerlinPS.cso'),
  ```
- If you add standalone source files for these entries, add their names to the skip list at `:383-388` (the root file owns the CSO, one producer per CSO).
- The FXC invocation (`:316`) has no `/I`; use relative includes. For the root file: `#include "../include/Noise.hlsl"` (root `shaders/stochasticBuildingPS.hlsl` → `shaders/include/`). For `shaders/ps/*.hlsl`: `#include "../include/Noise.hlsl"`.
- `--helper-tex` (`:819-836`) already exists; if you want the build to deploy the TXD, add a copy step after the packer (currently it only packs).

**If Option B (reuse entries) is chosen:** no `multi_entry` change. The three existing CSOs are overwritten in place.

### 7.5 `resources/resource.h` / `resources/Resource.rc`

**Option A:**

```cpp
// resources/resource.h
#define IDR_SIMPLESTOCHASTICPERLINPS        281
#define IDR_SIMPLEDETAILSTOCHASTICPERLINPS  282
#define IDR_XBOXBUILDINGSTOCHASTICPERLINPS  283
```

```rc
// resources/Resource.rc
IDR_SIMPLESTOCHASTICPERLINPS        RCDATA "cso/simpleStochasticPerlinPS.cso"
IDR_SIMPLEDETAILSTOCHASTICPERLINPS  RCDATA "cso/simpleDetailStochasticPerlinPS.cso"
IDR_XBOXBUILDINGSTOCHASTICPERLINPS  RCDATA "cso/xboxBuildingStochasticPerlinPS.cso"
```

- `280` is reserved for W8-3 (`resources/resource.h:135`); `281-283` are free.
- `278` is the existing `simpleStochasticPS` (`resource.h:138`); do not reuse it for the new CSO if you want rollback.

**Option B:** no `Resource.rc` / `resource.h` changes; the existing `227/231/278` CSOs are rebuilt by `fast_build` from the rewritten entries.

**Noise header:** `Noise.hlsl` is an include, not a resource; no RC entry.

### 7.6 `shaders/shaders.bat`

Legacy helper (`shaders/shaders.bat`) compiles only `shaders/ps/` and `shaders/vs/` with `/E main`; it does not compile the root-level `stochasticBuildingPS.hlsl` and is not used by `fast_build.py`. No change is required, but do not use it as the authoritative build.

### 7.7 Docs to update after implementation

- `docs/Building Pipeline.md:32-40` (stochastic entries, "no external dependencies").
- `docs/Shader Architecture.md:307-311` (the include graph is wrong today).
- `docs/INI Configuration.md:56` (add `NoiseSystem`, `NoiseSeed`).
- `docs/helper_tex.md:104-131` (loader status; update from "no C++ code loads" once `Noise::LoadHelperTxd` lands).
- `assets/helper_tex/README.md:48-55` (state the canonical channel semantics; note B is not tileable in the placeholder).

---

## §8 Risks and rollback

### 8.1 Risk table

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | `ps_3_0` arithmetic/instruction budget exceeded by Perlin + fBm + 2 taps | Medium | Shader fails to compile or slow | Cap octaves at 3-4 for the stochastic PS; use the baked R channel for macro Perlin; check `fxc /Fc` instruction count; compile with `/T ps_3_0` in the build. |
| R2 | Sampler state leaks from s9/s10 into later passes | High if not handled | Wrong textures in postfx/water/UI | Extend `buildingPipe_cleanup` (`:371-378`) and PBR cleanup (`:2557-2590`) to unbind s9/s10 and reset sampler state; add a `bpipe` leak probe like the existing `bpipeBlendLeakProbe`. |
| R3 | Stale `c49-c52` when noise is disabled | Medium | Wrong pattern / NaN | Always upload the block with `c52.x=0` when disabled; the shader branches to the plain path; do not rely on zero-initialized constants. |
| R4 | `helper_tex.txd` missing at runtime | High (today it is not deployed) | Feature silently missing | `Noise::LoadHelperTxd` returns false → `Noise::BakeUtilityTile` fallback; log once; bind `gpWhiteTexture` and `c52.x=0` if no texture; never sample an unbound stage. |
| R5 | CPU bake and pre-generated asset disagree | High (already true) | Visual inconsistency / A/B confusion | Pick the canonical definition; regenerate `utility_noise.png`/`.txd` with the new baker or make the baker match the asset; add a `Noise_SelfTest` hash check. |
| R6 | `sin` hash differs across GPU vendors | Medium | Subtle pattern differences | Use integer hash on CPU and document visual parity; for exact parity use the baked texture or the `s11` permutation LUT. |
| R7 | Tile wrap with negative UVs | Medium | Seams / bright edges | `fmod` then add period if negative; unit-test with negative UVs; use `frac` carefully. |
| R8 | `tex2D` LOD noise from warped UVs | High (same class as today) | Shimmer/aliasing | Use `Noise_WarpUVGrad` + `tex2Dgrad`; add mips to the noise tile if it is minified. |
| R9 | PBR integration changes every building's look | Medium | Broad regression | Gate behind `config->stochastic` + `texinfo->stochastic`; default off for non-stochastic materials; keep `NoiseSystem=0` rollback. |
| R10 | New CSO IDs collide or are forgotten in RC | Low | Shader load failure | Use 281-283 (280 reserved); add RC + `makePS` + `pipelinecommon` together; run `fast_build --shaders --check-shaders`. |
| R11 | `Resource.rc` embeds a stale CSO if `fast_build` is not run | Medium | Old shader runs | The build compiles CSOs before MSBuild; verify timestamps and the `[INIT] shaders: compiled OK` log (`pipelinecommon.cpp:980`). |
| R12 | Noise texture increases DLL size (if embedded) | Low | 4.7 MB ASI | Prefer runtime TXD load; if embedded, accept the size or pack only `utility_noise` + `blue_noise_64` into a smaller TXD. |
| R13 | `Noise.hlsl` collides with `PerlinNoise.hlsl` names | Low | Compile errors | Use the `Noise_` prefix; never include both; delete/alias the old header. |
| R14 | Detail map `c1`/`s2` contract broken | Low | Detail/stochastic wrong texture | Keep `c1=detailtile`, `s2=detail` exactly as `buildingPipe.cpp:953-954`; noise params go to `c49-c52`, noise texture to `s9/s10`. |

### 8.2 Rollback / kill switches

**Existing user-facing switch (primary):**

- `[SkyGfx] stochasticTexturing=0` — disables the stochastic PS selection entirely (`buildingPipe_selectPS` returns `simplePS`/`simpleDetailPS`/`xboxBuildingPS`, `buildingPipe.cpp:946-964`). This already works today and should remain the master kill switch.

**New, finer switches (recommended):**

- `[General] NoiseSystem` (integer, default `1`):
  - `0` = legacy hash-jitter path (old CSOs `227/231/278`; no s9/s10 bind, `c52.x=0`).
  - `1` = Perlin + blue-noise path (new CSOs `281-283`).
  - `2` = procedural Perlin (no texture; for debugging/comparison).
- `[General] NoiseSeed` (integer, default `0`) — seed for the CPU bake and the shader `c49.z`.
- `[General] NoiseQuality` (existing, `main.cpp:5156-5159`) — tile resolution 32/128/512/1024.
- `[General] NoiseDebug` (integer, default `0`) — optional debug view (show R/G/B/A channels) for QA.

**Rollback procedure:**

1. Set `[General] NoiseSystem=0`. This selects the old CSOs and skips the new uploads; no file changes needed.
2. If the new CSOs fail to compile/load, the old IDs are still embedded (Option A); the game falls back to the old selection when `NoiseSystem=0`.
3. If the new `Noise.hlsl` breaks a live shader, remove the include from that shader (each customer is a separate include) and rebuild; `NoiseSystem=0` does not fix a compile error, so keep the header isolated and only include it where needed.
4. If `helper_tex.txd` is missing, the code bakes the fallback tile; `NoiseSystem=1` still works, just without the pre-generated asset.
5. Keep the old `stochasticBuildingPS.hlsl` content in git history (or a `#if 0` block) until the new path has passed the test matrix.

### 8.3 Test matrix

| Test | Expected |
|---|---|
| `python tools/fast_build.py --shaders` | All new/modified CSOs compile `ps_3_0`; no `X3xxx` errors. |
| `python tools/fast_build.py --check-shaders` | Same, parallel validation. |
| `python tools/pack_helper_tex.py --verify` | `verify: OK (6 textures, 0 errors)` after any repack. |
| `stochasticTexturing=1`, `NoiseSystem=1`, PS2 mode | Stochastic buildings use Perlin warp + blue-noise blend; no seams/shimmer; `[BPIPE] bld_stoch` log. |
| Same, Xbox mode | Xbox stochastic path; env map unaffected; no s9/s10 leak in the next pass. |
| Same, PBR mode | If PBR integration is enabled: noise affects `main_building`; if not, `stochasticTexturing` remains a no-op under PBR (document this). |
| `stochasticTexturing=0` | Old simple/detail PS; no noise bind; no c49-c52 upload. |
| `NoiseSystem=0` | Old CSOs `227/231/278`; visual parity with today. |
| `NoiseQuality=0/1/2/3` | Tile regenerates 32/128/512/1024; no sub-pixel octaves; no crash. |
| `helper_tex.txd` removed | Fallback bake; `[Noise] helper_tex missing -> baked` log; no black/NaN. |
| PBR + Forward+ + normal maps | No sampler state leak; FF/UI/water not sampling noise. |
| `detailMaps=1` + stochastic | s2 detail remains the detail map; c1 remains detailtile; noise params in c49-c52. |
| `detailMaps=0` + stochastic | `simpleStochasticPS` path; c1 is not read; c49-c52 are uploaded. |
| Env-mapped building + stochastic | `xboxBuildingStochasticPS` uses s1 env unmoved; s9/s10 noise bound; c49-c52 uploaded. |
| Device reset | `g_pUtilityNoise` / `g_pBlueNoiseTex` survive or are recreated; `Noise::Init`/`LoadHelperTxd` idempotent. |
| Alt-tab / resolution change | No stale constants; no sampler state; tile size unchanged unless `NoiseQuality` changes. |

---

## Appendix A — FXC evidence (headers are not usable as shipped)

All commands run in `%TEMP%\opencode\noiseaudit` with the project's FXC:

```
"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe"
```

### A.1 `PerlinNoise.hlsl` as shipped

Harness:

```hlsl
#include "E:/dev(dave)/skygfx_plus_expIV/shaders/include/PerlinNoise.hlsl"
float4 main(float2 uv:TEXCOORD0):COLOR {
    float2 w = DoubleWarpUVs(uv, 0.1);
    return float4(Perlin2D(w.x,w.y), Perlin3D(w.x,w.y,0.5),
                  FBM2D(w.x,w.y,4,2.0,0.5), 1.0);
}
```

Result:

```text
E:/dev/dave/skygfx_plus_expIV/shaders/include/PerlinNoise.hlsl(76,20): error X3082: int or unsigned int type required
compilation failed; no code produced
```

After mechanically changing `floor(x) & 255` to `(int)floor(x) & 255`, the next error is:

```text
PerlinNoise_fixed.hlsl(76,25): error X3535: Bitwise operations not supported on legacy targets.
```

After replacing `&` with `fmod` and `hash % 12` with `fmod`, FXC did not produce a CSO within a 120 s timeout on the 512-entry `PERM[512]` array under `ps_3_0`. Conclusion: **do not base the new header on `PERM[512]` + bitwise indexing**.

### A.2 `CarPaintNoise.hlsl`

Harness:

```hlsl
#include ".../CarPaintNoise.hlsl"
float4 main(float2 uv:TEXCOORD0):COLOR {
    return float4(metallicFlake(uv, 8.0), fbm(uv,4).xx, 1.0);
}
```

Result: `compilation succeeded` (`ps_3_0`). It is usable, but not modern Perlin (cubic fade, `sin` hash, not tileable).

### A.3 `StochasticSamplerPS.hlsl`

Harness with `ps_2_0`: `compilation succeeded`. Usable, but it is the same single-tap 1 % jitter.

### A.4 SM3.0-safe prototype (validated)

The following header shape compiles `ps_3_0` and is the basis for `Noise.hlsl`:

```hlsl
float N_Hash11(float n){ return frac(sin(n * 12.9898) * 43758.5453); }
float2 N_Grad2(float2 p){
    float a = N_Hash11(dot(p, float2(127.1, 311.7))) * 6.2831853;
    return float2(cos(a), sin(a));
}
float N_Fade(float t){ return t*t*t*(t*(t*6.0-15.0)+10.0); }
float N_Perlin2D(float2 p){
    float2 i = floor(p), f = p - i;
    float2 u = float2(N_Fade(f.x), N_Fade(f.y));
    float a = dot(N_Grad2(i + float2(0,0)), f - float2(0,0));
    float b = dot(N_Grad2(i + float2(1,0)), f - float2(1,0));
    float c = dot(N_Grad2(i + float2(0,1)), f - float2(0,1));
    float d = dot(N_Grad2(i + float2(1,1)), f - float2(1,1));
    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}
// tileable variant uses fmod(i, period), no bitwise ops
```

Result: `compilation succeeded` (`ps_3_0`). This is the validated direction: hash-based 8-gradient Perlin + quintic fade + `fmod` wrap, no `PERM[512]`, no bitwise ops.

---

## Appendix B — Verified TXD read-back

Command:

```bash
python tools/pack_helper_tex.py --verify
```

Output (exact):

```text
== E:\dev(dave)\skygfx_plus_expIV\assets\helper_tex\helper_tex.txd (4723600 bytes)
  texdict: 6 textures, deviceID=2
  name           format  dims     mips  alpha  flt/u,v  texels
  blue_noise_64  LUM     64x64    1     no     1/1,1    4096B
  cloud_layer0   8888    512x512  10    yes    6/3,3    1398100B
  cloud_layer1   8888    512x512  10    yes    6/3,3    1398100B
  cloud_layer2   8888    512x512  10    yes    6/3,3    1398100B
  cloud_noise    8888    256x256  1     yes    6/1,1    262144B
  utility_noise  8888    256x256  1     yes    6/1,1    262144B
  verify: OK (6 textures, 0 errors)
```

Interpretation:

- `format` `8888` = A8R8G8B8; `LUM` = L8.
- `flt/u,v` = filter/addressing code; `6/1,1` = linear-mip / wrap; `1/1,1` = nearest / wrap; `6/3,3` = linear-mip / clamp.
- The `cloud_layer*` 10 mips are the only ones with a full chain; the noise tiles are 1 mip.

---

## Appendix C — Exact line references for the recommended slots

| Claim | Evidence |
|---|---|
| Highest existing PS sampler = s8 | `shaders/ps/VehiclePBR_Modern.hlsl:27` (`nmTex : register(s8)`); grep for `register(s9+)` = 0. |
| Highest existing PS constant = c48 | `shaders/ps/VehiclePBR_Modern.hlsl:55` (`nmParams : register(c48)`); grep for `register(c49+)` = 0. |
| `c1` is detailtile | `src/render/buildingPipe.cpp:953`; `shaders/ps/simpleDetailPS.hlsl:8`; `shaders/stochasticBuildingPS.hlsl:44`. |
| `c2` is fog | `shaders/ps/simpleFogPS.hlsl:7`; `src/render/buildingPipe.cpp:1832`, `:1836`. |
| `c3` is IBL params | `shaders/ps/VehiclePBR_Modern.hlsl:32`. |
| `c4` is cloud shadow / normMap ambient | `shaders/ps/VehiclePBR_Modern.hlsl:33`; `shaders/ps/normMapBuildingPS.hlsl:9`. |
| `c22/c23` are the PBR contract | `shaders/ps/VehiclePBR_Modern.hlsl:39-40`; `src/render/pipelinecommon.cpp:1029-1030`; AGENTS.md. |
| `c44-c48` are PBR | `shaders/ps/VehiclePBR_Modern.hlsl:55-72`. |
| `s3` is IBL | `shaders/ps/VehiclePBR_Modern.hlsl:21`; `src/render/buildingPipe.cpp:2137-2141`. |
| `s4` is normal buffer / classify | `shaders/ps/VehiclePBR_Modern.hlsl:22`; `shaders/ps/PipeChain.hlsl:18`. |
| `s5-s7` are Forward+ | `shaders/ps/VehiclePBR_Modern.hlsl:58`, `:76-77`. |
| `s8` is NM | `shaders/ps/VehiclePBR_Modern.hlsl:27`; `src/render/buildingPipe.cpp:2353-2358`. |
| `c30-c43` latent | `shaders/include/colorSpace.hlsl:28-40`; `src/render/vehiclePipe.cpp:38`. |
| `s9/s10` free | grep across `shaders/` for `register(s9)`/`register(s10)` = 0 hits. |
| `c49-c52` free | grep across `shaders/` for `register(c49+)` = 0 hits. |
| 280 reserved, 281+ free | `resources/resource.h:135` (`280=W8-3`), no `IDR_` at 281-283. |

---

## Appendix D — What does **not** exist (explicit negatives)

To avoid future guessing:

- **No `src/render/noise.cpp` or `noise.h`.** (Glob/grep: none.)
- **No `shaders/include/Noise.hlsl`.** (Glob/grep: none.)
- **No runtime `helper_tex` loader.** (`docs/helper_tex.md:126-131` confirms; `src/` grep = 0.)
- **No `IDR_HELPER_TEX` / `helper_tex` in `resources/resource.h` or `resources/Resource.rc`.**
- **No noise texture bound to any shader today.** `g_pUtilityNoise` is only displayed/generated by `utilitynoise.cpp`, `main.cpp`, `debugmenu_ui.cpp`; no `SetTexture`/`pipeSetTexture` call.
- **No stochastic in the vehicle path.** `src/render/vehiclePipe.cpp` grep for `stochastic`/`detail` = 0.
- **No stochastic in the PBR building callback.** `buildingPipe.cpp:2115` binds `buildingPBRPS` unconditionally; `selectPS` is only in the PS2/Xbox simple branches.
- **No dither in `PipeChain` classify.** `shaders/ps/PipeChain.hlsl:243-251` writes `packParams.rgb` + alpha; no noise sample.
- **No noise in `postfx_underwater`.** Grep for `noise`/`dither` in `src/render/postfx_underwater.cpp` = 0.
- **No noise in SMAA.** Grep in `shaders/ps/SMAA_*` = 0.
- **No `PerlinNoise.hlsl` / `CarPaintNoise.hlsl` / `StochasticSamplerPS.hlsl` include sites.** Grep for `#include` = 0 for all three (the docs claim otherwise; docs are stale).
- **No actual "stochastic map" asset.** The maps are the noise tiles in §2; the stochastic path is a shader hash jitter.

---

## Appendix E — Suggested implementation order (for the implementer, not applied)

1. **Land `noise.h/.cpp`** with pure math + `Noise_SelfTest` (CPU only). Build with MSBuild.
2. **Land `Noise.hlsl`** with the SM3.0-safe primitives; add a tiny debug PS (or reuse a customer) and compile with FXC. Do not include it in live shaders yet.
3. **Canonicalize the asset:** decide the channel semantics (G = F1, quintic, normalized fBm), update `tools/pack_helper_tex.py` and/or `utilitynoise.cpp`, regenerate `utility_noise.png`/`cloud_noise.png`, repack, verify.
4. **Add `Noise::LoadHelperTxd` + `BakeUtilityTile` fallback**; wire `GenerateUtilityTexture` to prefer the TXD; add mips/band-limit.
5. **Add `s9`/`s10` + `c49-c52`** and the bind/unbind/cleanup plumbing in `buildingPipe.cpp` (no shader change yet); verify no state leaks with a debug PS that samples s9.
6. **Add the new Perlin stochastic entries** (Option A) and select them when `NoiseSystem=1`; keep the old IDs.
7. **A/B test** `NoiseSystem=0/1`, `stochasticTexturing=0/1`, all building modes, detail on/off, env on/off.
8. **Migrate customers one at a time:** CarPaint_Reflections (live duplicate) → water detail → SSAO noise → DynamicSky/IBL → cloud shadow → PBR building (phase 2). Keep each migration behind its own compile-time or INI gate until verified.
9. **Update docs** and delete the dead duplicate shaders.

---

*End of report. No source files were modified.*
