# Water veil over land / sky band — live diagnosis (2026-10-24 21:03 run)

Read-only investigation. The only file written is this report.

**Scope:** `waterStyle=Xbox` (`waterStyle=0`), `waterQuality=0`, `waterParallaxEnable=1`,
live log `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (1,467,000 B, mtime 21:07),
24 screenshots in `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-10`
(21:04:18–21:07:14; in-game 10:32–13:06).

**Note on line numbers:** `src/core/main.cpp` was edited again at **21:19:54**, after the
21:02:54 compile / 21:03:40 deploy and after the 21:07 log. The hook is now at
`main.cpp:4118-4172` (not the ~3177-3230 in the task brief). The water draw itself is
identical; `waterPipe.cpp` mtime 2026-10-23 20:58 and `Water_VS/PS` CSOs predate the build.

---

## Top finding

**This is not a Z-state / depth-copy failure and not a blend-order failure. The water
mesh is being transformed to the wrong clip-space position (a geometry/transform fault),
so its quads are painted over dry land and above the horizon. Z-test is pinned ON and
tier 0 never runs the depth copy. The water is alpha-blended over whatever is behind it
because the misplaced fragments are *in front* in clip space, not because depth testing
is off.**

The most probable root is the water pipe's use of the **RW cached view-projection**
(`_RwD3D9ActiveViewProjTransform` via `_rwD3D9VSGetComposedTransformMatrix`,
`pipelinecommon.cpp:299-314`) for its raw `DrawIndexedPrimitiveUP`. That cache is a
global; the water flush happens mid-frame, after PBR atomics and reflection/envmap/mirror
passes, while every other custom pipe composes from the **live**
`_RwD3D9D3D9ViewTransform/_RwD3D9D3D9ProjTransform`. A reflection-camera matrix left in
the cache projects the (correct world-space) water mesh as the large off-position sheet.
The pre-existing `D3D9ActiveTransform = NULL` work-around only neutralizes the *world*
matrix, not the cached view-proj.

**Kill switch / A/B (INI only): `waterParallaxEnable = 0`** — the hook then calls the
original `CWaterLevel::RenderAndEmptyRenderBuffer` (`main.cpp:4131-4139`). If the veil
disappears, the water pipe owns it; if it remains, it belongs to another lane. Second
A/B: `waterBodyClassify = 0` (rules out the per-vertex body write).

---

## 1. Which build ran

`skygfx_dbg.log:20`:

```
[21:03:59.985] [INFO ] :0 () - [BUILD] asipath=E:\games\gtasa_skygfx_plus\skygfx.asi
  size=2317824 mtime=2026-10-24 21:03:40 built=Oct 24 2026 21:02:54
```

- Deployed ASI: **2,317,824 bytes**, mtime **2026-10-24 21:03:40**, compile stamp
  **21:02:54**.
- This is the **21:03 build**, not an older one (the previous swarm build in
  `b7396c4` was 2,061,312 bytes).
- Object timestamps agree: `build/obj/Release/waterPipe.obj` 21:03:35,
  `pipelinecommon.obj` 21:03:26.
- Log starts at 21:03:59.965 and ends 21:07:13, i.e. it covers this exact binary.
- Shaders: `[INIT] shaders: compiled OK (ps=57 vs=17), FAILED (ps=0 vs=0)` (line 175).

---

## 2. The `[water]` lines (distinct variants)

Counts: **62** `[water] entry`, **62** `[water] chain`, **62** `[water] restore`
(186 lines), plus `[water] detail normal tex` and `[water] body table` (190 total).

### Entry (62 lines, all identical except counts)

```
[water] entry: enable=1 waterStyle=0 tier=0 verts=<N> indices=<M>
  sceneRT=00000000 depthRT=00000000 router=IDR_WATER_PARALLAX
```

Count histogram (N/M):

| verts/indices | count | note |
|---|---|---|
| 2044 / 3066 | 32 | main ocean flush (1022 quads) |
| 256 / 384 | 5 | 64 quads |
| 80 / 120 | 5 | 20 quads |
| 72 / 108 | 5 | 18 quads |
| 248 / 372 | 3 | |
| 240 / 360 | 3 | |
| 96 / 144 | 2 | |
| 296/444, 880/1320, 920/1380, 928/1392, 992/1488, 208/312 | 1 each | |
| **232 / 990** | **1** | **impossible for SA water quads/tris; see §6.D** |

- `waterStyle=0`, `tier=0`, `sceneRT=00000000`, `depthRT=00000000` on **all 62**.
- No `[water] gate failed`, no device-lost, no refraction-RT-unavailable lines.
- One `232/990` flush at `21:07:11.637` has no paired chain/restore (early-out or log
  throttle) and is the only count that cannot be a water buffer (4 verts/6 indices per
  quad, 3/3 per triangle).

### Body table and detail texture (once, first flush 21:05:03)

```
[water] detail normal tex 256x256 mips=1
[water] body table: 305 planes (ocean 56 shallow 21 interior 0 river 15 pond 19 lake 194)
  invisible 2 limitedDepth 21 skipped 0
```

The classifier is reading the real water.dat tables correctly (counts/layout sane).

### Chain (62 lines, all the same shape)

Representative (`21:05:14.205`, 2044-vert flush):

```
[water] chain: style=Xbox waterStyle=0 tier=0 needRT=0 sceneRT=none depthRT=none
  reflRT=none reflFlag=0 fresnelSplit=1 body=1806919828 VS c10.gate=1.0000
  PS c14=(0.000000,0.000200,0.000000,0.000000) c15=(0,0,0,0)
  flushVerts=2044 ocean=1968 shallow=0 interior=0 river=0 pond=8 lake=68
  nearClip=0.300 farClip=1800.000 PS=2138A508(IDR_WATER_PARALLAX)
  VS=212C8C50(IDR_WATER_VS) cullPre=0x01 cullSet=NONE
  restore=paired(waterPipe_restoreRenderState)
```

All 62 chains agree on:

- `style=Xbox`, `waterStyle=0`, `tier=0`, **`needRT=0`**.
- `sceneRT=none depthRT=none reflRT=none reflFlag=0` — **no scene/depth/reflection RT**.
- `fresnelSplit=1`, `VS c10.gate=1.0000`, `PS c14` sentinel `(0, 2e-4, 0, 0)`,
  `c15=(0,0,0,0)` — body classification is ON.
- `cullPre=0x01`, `cullSet=NONE`, `restore=paired(...)`.
- `nearClip` 0.280–0.300, `farClip` 1800.000 (main camera); PS/VS handles stable.
- Histogram is ocean-dominated: 40–1982 ocean verts, 28–586 lake, 0–48 river,
  0–12 pond, only one flush with `shallow=24`, `interior=0` always. (The lake counts
  are mostly the classifier's no-table-hit fallback, not a geometry clue.)
- `body=1806919828` is a **format-string bug**: `%d` is fed `bodyOn ? "on" : "off"`
  (a `const char*`), so it prints the string pointer, not the state. Cosmetic only.

### Restore (62 lines)

```
[water] restore: VS/PS unbound, tex stages 0-3 cleared, samplers 0-3 -> linear/wrap,
  blend -> SRCALPHA/INVSRCALPHA, vertexAlpha off, cull NONE(0x01) -> restored 0x01,
  fog on, zstate -> entry snapshot (3 layers)
```

No restore-mismatch or leak lines.

---

## 3. Did tier>=2 `PostFX_CopyDepthToTexture` run?

**No.**

- `needRT = (tier >= 2)` (`waterPipe.cpp:966`); the copy is gated at
  `waterPipe.cpp:1002-1011`. Every one of the 62 entries/chains says `tier=0` and
  `needRT=0`, so `PostFX_CopyDepthToTexture` (`postfx_ssao.cpp:288-395`) was never
  entered, and `SetDepthStencilSurface(NULL)` (`:333`) never executed this session.
- The only depth-related log lines are SSAO setup, not the water copy:
  - `21:04:15.777 DepthHook: Create INTZ tex=1EEF70D8 ... format=INTZ`
  - `21:04:15.778 DepthHook: installed successfully (INTZ=yes)`
  - `21:04:15.818 InitSSAOResources: using depthhook INTZ depth texture 1920x1080`
  - `21:04:16.199 DepthHook: lazy INTZ recreate 1920x1080 after Reset`
- SSAO itself is off: `[PostFX] DrawSSAO bailing: ssaoEnable=0` (many lines).
- No `[PostFX] CopyDepthToTexture fault` line (the helper only logs on fault).
- No `DepthHook_Restore`/DS-unbind errors.

So the "depth-stencil surface left NULL by a depth copy" hypothesis is **not live in this
session**.

---

## 4. Mechanism: how the water gets over dry land / into the sky

### What the screenshots show

Read: `gta_sa_YGKAtsKnOl.png` (13:00), `gta_sa_lUcuyObJQq.png` (12:59),
`gta_sa_WAvyJqbueG.png` (12:53), `gta_sa_q0CvX0KDfy.png` (12:48). Also checked
`gta_sa_kUXHm8iMMp.png` (11:06) and `gta_sa_ALvawpNUu8.png` (10:32).

- Hard-edged, stepped translucent blue-grey sheets tile the lower scene; grass, sand,
  fences and the ship are visible **through** them. The edges are straight
  water.dat quad/triangle boundaries, not a fog or full-screen post effect.
- In the dock shots the sheet's top edge is a **near-horizontal line well above the
  ship's waterline** (ship hull waterline ~y=390 of 1080; sheet reaches ~y=100). The
  2044-vert ocean flush is the only water draw at those moments.
- In `kUXHm8iMMp.png` (downtown, 11:06) and `ALvawpNUu8.png` (10:32) the same sheet
  sits **in front of distant trees/buildings but behind the nearby player/car**. A
  correctly transformed horizontal water plane at z=0 cannot project into the downtown
  skyline from a camera on a street; it must be below the horizon at all times.
- The sheet's shading (flat sky-reflection colour, view-dependent opacity) matches the
  `Water_Parallax` Xbox core at tier 0, and the quad grids match `water.dat`.

### Which of (a)-(d)

- **(a) Z-test off at the draw — ruled out as the cause.** `waterPipe.cpp:1405-1413`
  pins `ZENABLE=TRUE`, `ZFUNC=LESSEQUAL`, `ZWRITE=FALSE` through L1 (RW cache),
  L2 (RW driver), a flush, and raw L3; nothing after that touches Z. There is no
  depth-copy run at tier 0 (§3). Z-test off could put water over *nearer* terrain, but
  it cannot move a z=0 plane above the horizon.
- **(b) water mesh genuinely covering land because of huge/wrong world scale — partly
  true, but not the mesh's authored geometry.** The world-space vertices are real
  water.dat positions (the classifier's histogram hits ocean/river/pond types, which it
  could not do with camera-space or garbage XY). The mesh is not "huge"; it is
  **projected as if it were somewhere else**.
- **(c) blend without depth write but drawn after terrain — ruled out.** If the
  clip-space positions were correct, the terrain/objects in front would still be nearer
  and Z-test would reject the water. The screenshots show water *behind the car* and
  *in front of distant buildings*, i.e. depth sorting is working; the water's actual
  position is wrong.
- **(d) something else — the raw draw's vertex transform.** The water is drawn with a
  **wrong view-projection** (and/or a foreign vertex shader/constant block), placing
  correct world-space quads at the wrong clip-space positions. Alpha blending then
  makes the terrain show through exactly as observed.

### Why the cached view-proj is the prime suspect

- The water path is the only draw that obtains its WVP from
  `_rwD3D9VSGetComposedTransformMatrix` after nulling `D3D9ActiveTransform`
  (`waterPipe.cpp:1069-1083`). When the world pointer is NULL, that function returns a
  **cached** `_RwD3D9ActiveViewProjTransform` (`pipelinecommon.cpp:299-314`), not a
  freshly composed live view/proj.
- Every building/vehicle PBR draw composes from the **live**
  `_RwD3D9D3D9ViewTransform` / `_RwD3D9D3D9ProjTransform` via
  `pipeGetComposedTransformMatrix` (`pipelinecommon.cpp:393-407`). That is why geometry
  other than water is correct while water alone is displaced.
- The water flush runs **mid-frame**, immediately after PBR building atomics (e.g.
  `21:05:14.197 PBRBuilding ...` then `21:05:14.203 [water] entry`). The log also shows
  per-frame `[MirrorPass]` and `[EnvMap] RenderSphereReflections` windows and
  `[BUILDING]/[FxAlpha] camRas=... != screen 1920x1080 — non-screen raster LIVE`.
  Reflection/envmap cameras are exactly the passes that leave a non-main view-proj in
  the RW cache if they set D3D transforms raw/end without invalidating it.
- The `D3D9ActiveTransform = NULL` work-around in the source addresses the *world*
  matrix, not the cached view-proj; the bug therefore survives it. This also matches the
  prior 20:48 diagnosis (`2026-10-24-water-tier0-diagnosis.md` §4), which already saw
  the same straight-edged sheet reaching the top of the frame and could not decide
  whether it was water.

### The sky band

It is the **far boundary of the same misplaced water mesh**, not a separate sky effect.
Because the water quads are projected with a camera/view-proj that is not the main
camera (effectively placing the plane at/above the eye), the mesh's far edge projects
above the main camera's horizon as a hard horizontal line. The stepped/rectangular
edges in the sky and over the beach are the water.dat quad/triangle boundaries. No
atmospheric or cloud code is involved.

### Secondary hardening suspect (not the top cause)

The raw draw bypasses `RwIm3DTransform`, so it also bypasses RW's Im3D setup of the
vertex format. `RwD3D9SetVertexShader` is the **value-compare macro** (`rwcore.h:3496`)
and does not set the FVF/declaration. If the last PBR atomic left a foreign vertex
declaration, positions are normally still at offset 0 (building/vehicle declarations in
`buildingPipe.cpp:2644` / `defaultFuncs.cpp:164`), so the mesh stays coherent — but the
attribute channels (normal/colour/UV) can be misread, and a declaration whose position
is not offset-0/float3 would scramble the geometry. Pin the FVF explicitly (see §6.C).

---

## 5. `state_baseline` / INI check

- `E:\games\gtasa_skygfx_plus\skygfx.ini:178` → **`stateBaselineEnable = 1`**. Present,
  value **1**.
- It **does fire**. The log has **90** `[Pipeline] pipeApplyWorldBaseline: ...` lines
  (`SceneStart` / `EffectsExit`) and the one-shot receipts at 21:04:15:
  ```
  [probe] baseline scenestart called            (21:04:15.770)
  [probe] baseline effects_exit_postfx called   (21:04:15.938)
  [probe] baseline effects_exit_renderfx called (21:04:15.941)
  [probe] baseline effects_exit_final called    (21:04:15.950)
  ```
  The `"state_baseline"` tag at `pipelinecommon.cpp:1696` is `dbglog_throttle`d to one
  line per 2 s, which is why the count is not one per frame. "Never firing" is a
  misreading of the throttle, not a runtime fault.
- The baseline only runs at **frame boundaries** (SceneStart/EffectsExit), never
  mid-frame, so it cannot repair a mid-frame water WVP or a stale shader cache.

Water INI values vs code expectations (all correct):

| Key | Live value | Code expectation | Result |
|---|---|---|---|
| `waterStyle` | `Xbox` | `StrAssoc` map `Xbox`→0 (`main.cpp:5134-5140`) | 0, log agrees |
| `waterQuality` | `0` | int 0–3 (`main.cpp:5141-5143`) | 0, tier 0 |
| `waterParallaxEnable` | `1` | int gate (`main.cpp:5065`, hook `:4128-4130`) | ON |
| `waterBodyClassify` | `1` | int gate (`main.cpp:5077`) | ON |
| `waterFresnelSplit` | `1` | int 0/1 (`main.cpp:5148-5150`), uploaded c11.w | 1 |

No config mismatch.

---

## 6. Minimal patch spec

Line numbers below are the **current working tree** (post-21:19 main.cpp edit). Do not
change the deployed 21:03 binary for this diagnosis until the A/B below is run.

### A. Main fix — never use the RW cached view-proj for the water WVP

`src/render/waterPipe.cpp:1069-1083` currently does:

```cpp
D3D9ActiveTransform = NULL;
_rwD3D9VSGetComposedTransformMatrix(wvp);
D3D9ActiveTransform = savedWorldM;
RwD3D9SetVertexShaderConstant(0, wvp, 4);
```

Minimal change (one line plus an extern):

```cpp
extern D3DMATRIX *&_RwD3D9ActiveViewProjTransform;   // pipelinecommon.cpp:285
D3D9ActiveTransform = NULL;
_RwD3D9ActiveViewProjTransform = NULL;   // <-- force recompute from LIVE view/proj
_rwD3D9VSGetComposedTransformMatrix(wvp);
D3D9ActiveTransform = savedWorldM;
```

`pipelinecommon.cpp:302-305` recomputes `view*proj` from
`_RwD3D9D3D9ViewTransform/_RwD3D9D3D9ProjTransform` whenever the pointer is NULL, so
this removes the stale-camera source while preserving the existing row-major/transpose
convention. It also respects a reflection pass: if the water flush really is in a
reflection camera pass, the live transforms are that camera's, which is correct.

If you prefer not to touch the RW global, add a helper next to
`pipeGetComposedTransformMatrix` (`pipelinecommon.cpp:393`) that composes
`transpose(liveView * liveProj)` with identity world, and call that instead.

**Diagnostic to prove it (one-shot):** log the WVP and the flush's clip-space bounds in
`waterPipe_setRenderState` after the `wvp` upload:

```cpp
// for each TempVertexBuffer[i]: p = mul(float4(v,1), wvp); track min/max(p.xy/p.w)
if(dbglog_throttle("water_wvp"))
    dbglog("[water] wvp c0=(%.3f,%.3f,%.3f,%.3f) c3=(%.3f,%.3f,%.3f,%.3f) "
           "ndc x[%.2f..%.2f] y[%.2f..%.2f] w[%.2f..%.2f]",
           wvp[0],wvp[1],wvp[2],wvp[3], wvp[12],wvp[13],wvp[14],wvp[15], ...);
```

A correct sea-level flush viewed from land must have `ndc.y` mostly < horizon (0) and
`w > 0`; the current bug should show `ndc.y` reaching into the sky and/or the sheet
starting at the wrong `w`.

### B. Pin the raw shader stage (cheap, closes the cache hole)

`src/core/main.cpp:4141-4146`, immediately before `waterPipe_setRenderState()` (or at
the end of it, before the draw):

```cpp
// RW setters are value-compare gated (rwcore.h:3496); a raw device write by
// another pass can leave the cache equal to Water_VS while the device has a
// foreign VS. Force both layers.
RwD3D9SetVertexShader(NULL);
RwD3D9SetPixelShader(NULL);
g_d3dDevice->SetVertexShader((IDirect3DVertexShader9*)Water_VS);
g_d3dDevice->SetPixelShader((IDirect3DVertexShader9*)waterPS);
*(void**)0x8E2448 = Water_VS;   // _rwD3D9LastVertexShaderUsed
*(void**)0x8E244C = waterPS;    // _rwD3D9LastPixelShaderUsed
```

This is the same invariant `src/rw/normalmap_plugin.cpp:469` documents.

### C. Pin the FVF / declaration (secondary)

Before the raw draw (`main.cpp:4157`), capture and set:

```cpp
IDirect3DVertexDeclaration9 *prevDecl = nullptr;
DWORD prevFVF = 0;
g_d3dDevice->GetVertexDeclaration(&prevDecl);
g_d3dDevice->GetFVF(&prevFVF);
g_d3dDevice->SetVertexDeclaration(NULL);
g_d3dDevice->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1); // 0x112
*(void**)0x8E2444 = NULL;      // _rwD3D9LastVertexDeclarationUsed
*(DWORD*)0x8E2440 = 0x112;     // _rwD3D9LastFVFUsed
// ... draw ...
g_d3dDevice->SetVertexDeclaration(prevDecl);
g_d3dDevice->SetFVF(prevFVF);
*(void**)0x8E2444 = prevDecl;
*(DWORD*)0x8E2440 = prevFVF;
if(prevDecl) prevDecl->Release();
```

`sizeof(RwIm3DVertex)` is 36 (`external/d3d9/rwcore.h:1733-1740`), which matches
`0x112`.

### D. Buffer-count sanity (kills the one impossible flush class)

`src/render/waterPipe.cpp:514` currently only rejects `nVerts == 0`, `nVerts > nIdx`,
`nVerts > 16384`. Add:

```cpp
if(nIdx > nVerts * 3 || (nIdx % 3) != 0 || nVerts > 4096) return false;
```

and in `main.cpp:4146` if `g_waterParallaxActive` is false, fall through to the vanilla
draw (already the `else` branch). The live log's `232 verts / 990 indices` would then
never be raw-drawn with stale index targets.

### E. Z/depth-surface hardening (diagnostic; **not** the live fix)

Although §3/§4 rule it out for this run, add the assertion the orchestrator asked for.
Immediately before `RwD3D9DrawIndexedPrimitiveUP` (`main.cpp:4157`):

```cpp
DWORD zen = 0, zf = 0, zw = 0;
g_d3dDevice->GetRenderState(D3DRS_ZENABLE, &zen);
g_d3dDevice->GetRenderState(D3DRS_ZFUNC, &zf);
g_d3dDevice->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
IDirect3DSurface9 *ds = nullptr;
g_d3dDevice->GetDepthStencilSurface(&ds);
if(dbglog_once("water_drawstate"))
    dbglog("[water] drawstate zenable=%u zfunc=%u zwrite=%u ds=%p", zen, zf, zw, (void*)ds);
if(ds) ds->Release();
```

Expected: `zenable=1 (D3DZB_TRUE)`, `zfunc=4 (D3DCMP_LESSEQUAL)`, `zwrite=0`,
`ds != NULL`. If `ds == NULL` or `zenable != TRUE`, raw-pin the three states right
there (after every possible RW flush) and fall back to the vanilla draw if the DS is
still NULL:

```cpp
g_d3dDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
g_d3dDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
g_d3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
```

Do **not** expect this to remove the veil: at tier 0 the pin in `waterPipe.cpp:1405-1413`
is already intact, and the geometry is wrong.

If tier>=2 is ever used, keep the `PostFX_CopyDepthToTexture` contract
(`postfx_ssao.cpp:388-390` re-binds `oldDS`) and add a one-shot log of `oldDS` vs
`g_intzSurf` at `:304`. The current code already returns false when INTZ is not the
active DS.

### F. INI levers (no rebuild)

| Lever | Value | Effect |
|---|---|---|
| `waterParallaxEnable` | **0** | **Kill switch.** Hook calls `CWaterLevel__RenderAndEmptyRenderBuffer` (vanilla Im3D path). If veil persists, water pipe exonerated. |
| `waterBodyClassify` | 0 | Disables the per-vertex NORMAL-channel write + c14 sentinel; tests the body classifier. |
| `waterFresnelSplit` | 0 | Legacy composite: lower alpha / less veil visibility, but geometry stays wrong. Diagnostic only. |
| `waterQuality` | 0 | Keep at 0 for A/B; do **not** raise to 2/3 to "fix" (that enables the depth/scene copies and changes the path). |

The decisive first test is `waterParallaxEnable = 0`.

---

## 7. Evidence caveats

- The earliest screenshots at 21:04:18–21:04:55 (`ALvawpNUu8.png`, 10:32; etc.) predate
  the first logged water draw at 21:05:03.832. They show the same sheet, so either the
  water pipe drew without logging during those frames (unlikely: `waterentry` throttle
  logs the first call) or the sheet has a second contributor. The 21:05:14+ shots
  overlap logged water flushes and are conclusive. The INI kill switch decides the
  21:04 frames.
- The two 20:48 findings (`2026-10-24-water-tier0-diagnosis.md`) already flagged the
  same straight-edged wedge and left it undecided; this run adds the city/dock frames
  and the tier-0 no-depth-copy proof.
- The `[PipeChain]` lines (365) show a healthy post chain: classify RT 2048x1024 /
  1920x1080, edge-fade mask created, `c1chk ok`, `ran: intensity=0.30
  classifyBegins=128..4031`, `camera raster repainted from pRasterFrontBuffer`. No
  error/fallback lines, and no full-screen PipeChain pass that could create the
  water.dat-shaped quads.
- The water classifier's `body=` pointer print and the `232/990` flush are the only
  anomalous log artifacts.
