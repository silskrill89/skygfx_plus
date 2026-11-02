# Xbox building pipe — face-ordering / transparency audit

Date: 2026-10-26
Build: `skygfx.asi` size **2,395,136**, mtime 2026-10-26 01:11:54, built 01:11:06
(`[BUILD] asipath=...` log line 28)
Mode: `pipeline = Xbox`, `buildingPipe = Xbox`, `colorFilterEnable = 0`,
`detailMaps = 1`, `dualPassBuilding = 0`, `texAlphaClassify = 1`,
`zwriteThreshold = 128`, `forceWindShader = 0`
Log: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`, session 01:12:26 → 01:25:52
(DLL_DETACH complete). Snapshot used for every number below; the file was
still growing while the audit started, so counts differ from an earlier read.

Scope: READ-ONLY. No source file was modified.

---

## 0. TL;DR

1. **Every building atomic is being treated as `DefinedVertexShader::WIND`.**
   `EDEDPluginAttach()` has **no call site**, so `ededAtmOffset` stays 0 and
   `GetDefinedShader()` reads byte 0 of the `RpAtomic` — which is
   `RwObject.type == rpATOMIC == 1 == WIND`. Log proof: **270/270** `[BPIPE]`
   building lines bind `IDR_XBOXBUILDINGWINDVS`, with `forceWindShader=0` in
   the INI.
2. **`TAC_FADED` is switching blending on for solid building/LOD textures**
   whose alpha channel is a uniform unused value. Log proof:
   `LAN2skyscra9b_LOD zero=0 full=0 mid=1024(100%) → FADED`, plus
   `vgsNelec_fence_lod` 84% mid and `cleargraf02_LA` 85% mid.
3. Blend ON + `ZWRITE` ON + no sorting (the decal rule only clears depth for
   *material*-translucent meshes, `buildingPipe.cpp:1921/2681`) = order- and
   angle-dependent translucency. Top-down you see one roof plane over the sky;
   at a low angle you see many stacked, blended, depth-writing wall faces.
4. Depth ordering is the **amplifier**, not the trigger. Alpha test cannot
   produce partial opacity (it only discards texels). The shader is complicit
   but not the root cause.

---

## 1. Callback flow

`CCustomBuildingDNPipeline__CustomPipeRenderCB_Switch` (`buildingPipe.cpp:3012-3129`)

| Step | Line | What it does |
|---|---|---|
| cull save + force | 3018-3022 | `pipeEnterCullMode()`; raw saved → `s_bldSwitchSavedCull`; forces sanitized saved (NONE/BACK/FRONT) |
| alpha canonicalise | 3023 | `pipeEnterAlphaMode()` (blend OFF, test ON, ref 0→1, SRCALPHA/INVSRCALPHA, ADD) |
| ZWRITE three-layer guard | 3040-3055 | save rw/drv/dev, force **TRUE** on all three |
| camRas probe | 3073-3081 | logs non-screen raster during geometry |
| alpha repair | 3095 | `pipeForceAlphaBlock(&savedAlpha)` L1→L2→L3 |
| dispatch | 3097-3113 | Sphere → PS2 → Xbox → PBR |
| exit | 3114-3127 | `pipeExitAlphaMode`, leak probe, per-layer ZWRITE restore, `pipeExitCullMode(savedCull)` |

Callback bodies:

* PS2 / GTAIV — `1198-1573`
* Xbox — `1674-2038`
* Sphere — `2042-2180`
* PBR — `2253-3010`

Only the Xbox body is analysed in write order below; the other three are
covered in §3 and §5.

---

## 2. Xbox callback — per-mesh write order and elision

### 2.1 Once per callback (before the mesh loop)

| Order | Line | State | Layer | Value | Elided |
|---|---|---|---|---|---|
| 1 | 1751-1752 | `rwRENDERSTATEVERTEXALPHAENABLE` read | L1 | saved (== FALSE, Switch canonicalised) | — |
| 2 | 1767 | `D3DRS_ALPHAREF` | L1+L2+L3 | tree profile only; log `treeAlpha(act=0)` → no-op | no (guarded by act) |
| 3 | 1770 | PS c0 colorScale | — | 1.0 | no |
| 4 | 1773 | VS c0-c3 WVP | — | transform | no |
| 5 | 1780 | VS c30/c31 day/night | — | `setDnParams` | no |
| 6 | 1797-1798 | VS c5/c6 direct light | — | `pDirect` force-upload | no |
| 7 | 1801 | VS c38-c40 env matrix | — | envmat | no |
| 8 | 1804-1813 | VS bind | — | **WIND VS in 100% of logged cases** | no |
| 9 | 1822 | `D3DRS_CULLMODE` | L1+L2+L3 | `s_bldSwitchSavedCull` | no |
| 10 | 1828 | `decalZWriteOff` local | — | false | — |

### 2.2 Per mesh, in draw order

| # | Line | State | Layer | Value | Elided? | Key |
|---|---|---|---|---|---|---|
| 1 | 1844-1845 | texture stage 0 | L3 (RW texture cache) | `material->texture`, NULL → `gpWhiteTexture` | **yes** (`BPELIDE_XBOX += 1`) | `elideLastTex0` resolved `RwTexture*` |
| 2 | 1847 | VS c32-c35 texmat | — | identity or MatFX UV matrix | no | — |
| 3 | 1856-1897 | CPU classification | — | `matPaintMarker`, `matTranslucent`, `hasAlphaLegacy`, `tac`, `blendOn` | — | — |
| 4 | 1903-1909 | `rwRENDERSTATEVERTEXALPHAENABLE` (L1) + `D3DRS_ALPHABLENDENABLE` (L2) | L1+L2 | `blendOn` | **yes** (`+= 2`) | `elideLastBlend` |
| 5 | 1921-1946 | ZWRITE decal policy | L1+L2+L3 | FALSE then TRUE | **yes** (`+= 3` per burst) | `elideLastZWrite`, `decalZWriteOff` |
| 6 | 1956-1963 | `[BldXbox]` / `[BldTex]` diag | — | — | — | process-global caps |
| 7 | 1966-1970 | VS c4 ambient | — | `buildingAmbient` | **yes** (`+= 1`) | `bpipeElideChanged4`, exact bitwise |
| 8 | 1972-1983 | VS c19 matCol + c20 surfProps | — | mat colour / packed surf (+ diffuse==0 → z=1) | no | — |
| 9 | 1988 | `rwRENDERSTATETEXTUREADDRESS` | L1 only | WRAP (env branch) | **no elision, no per-mesh restore** | leaks to following non-env meshes until cb exit |
| 10 | 1991 | texture stage 1 | L3 | env texture, NULL → white | no | — |
| 11 | 1998-1999 | VS c36 shininess/lightmult, c37 env scale | — | envData | no | — |
| 12 | 2003-2006 | PS select + bind (env) | — | `xboxBuildingPS` / noise variant | no | — |
| 13 | 2008 | PS select + bind (simple) | — | `simplePS` / `simpleDetailPS` / noise / `treeAlphaPS` | no | also binds s2 detail + s9 noise inside the helper |
| 14 | 2011-2014 | `TagRenderCB` draw | alpha func/ref | per-object ref, restores ref; SRC/DST blend restored | no | `continue` skips `D3D9RenderDual` only |
| 15 | 2016 | `D3D9RenderDual(...)` draw | alpha test + ZWRITE, restores | `dualPassBuilding=0` in this INI → plain draw; for a decal (`zwrite==FALSE`) it takes the `!zwrite` leg: ALPHAFUNC=ALWAYS → draw → restore | n/a | `main.cpp:252-301` |

### 2.3 Elision findings (Xbox)

* **No mid-loop writer of ALPHABLENDENABLE / stage-0 texture / c4 exists in
  the Xbox body.** `fixSAMP()` (`main.cpp:181-205`) only writes fog and flushes
  the RW driver cache; `TagRenderCB` (`322-338`) writes `ALPHATESTFUNCTIONREF`
  and SRC/DST blend and restores both; `D3D9RenderDual` writes ALPHAFUNC/REF
  and ZWRITE and restores both. So the three trackers are **safe within one
  callback**.
* The two "restores" are to values read at their own entry
  (`main.cpp:260-264`), i.e. the value the tracker just installed — consistent.
* **Amplifier**: `_rwD3D9RenderStateFlushCache()` inside `fixSAMP()` is what
  actually pushes the L2 blend write to the device before each draw, so the
  L1+L2 pair does take effect even without a raw L3 write.
* **Latent (PBR only)**: `PipeChain_ClassifyEnd` restores L1
  `VERTEXALPHAENABLE` from the cache snapshot and L2/L3 `ALPHABLENDENABLE` from
  the **device** snapshot (`postfx.cpp:3510-3526`). The PBR tracker
  (`buildingPipe.cpp:2759-2769`) is not invalidated after the bracket. If L1
  and L3 ever disagreed at `ClassifyBegin`, `End` can leave L2/L3 at the
  device value while the tracker still believes the cache value, and the next
  mesh with the same `blendOn` elides the repair. Today `pipeEnterAlphaMode` +
  `pipeForceAlphaBlock` make them agree at entry, so this is a soundness gap,
  not a live bug. Same for the ZWRITE tracker.
* **Not a bug but worth knowing**: the stage-0 elision is keyed on the
  *resolved* pointer, so `NULL` and an explicit `gpWhiteTexture` share one
  tracker state (documented at 1149-1159).
* `buildingPipe_cleanup()` (`371-389`) touches only s1/s2 and stage ops — it
  cannot stale the stage-0 tracker (which is a per-callback local anyway).

---

## 3. Intervening writers vs trackers

| Writer | Line | States it writes | Balanced? | Invalidates Xbox trackers? |
|---|---|---|---|---|
| `TreeAlpha_ApplyAlphaRef` | treeWind.cpp:400-442 | `D3DRS_ALPHAREF` L1+L2+L3 | yes, `Restore` at 442 | no — ALPHAREF is not tracked; log `act=0` anyway |
| `TagRenderCB` | buildingPipe.cpp:322-338 | `ALPHATESTFUNCTIONREF`, SRC/DST blend (PS2 pipe only) | ref yes; SRC/DST restored to canonical, not to the true entry value | no (does not touch blend-enable/zwrite/stage0/c4) |
| `D3D9RenderDual` | main.cpp:252-301 | ALPHAFUNC/REF, ZWRITE | yes, restores its entry values | no |
| `PipeChain_ClassifyBegin/End` | postfx.cpp:3347-3540 | ZWRITE, ALPHABLENDENABLE, FOGENABLE, RT, PS c0/c1 | yes, but L1 from cache / L2+L3 from device | **PBR only; trackers not invalidated** |
| `buildingPipe_cleanup` | buildingPipe.cpp:371-389 | s1, s2 textures; stage 1/2 ops | n/a (exit handoff) | no |
| `pipeEnterAlphaMode/Exit` | pipelinecommon.cpp:1522-1744 / 1746-... | VERTEXALPHAENABLE, blend family, test family | yes | no — runs in the Switch, before the per-callback trackers exist |
| `pipeForceAlphaBlock` | buildingPipe.cpp:543-734 | full alpha block + separate-alpha + COLORWRITE | writes only | no |
| `bpipeBlendLeakProbe` | buildingPipe.cpp:905-951 | ALPHABLENDENABLE, SRC, DST | repairs only | runs at cb exit, after the tracker scope |
| `fixSAMP` | main.cpp:181-205 | FOGENABLE/MODE | yes | no; also flushes L2→L3 |

---

## 4. Ordering / transparency mechanisms, ranked

### R1 (root cause) — every atomic is `WIND`

* `src/rw/extendedplg.cpp:8-13` — `GetDefinedShader` reads a `uint8` at
  `ededAtmOffset`.
* `src/rw/extendedplg.cpp:44-52` — `EDEDPluginAttach()` is the only writer of
  `ededAtmOffset`.
* **No call site for `EDEDPluginAttach()` anywhere in `src/`** (grep: only the
  definition + the `skygfx.h:1128` declaration).
* Therefore `ededAtmOffset == 0` and `*(uint8*)(atomic + 0)` is
  `RwObject.type`. `external/d3d9/rpworld.h:2478` — `#define rpATOMIC 1`, and
  `skygfx.h:207-213` — `enum DefinedVertexShader { DEFAULT, WIND }`, so
  `WIND == 1`.
* Log: **270/270** `[BPIPE]` lines `VS=IDR_XBOXBUILDINGWINDVS`; zero
  `IDR_XBOXBUILDINGVS`. `forceWindShader = 0`.

Consequences in the Xbox body:

* `1803-1809` — `vertexAlphaIsAlpha = false`; `setWindParams`; bind
  `xboxBuildingWindVS`.
* `1861-1863` — the legacy fallback (`TAC_UNKNOWN`) ignores
  `instancedData->vertexAlpha` for every building, so the old
  "vertexAlpha ⇒ blend" path is dead for walls.
* `1896-1897` — the foliage blend override runs for **every** building mesh.
* `xboxBuildingWindVS.hlsl:70-85` — per-vertex displacement
  `dist = (noise(windPos+Pos)-0.5)*windIntensity*(1-DayColor.a)` plus a
  `gust`, applied to x, y and 0.3*z. This is **not rigid**: each vertex of a
  wall moves by a different value, so faces shear and cross each other — the
  "ordering of faces" feel, worst at grazing angles, invisible on a top-down
  roof plane. Amplitude up to ~3.5 units.
* `xboxBuildingWindVS.hlsl:71` — `dayColor.a = IN.NightColor.a;` rewrites the
  colour alpha that the PS later multiplies into opacity.
* `xboxBuildingWindVS.hlsl:93` — no `directCol`/`directDir` term at all, so
  the sun the callback force-uploads at `1797-1798` is ignored on every
  building.
* `treeWind.cpp:280-281` — `TreeWind_OnEntity` early-returns on WIND, so the
  game's own sway is disabled everywhere too.

### R2 (direct cause of translucency) — `TAC_FADED` on uniform-alpha textures

* `texalpha.cpp:274-312` — the single rule: `nMid*100 >= nTotal*60` → FADED
  (`TAC_FADED_PERCENT` = 60 at line 77). **There is no single-value-alpha
  guard**, although `texalpha.h:15-18` claims OPAQUE covers
  "single-value alpha". A DXT3 texture whose 4-bit alpha nibbles are all 8
  (the classic "unused alpha filled with 0x88") is 100% mid → **FADED**.
* Log FADED samples (session): `LAN2skyscra9b_LOD` (3×, mid=100%),
  `vgsNelec_fence_lod` (84%), `cleargraf02_LA` (85%), `CJ_FRAME_Glass`,
  `green_glass_64`, `muddywater`.
* Xbox mapping `1874-1883`: `case TAC_FADED: blendOn = 1;`
* `1921` — `decalBlend = blendOn && matTranslucent && tac != TAC_FADED`;
  for a FADED wall `matTranslucent` is false → **ZWRITE stays ON**.
* Net: a solid wall is blended, writes depth and is never sorted. Near faces
  occlude farther ones mid-blend; compile order changes per atomic → the
  artifacts move with the view. At a low angle you see many stacked wall
  faces (worst); top-down you see a single roof plane over the sky (best).

### R3 — Switch ZWRITE repair is load-bearing

* 65/66 `[BUILDING] ZWRITE` samples: `rw=0 drv=0 dev=0` on entry; 1:
  `rw=1 drv=1 dev=0`. The game enters the building callback with depth writes
  OFF essentially every time (`buildingPipe.cpp:3040-3055` repairs it).
* Any path that draws buildings without the Switch (none today) inherits
  offset-0 depth writes — pure painter's-order artifacts.

### R4 — cull

* `s_bldSwitchSavedCull` (`1665`) is the raw saved mode; the Xbox cb
  re-asserts it through all three layers at `1822`. Log: `[Cull] enter
  saved=1` (NONE) 29× — mirror pass, correct; `saved=2` 150×.
* PBR ignores the saved value and hardcodes BACK at `2318` — would break the
  mirror pass if PBR were selected. Not the current mode.

### R5 — alpha test

* Switch forces ALPHATESTENABLE=TRUE and ref 0→1 (`pipelinecommon.cpp:1664-1671`).
  Log: 104/247 `[PipeAlpha] entry resync` with `rw ref=0`; 1 device sample
  `test=0` (repaired). Alpha test only removes texels; it cannot produce
  partial opacity.

### R6 — `D3D9RenderDual`

* Disabled in this session: `dualPassBuilding = 0`. Gate is
  `dual && hasAlpha && zwrite` (`main.cpp:277`) with `hasAlpha` read from the
  **L2** `D3DRS_ALPHABLENDENABLE` (`main.cpp:259`). Perfectly placed to
  re-introduce the dual legs if the INI is flipped; the Xbox L2 mirror
  (`1909`) keeps that gate honest. Not a factor in the reported run.

### R7 — `gpWhiteTexture`

* `buildingPipe.cpp:1164-1173` resolves NULL to `gpWhiteTexture`
  (`src/rw/gta.cpp:17` = game global `0xB4E3EC`).
* `texalpha.cpp:790-797` classifies a NULL texture **OPAQUE** → blend off.
* `main_simple` (`buildingPipePS.hlsl:25-30`) returns
  `white * IN.color * 1.0` = the vertex-lit material colour, **not** flat
  white/black. Safe.
* Caveat: `main_xboxBuilding` (`buildingPipePS.hlsl:91-97`) is
  `tex0*color + tex1*envcolor` (additive) — an untextured env mesh with a
  white env sample and shininess would wash out. `main_xboxBuilding` was
  never bound in this log (no env-flagged building material), so it is not
  the reported artifact.

### R8 — instrumentation blind spots

* `s_bldMeshLogged` (`1671`) and `s_bldTexLogged` (`1183`) are process-global
  and never reset: the 16/24 samples are consumed by the **first interior
  meshes** of the session (`lw_seat2`, `AH_*`, `la_carp3`). Outdoor walls —
  where the symptom lives — are never logged.
* `bTrace.wind` / `bTrace.tilt` are never set on the Xbox path (`bTraceBegin`
  resets them at `764-768`; nothing writes them), so the `[BPIPE]`
  `wind(tilt=0 c21c22=0)` field is dead.
* `[BldOpacity]` is the PBR tag only (`2783`); PBR never ran → 0 lines.
* `[BldElide]` shows up to **803,704** skipped writes per 2 s window — elision
  is heavily exercised (and therefore worth trusting only under the proof in
  §2.3).

---

## 5. Shaders

### 5.1 `shaders/buildingPipePS.hlsl`

* `main_simple` (25-30): `tex2D(s0,uv) * IN.color * c0.x`. Output alpha =
  `tex.a * IN.color.a`. `IN.color.a` is the VS-computed prelight alpha, i.e.
  the day/night **vertex colour alpha**, not a texture coverage mask.
* `main_simpleDetail` (46-53): same alpha, RGB gets `*2.0` detail modulation
  (can clip RGB only).
* `main_xboxBuilding` (91-97): `tex0*color + tex1*envcolor` — no `saturate`,
  but the render target clamps, and `envcolor.a = shininess*lightmult*0`
  (`xboxBuildingVS.hlsl:77`) so it contributes **zero alpha**. Not an alpha
  bug.
* No dither/discard, no `IN.envcolor` misuse.

### 5.2 `shaders/vs/xboxBuildingVS.hlsl` (root file)

* `OUT.Color = (prelight*surfDiff + ambient*surfAmb + directContrib)*matCol`
  (line 75). Alpha = `prelight.a * surfDiff * matCol.a`; `surfDiff` is forced
  to 1 by `buildingPipe_uploadSurfProps` for the common SA `diffuse==0`
  material. `matCol` alpha is the material alpha (255 → 1).
* So even the correct VS defines opacity as **vertex prelight alpha**, and
  the PS multiplies it into `tex.a`.

### 5.3 `shaders/vs/xboxBuildingWindVS.hlsl` (actually bound)

* Line 70: `intensityMask = 1 - DayColor.a` — day vertex alpha is the wind
  mask.
* Line 71: `dayColor.a = IN.NightColor.a` — the day colour alpha is
  **replaced** by the night alpha, so the blend opacity becomes
  `NightColor.a` (weights sum to 1 via `dayparam.a + nightparam.a = 1`).
* Lines 75-86: non-rigid per-vertex displacement (see R1).
* Line 93: no direct-sun term.
* `main_simple`/`main_simpleDetail` then output `tex.a * NightColor.a`.

Verdict for the shader: **not the root cause.** With blending off, PS alpha
has no visual effect except the alpha test. With blending on (R2) the shader
is the *mechanism* that converts texture alpha × vertex alpha into
translucency, and the Xbox path has no equivalent of the PBR `c48.w`
force-alpha-1 escape hatch (`buildingPipe.cpp:1870-1873` says this explicitly).
There is no missing-`saturate` / NaN defect.

---

## 6. Log evidence (snapshot 01:12:26 → 01:25:52)

### 6.1 `[BPIPE]` — 270 lines, all `cb=Xbox pipe=XBOX(1)`

| VS | PS | Count |
|---|---|---|
| `IDR_XBOXBUILDINGWINDVS` | `IDR_SIMPLEDETAILPS` | 157 |
| `IDR_XBOXBUILDINGWINDVS` | `simplePS` | 113 |
| `IDR_XBOXBUILDINGVS` | — | **0** |

Other fields in every line: `ivMode=0 fp=0 wind(tilt=0 c21c22=0 ext=...)`
`treeAlpha(act=0 en=1 prof=0 mul=1.000)` `handles pbr=... iv=... fp=00000000/00000000`.
`act=0` confirms the tree alpha lane never ran; `tilt/c21c22=0` are dead
instrumentation (§R8).

### 6.2 `[BldTex]` — 24 lines (cap reached), all `null=0 blend=0 zw=1`

| tex | count |
|---|---|
| `lw_seat2` | 12 |
| `sjmhoodlawn41` | 3 |
| `CJ_CHROME2` | 2 |
| `AH_barpole1` | 2 |
| `wall4b`, `sidewgrass_fuked`, `sidewgrass1`, `fourlanefreeway`, `CJ_bottle` | 1 each |

`null=0` everywhere → the NULL→`gpWhiteTexture` path was not exercised in the
logged window. `blend=0` on all 24.

### 6.3 `[BldXbox]` — 16 lines (cap reached), all identical decision

`tac=1 blendOn=0 matA=255 zw=1` for: `la_carp3` (×2), `wallpapkb1`,
`mp_diner_wood`, `lw_seat2`, `kit_windo_12`, `door_pan1_64_128`,
`breezewallbse`, `ah_utilbor1`, `ah_blu_paper2`, `ah_blu_paper`, `ah_bantisto`,
`AH_plnskirting`, `AH_grepaper2`, `AH_cheapredcarpet`, `AH_barpole1`.

All TAC_OPAQUE, all blend off — these are the **interior** meshes rendered
first in the session. **No outdoor mesh was ever sampled.**

### 6.4 `[BldOpacity]` — 0 (PBR callback never dispatched)

### 6.5 `[BUILDING]` — 89 lines

| Kind | Count | Detail |
|---|---|---|
| `ZWRITE off/desynced on entry` | 66 | `rw=0 drv=0 dev=0` ×65; `rw=1 drv=1 dev=0` ×1 |
| `cb camRas ... != screen` | 18 | 2048×1024 raster live during geometry |
| `alpha/blend layer-3 drift repaired` | 5 | mask `0x040` (ALPHABLENDENABLE) ×4, mask `0x004` (ALPHAFUNC) ×1 |
| `blend_leak` | 0 | signature not caught at cb exits |

### 6.6 `texalpha` classifier

Final summary: `72865 opaque, 145 cutout, 111 faded, 0 unknown, 0 faults
(cached 3453/4096)`.

FADED names (all samples): `LAN2skyscra9b_LOD` ×3 (100% mid),
`green_glass_64` ×2 (100%), `CJ_FRAME_Glass` (100%), `muddywater` (100%),
`cleargraf02_LA` (85%), `vgsNelec_fence_lod` (84%) → **`blendOn=1`** in the
pipe's own rule table. The first two are glass/water; the LOD skyscraper and
the graffiti/fence are buildings.

CUTOUT names: `macbrij1_LAe`, `steel64`, `sw_metalgate1`, `telewires_law`,
`veg_largefurs05/06` → alpha-tested, no blend (correct).

### 6.7 `[BldElide]` — 268 reports

Example windows: `Xbox=81786`, `102285`, `803704`, `45573`, `39055`. No
`[BldElide]` line reports a failed/late write, so no elision-induced
regression surfaced, but the volume also means a stale tracker would be
invisible in aggregate.

---

## 7. Answers to the three explicit questions

**Are any elided writes dependent on state another path can change
mid-callback?**
In the Xbox body, no. In the PBR body, the PipeChain bracket writes
ALPHABLENDENABLE/ZWRITE outside the trackers and restores from mixed
cache/device snapshots (`postfx.cpp:3510-3526` vs `buildingPipe.cpp:2759-2769`,
`2685-2706`); the trackers are not reset. No Xbox-path writer exists.

**Does an untextured wall now render flat white/black?**
No. `main_simple` multiplies `gpWhiteTexture` by the vertex colour
(prelight + ambient + sun) × material colour. Only the never-bound
`main_xboxBuilding` adds an additive white env term.

**Does any building mesh log `blendOn=1` that should be opaque?**
Directly, no — only 16 interior meshes were logged and all are OPAQUE. But
the classifier output proves FADED is being assigned to building/LOD textures
(`LAN2skyscra9b_LOD`, `vgsNelec_fence_lod`, `cleargraf02_LA`), and the Xbox
rule maps `TAC_FADED → blendOn=1` unconditionally. The diagnostic simply
stopped before it reached them.

---

## 8. Verdict

**(a) Blend state.** A face can only be *semi-transparent* if
`ALPHABLENDENABLE=1`; that is the state that changes, and it changes for
solid building/LOD textures because `TAC_FADED` cannot distinguish a uniform
unused alpha channel from glass. Depth ordering (c) is the **amplifier** —
blend + `ZWRITE` ON + no sorting produces view-dependent artifacts, and the
Xbox decal rule deliberately leaves ZWRITE ON for FADED meshes. Alpha test
(b) can only punch holes, never produce partial opacity. The shader (d) is
complicit (it forwards `tex.a * IN.color.a`, and the WIND VS substitutes
`NightColor.a` into that alpha) but is not the trigger.

The `WIND` misclassification (§4/R1) is the most consequential *structural*
defect in the Xbox path — it warps geometry per vertex, kills the sun term,
disables the vertex-alpha opacity fallback for all buildings and disables the
game's own tree sway — and it is almost certainly what the user is seeing as
"face ordering". It is a separate bug from the translucency and should be
fixed first (one-line: call `EDEDPluginAttach()` / read a correctly registered
plugin offset).

Recommended follow-ups (not applied):

1. Register the EDED plugin (or delete the dead plugin read) — restores
   `IDR_XBOXBUILDINGVS` for walls and re-enables the direct-sun term.
2. Add a single-value-alpha guard to `TacClassFromHist` (e.g. all-`nMid` with
   zero variance → OPAQUE) or treat uniform mid as OPAQUE for building
   textures; re-check `LAN2skyscra9b_LOD` and `vgsNelec_fence_lod`.
3. For any remaining FADED mesh that is not `matTranslucent`, clear ZWRITE
   (decal policy) so blended walls stop carving depth, or force alpha 1 for
   OPAQUE at the shader level (the Xbox path lacks the PBR `c48.w` slot).
4. Reset `elideLastBlend`/`elideLastZWrite` after `PipeChain_ClassifyEnd`
   in the PBR body (soundness).
5. Make `s_bldMeshLogged`/`s_bldTexLogged` reset per level/frame, and set
   `bTrace.wind`/`tilt` in the Xbox path so the log can see outdoor meshes.

---

## 9. Evidence index

| Claim | Location |
|---|---|
| WIND for every atomic | `src/rw/extendedplg.cpp:8-13,44-52`; `src/skygfx.h:207-213`; `external/d3d9/rpworld.h:2478`; no `EDEDPluginAttach` call site |
| `[BPIPE]` 270/270 WIND | log lines `[BPIPE] cb=Xbox ... VS=IDR_XBOXBUILDINGWINDVS` |
| FADED rule | `src/render/texalpha.cpp:77,274-312` |
| FADED samples | log `texalpha: FADED tex='LAN2skyscra9b_LOD' ...` |
| Xbox blend decision | `src/render/buildingPipe.cpp:1856-1897,1921` |
| Xbox write order | `src/render/buildingPipe.cpp:1841-2017` |
| Elision core | `src/render/buildingPipe.cpp:1094-1174` |
| PBR classify bracket | `src/render/buildingPipe.cpp:2938-2946`; `src/render/postfx.cpp:3347-3540` |
| Switch guards | `src/render/buildingPipe.cpp:3012-3129` |
| ZWRITE entry state | log `[BUILDING] ZWRITE off/desynced on entry (rw=0 drv=0 dev=0)` ×65 |
| Dual gate | `src/core/main.cpp:252-301`; `skygfx.ini:49` `dualPassBuilding = 0` |
| Shader alpha | `shaders/buildingPipePS.hlsl:25-30,91-97`; `shaders/vs/xboxBuildingWindVS.hlsl:70-93`; `shaders/vs/xboxBuildingVS.hlsl:75` |
| White-texture path | `src/render/buildingPipe.cpp:1161-1174`; `src/rw/gta.cpp:17`; `texalpha.cpp:790-797` |
