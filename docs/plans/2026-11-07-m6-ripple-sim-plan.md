# M6 — GPU Ripple Simulation (spring/height-field) — Implementation Plan

Date: 2026-11-07
Status: plan (no code this wave)
Owner: water lane (M6 is spec §5 "Phase 3 — deep systems")
Depends on: spec `docs/plans/2026-10-30-water-overhaul-spec.md` §4 contract #2 + §5 Phase 3

---

## 0. What M6 is (and is not)

- **Real M6** = a GPU spring/height-field ripple simulation: a small world-space
  height grid stepped on the GPU each frame (sim-RT ping-pong), sampled by the
  water surface shader to add short-wavelength ripples on top of the analytic
  c11 swell. Disturbances are injected by rain, boat wakes, and the player
  wading/swimming. Gated to `waterQuality` tier 3 (`tier-3 gated`, spec §5).
- **Per spec §4 contract #2**: the sim's height field is **GPU-side and
  visual-only**. Physics (softbody + Bullet ragdoll) keeps the analytic c11
  swell (`water_swell.h` `WaterSwell::Height`, this wave's M9 unification) and
  treats ripples as cosmetic — ripple amplitude ≪ buoyancy tolerance. RAGE does
  the same: the sim grid is cosmetic beyond `GetWaterLevel`.

### 0.1 The c17.w naming caveat (must be recorded, else M6 looks already-done)

The existing water PS already labels a constant **"M6"**, and that label is
**wrong for the ripple sim**. Concretely:

- `src/render/waterPipe.cpp:2434` — comment: `// ---- M4/M5/M6: water FX params 2, c17 ----`
- `src/render/waterPipe.cpp:2448` — `// isecFoamGain (M6, per-texel isec foam mask): ...`
- `shaders/ps/Water_Parallax.hlsl:173` — `waterFxParams2 : register(c17); // M4/M5/M6: (refractValidGain, causticsScale, causticsSpeed, isecFoamGain)`
- Upload site: `src/render/waterPipe.cpp:2462` `RwD3D9SetPixelShaderConstant(17, waterFx2, 1);`

The c17.w component (`isecFoamGain`, "per-texel isec foam mask") is **the M3
soft-intersection foam mask** (spec M3 = "soft-intersection foam"), NOT a
ripple sim. It is an *M3 extension* that was mislabelled "M6" in the comment.
The **real M6 milestone is this ripple sim** (spec §5 Phase 3:
"M6 ripple sim (new waterSim.cpp, tier-3 gated)").

**Action**: the M6 implementation must NOT collide with c17.w. The ripple sim
gets its own sampler + constants (see §3); when landing M6, fix the `// M6`
labels at `waterPipe.cpp:2434/2448` and `Water_Parallax.hlsl:173` to say "M3
(isec foam mask)" so the milestone names stop cross-wiring.

---

## 1. Sim design — GPU spring/height-field, ping-pong

### 1.1 State

Two (optionally four) small floating-point render targets, ping-ponged:

- `h0` / `h1` — height at time t / t+1 (one ping-pong pair).
- `v0` / `v1` — vertical velocity (optional second pair; packs the spring's
  momentum so the recurrence is stable at large `dt`).

Packing choice (D3D9/SM3.0 friendly): height in `.r`, velocity in `.g` of a
single RT pair — halves the RT count to **2** and keeps one texture bind.
Format: `D3DFMT_A16B16G16R16F` (16-bit floats, broad SM3.0 support) or
`D3DFMT_R32F` where available; fall back to `D3DFMT_A8R8G8B8`-encoded fixed
point only if neither float format exists on the target GPU. Grid: 128×128
(256×256 at tier 3 on strong cards), world-extent ~64 m around the camera —
the same spatial domain as the W7-B water grid in `waterPipe.cpp` (`WGRID_N`,
`waterPipe.cpp:411`).

### 1.2 Step recurrence (PS kernel)

Standard damped wave equation, second-order in time, computed per texel:

```
v' = (v + c^2 * lap(h) * dt) * (1 - damping*dt)
h' = h + v' * dt
lap(h) = h(x+1) + h(x-1) + h(y+1) + h(y-1) - 4*h(x)   // neighbour fetches
```

- `c` = propagation speed (m/s), `damping` = per-second decay, `dt` = frame
  delta (clamped, project 1e-7 rule — NaN writes black on SM3.0).
- All arithmetic in the PS, no dynamic loops, fixed 4-tap neighbourhood
  (SM3.0 rule: no `tex2Dlod` in ps_3_0, use `tex2D` with point sampling).
- Ping-pong: bind `h_prev` as `s0`, render the kernel into `h_next`, swap.

### 1.3 Disturbance injection

A tiny "splat": draw a 1×1 (or small N×N) quad into the *current* RT at the
world→grid UV of the disturbance, adding `+impulse` to `.r` (or `.g`). One
quad per event; events are queued CPU-side each frame (see §2) and flushed in
one `DrawPrimitiveUP` batch before the step, or merged into the step via a
second small PS.

### 1.4 Consumption by the water surface

`Water_Parallax.hlsl` samples the sim height RT in its water UV space and adds
`rippleHeight` to the swell — **visual only** (§0). Two touch points:
1. VS `Water_VS.hlsl` — optional: add the ripple to the displaced vertex (cheap;
   gives geometric wobble on the quad). Deferred if the VS vertex-texture
   budget is tight.
2. PS `Water_Parallax.hlsl` — add ripple height to the normal perturbation so
   ripples catch specular/foam (the primary visual win, no geometry change).

### 1.5 Device lifecycle (waterPipe_OnDeviceReset extension)

- `waterPipe_OnDeviceReset()` — `src/render/waterPipe.cpp:1067` — currently
  just `WaterPipe_ReleaseDeviceObjects()`. **Extension**: also call
  `waterSim_OnDeviceReset()` (new), which releases the ping-pong RTs (all are
  `D3DPOOL_DEFAULT`, killed by Reset).
- `SyncDevice()` — `src/render/waterPipe.cpp:1075` — on device replacement it
  nulls `g_sceneTexture`/`g_depthTexture`/`g_reflTexture` etc.; the sim RT
  pointers must be added to that same forget-list so a replaced device never
  leaves dangling sim-RT pointers (same discipline as `g_detailTex`/
  `g_foamTex`).

---

## 2. Disturbance-injection hooks (concrete anchors)

All three sources call the new `waterSim_Disturbance(float x, float y, float z, float strength)`.

### 2.1 Rain

- **Source of truth**: `CWeather__Rain` — defined `src/rw/gta.cpp:25`
  (`float &CWeather__Rain = *(float*)(0xC81324);`), extern `src/rw/gta.h:178`.
  Existing read precedent: `src/render/neopipe.cpp:2099`
  (`if(!NoRain() && CWeather__Rain != 0.0f ...)`).
- **Injection point**: the per-frame sim step (`waterSim_Update()`, §4 Leaf 3).
  When `CWeather__Rain > 0`, emit `N = f(CWeather__Rain)` pseudo-random
  disturbances spread over the grid each frame (N clamped; a fixed, low-N
  scatter keeps the SIMD/ALU cost bounded — no per-drop loops in shader).

### 2.2 Boat wakes

- **Hooks (already exist)**: `WakeDirectFlush1_hook` (`src/core/main.cpp:7059`)
  and `WakeDirectFlush2_hook` (`main.cpp:7066`), both delegating to the shared
  `WakeDirectFlush_Draw` (`main.cpp:6971`), installed at `RenderWakeSegment`
  `0x6EA2E8` (`main.cpp:9559`) and `RenderBoatWakes` `0x6EDD7C` (`main.cpp:9560`).
  The wake verts are already world-space (`main.cpp:6949-6950`).
- **Injection point**: inside those two hooks, per flush, emit a disturbance at
  each wake-segment vertex (or one per segment centroid). Parallel anchor: the
  existing GPU-particle wake path `GPUParticles_WakeSpawnFromTempBuffer`
  (`src/render/particles_gpu.cpp:252`) / `WakeSprite`
  (`src/render/particles_gpu.cpp:154`) already rides these flushes — the ripple
  disturbance lands at the same sites.

### 2.3 Player (wading / swimming)

- **Source of truth**: `FindPlayerPed(int)` — declared `src/rw/gta.h:174`,
  defined `src/rw/gta.cpp:35` (`WRAPPER CPlaceable *FindPlayerPed(int) { EAXJMP(0x56E210); }`).
  Player world position: `FindPlayerPed(0)->GetPosition()`.
- **Injection point**: per-frame sim step. Check the player is in water
  (reuse `SoftbodyWaterQuery` — `src/entities/softbody.h:96` — the same query
  the ragdoll/softbody lanes use), and if submerged/wading (submersion fraction
  in `(0, 1)`, velocity above a small threshold), emit a disturbance at
  `(player.x, player.y)` each frame. This is exactly RAGE's
  `CWaterTestHelper`/`GetWaterHeightAtPositionIncludingRivers` analogue.

---

## 3. New sim shader pair + register budget

New files (follow the existing `Water_*` naming + `cso/` resource convention):

| | File | Entry | Resource id | CSO |
|---|---|---|---|---|
| VS | `shaders/vs/WaterSim_VS.hlsl` | `main` | `IDR_WATER_SIM_VS` | `cso/WaterSim_VS.cso` |
| PS | `shaders/ps/WaterSim_PS.hlsl` | `main` | `IDR_WATER_SIM_PS` | `cso/WaterSim_PS.cso` |

Resource registration (mirror `IDR_WATER_VS` / `IDR_WATER_PARALLAX`):
`resources/Resource.rc` (water entries at lines 59-62, 135) +
`resources/resource.h` (`IDR_WATER_VS = 202`, `IDR_WATER_PARALLAX = 201`,
`IDR_WATER_UNDERWATER = 276` — pick unused ids for the two new ones).

**Register budget (SM3.0, D3D9)** — the sim shaders are a *fresh pair*, so they
get their own registers and do not compete with the existing water surface
pair:

- `WaterSim_VS` — fullscreen-quad passthrough; `uniform float4 simParams : register(c0);`
  (dt, gridScale, reserved, reserved). No WVP needed (clip-space quad).
- `WaterSim_PS` — the kernel/disturbance merge:
  - samplers: `s0` = `h_prev` (point), `s1` = disturbance splat RT (optional
    second bind; can be folded into `s0` when packing h+v in one RT).
  - constants: `c0` = (dt, cellSize, waveSpeed, damping), `c1` =
    (disturbanceEnabled, disturbanceCount, reserved, reserved).

**Headroom check against the existing surface shaders** (so nothing collides):

- `Water_VS.hlsl` uses **c0–c13** (`worldViewProj` c0-c3, `world` c4-c7,
  `camTime` c8, `sunDir` c9, `bodyParams` c10, `waveSwell` c11, `windParams` c12,
  `clipParams` c13 — `Water_VS.hlsl:81-88`). VS constant headroom free from c14.
- `Water_Parallax.hlsl` uses **c0–c18** + samplers **s0–s4** (`sceneTex` s0,
  `detailTex` s1, `depthTex` s2, `g_reflTex` s3, `foamTex` s4 —
  `Water_Parallax.hlsl:150-154`; registers c0-c18 at `Water_Parallax.hlsl:156-174`).
  **s5 was budgeted for a gradient LUT but folded into ALU** (`Water_Parallax.hlsl:358`
  comment) — so **s5 is free** and is the natural bind slot for the ripple
  height RT, with c19+ free for its params.

---

## 4. Ordered implementation breakdown (≤1-file leaves)

Each leaf is independently landable and compile-safe (octree leaf discipline).

1. **Leaf 1 — sim module (new file)**: `src/render/waterSim.cpp` + `waterSim.h`
   (one new module). Owns the ping-pong RTs and API only — `waterSim_Init()`,
   `waterSim_Shutdown()`, `waterSim_OnDeviceReset()`, `waterSim_Update(dt)`
   (frame-gated step), `waterSim_Disturbance(x,y,z,strength)`, `waterSim_BindHeight(int stage)`.
   No hooks, no wiring — compiles and links standalone (nothing calls it yet).
   **Landing test**: builds; no behaviour change.

2. **Leaf 2 — sim shader pair (new files + resource registration)**:
   `shaders/vs/WaterSim_VS.hlsl` + `shaders/ps/WaterSim_PS.hlsl` (spring kernel +
   disturbance merge, register budget in §3), plus `resources/Resource.rc` +
   `resources/resource.h` entries (`IDR_WATER_SIM_VS` / `IDR_WATER_SIM_PS`).
   **Landing test**: CSOs compile under the SM3.0 profile; resource load returns
   non-null; no runtime path reaches them.

3. **Leaf 3 — step + bind wiring**: `src/render/waterPipe.cpp` only.
   (a) call `waterSim_Update(dt)` once per frame (frame-gated on
   `RWSRCGLOBAL(renderFrame)` exactly like `Ragdoll_Simulate` —
   `src/extras/ragdoll_death.cpp:1140-1151` — since `waterPipe_setRenderState`
   runs from 4 flush sites via `CWaterLevel__RenderAndEmptyRenderBuffer_hook`,
   `main.cpp:7076`);
   (b) extend `waterPipe_OnDeviceReset()` (`waterPipe.cpp:1067`) +
   `SyncDevice()` (`waterPipe.cpp:1075`) per §1.5;
   (c) bind the sim height RT to `s5` + upload `c19` params in
   `waterPipe_setRenderState()` (`waterPipe.cpp:1600`).
   **Landing test**: sim RT steps and binds at tier 3; released cleanly on Reset.

4. **Leaf 4 — surface consumption**: `shaders/ps/Water_Parallax.hlsl` only.
   Sample `s5` (ripple height) and add it to the normal perturbation (and,
   optionally, `Water_VS.hlsl` for vertex ripple) behind a tier-3 gate on
   `qualityParams.x` (`Water_Parallax.hlsl:167` `c11.x` = tier). Fix the `// M6`
   → `// M3 (isec foam mask)` labels (`Water_Parallax.hlsl:173`,
   `waterPipe.cpp:2410/2423`) in the same leaf.
   **Landing test**: ripples visible on the surface; zero change at tier < 3.

5. **Leaf 5 — disturbance sources**: `src/core/main.cpp` only. Wire
   rain + player into `waterSim_Update()` (read `CWeather__Rain` gta.h:178 /
   `FindPlayerPed` gta.h:174) and emit boat disturbances from
   `WakeDirectFlush1_hook` / `WakeDirectFlush2_hook` (`main.cpp:7059/7066`).
   **Landing test**: rain/boat/player all produce ripples; no disturbance → flat
   sim → visually identical to pre-M6 surface.

**Note on scope**: `waterPipe.cpp`, `shaders/*`, and `main.cpp` are owned by
sibling lanes **this** wave — the M6 leaves above are future work and are
recorded here only as the landing map.

---

## 5. Do-not-regress + risks

- Tier < 3 must be **bit-identical** to today (all new fetches gate on the tier
  uniform; sim RT unbound → zero contribution).
- No new samplers beyond the budgeted **s5**; `c16-c31` headroom respected.
- SM3.0 rules throughout: no `tex2Dlod` in ps_3_0, no dynamic loops, 1e-7
  guarded divides, straight-line fetches only (see `shaders/include/Noise.hlsl`
  header for the canonical rules doc).
- Ripple amplitude ≪ buoyancy tolerance — physics **must not** consume the sim
  field (spec §4 contract #2); the shared swell law (`water_swell.h`) stays the
  single physics truth.
- Ping-pong RTs are `D3DPOOL_DEFAULT` — the `SyncDevice()` forget-list must
  cover them or a device replacement leaves stale pointers (same class of bug
  as the `g_sceneTexture` fix, `waterPipe.cpp:1075-1095`).
