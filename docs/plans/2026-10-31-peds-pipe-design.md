# Peds Pipe Design — `peds_pipe` + skin / hair / cloth sub-modules

**Date:** 2026-10-31
**Status:** DESIGN ONLY — no source files modified.
**Scope:** GTA SA v1.0 US exe, RW 3.6 baked into the exe (WRAPPER/EAXJMP only), D3D9 SM3.0 (FXC ps_3_0 / vs_3_0), ASI plugin `skygfx.asi`.

## 0. Problem statement and user vision

> "create a custom peds_pipe so we can control peds better from there and from there we gonna create sub files relating called skin/hair/cloth and later we gonna reuse those for other items that might have it aka when we have animals introduced they be using hair thread, anything that's cloth in game will use cloth sim linked with our physics engine."

Today everything ped-visual lives as a ~470-line open-coded branch inside `myDefaultCallback` in `src/core/main.cpp` (lines 1881–2419): entry contract, dual pass, stage-0 texture resync, pAmbient override, opaque contract, exit monitor. That code is *correct* — every defect it fixes is real — but it is monolithic, per-atomic expensive in its worst case, `${main.cpp}`-coupled, and cannot be toggled or extended without touching the global callback. There is no home for hair extension (animals) or cloth simulation.

This doc proposes `src/peds/` — a pipe control point plus three sub-modules, following the proven TexDefs plugin lifecycle (`src/texdefs.cpp`) and the softbody lifecycle contract (`src/entities/softbody.h:52-63`), with cloth linked to the existing physics layer (`softbody` native backend + `g_bulletWorld` Bullet backend from the ragdoll lane).

---

## 1. Module layout

```
src/peds/
  peds_pipe.{cpp,h}   — pipe control point (hook owner, skinPipe recognition,
                        entry/exit contracts, per-clump state attach, config)
  skin.{cpp,h}        — skin/skin-material sub-module (resync + dual-pass +
                        opaque + ambient absorbed here; future skin shader)
  hair.{cpp,h}        — hair extension sub-module (card strips, per-bone
                        attach; shared thread for future animal fur)
  cloth.{cpp,h}       — cloth simulation sub-module (verlet cloth, ground/
                        wall/limit collision, AnimBlend co-existence, future
                        flags/curtains)
```

### 1.1 Responsibilities

| Module | Owns | Does NOT touch |
|---|---|---|
| `peds_pipe` | the isPedPipe branch of `myDefaultCallback`; `PedsPipeInit/Update/Shutdown`; per-clump plugin slot; `[Ped]` config parsing; receipt routing | buildings/vehicles pipeworks, `pipeEnter*`/`pipeExit*` implementations (shared, stay in `pipelinecommon.cpp`) |
| `skin` | stage-0 resync, dual-pass legs, opaque contract, pAmbient override, head-part receipt, optional FFP→shader swap | cloth/hair sim state |
| `hair` | hair volume atomics (build + draw + animate), anisotropic hair shader, per-bone attach tables | skin material state |
| `cloth` | cloth instance pool, verlet integrator, constraint solver, anchor sampling from bone LTMs, collision queries | anim frames (reads only — never captures ped bones) |

### 1.2 Public APIs

`peds_pipe.h`:

```cpp
// lifecycle (TexDefs-style: texdefs.cpp TexDefsPluginAttach / LoadDatabase /
// Shutdown; softbody.h:52-55 contract — no hooks installed inside the module)
void   PedsPipeInit(void);        // InjectDelayedPatches: pool zero + INI read. Idempotent.
int    PedsPipePluginAttach(void);// called from myPluginAttach (main.cpp:5732)
                               // — registers the clump plugin slot. Idempotent,
                               // always returns 1 (never break the game chain,
                               // mirroring TexDefsPluginAttach texdefs.cpp:316).
void   PedsPipeLoadDatabase(void);// InitialiseGame_hook slot (main.cpp:5807)
void   PedsPipeShutdown(void);    // DLL_PROCESS_DETACH — no RW/D3D9 calls
                               // (same detachment discipline as main.cpp:9087-9091)

// render entry — the ONLY call main.cpp's callback makes:
bool   PedsPipeIsPedAtomic(RpAtomic *atomic);   // replaces the inline
                               // `pipe == skinPipe` test (main.cpp:1900,
                               // skinPipe = *(RxPipeline**)0xC978C4, gta.cpp:66)
bool   PedsPipeRenderPedAtomic(RpAtomic *atomic); // full ped-span replacement;
                               // returns true iff the atomic was handled AND
                               // RxPipelineExecute ran. false => caller falls
                               // back to the legacy inline path (rollback lever).
```

`skin.h`: `SkinInit / SkinShutdown / SkinStage0Resync(RpAtomic*) / SkinEntryContract / SkinExitContract / SkinAmbientOverride / SkinDrawDual(passes)`. `hair.h`: `HairInit / HairShutdown / HairOnClumpAttach(RpClump*) / HairPreRender(RwCamera* cam) / HairDraw(...)` — same shape as `SoftbodyPreRender` (`softbody.h:54`, wired `main.cpp:4106`) so the game-loop wiring copies a known template. `cloth.h`: `ClothInit / ClothUpdate (per game frame, main.cpp:5657 neighbourhood) / ClothPreRender (main.cpp:4106 neighbourhood) / ClothShutdown`.

### 1.3 Lifecycle and device handling

- **attach:** `PedsPipePluginAttach()` runs inside `myPluginAttach` — the game's pipeline-plugin attach point already intercepted there at `0x53D903` (`main.cpp:5730-5749`); the SkinSFX module registers the clump plugin slot there, exactly as the four TexDefs slots do today (`texdefs.cpp:285-317`: atomic `0xEDED`, atomic `0x131`, texture `0xbadeaffe`, texture `0x4E4D3137`).
- **init:** `PedsPipeInit()` from `InjectDelayedPatches` (same call site as `SoftbodyInit()`, `main.cpp:8367`). Never touches the device.
- **per-frame:** no separate game-frame tick until Phase 3/4 (hair build refresh, cloth step) — wired at the same slots the softbody module already uses (`SoftbodyUpdate` `main.cpp:5657`, `SoftbodyPreRender` `main.cpp:4106`).
- **device reset:** sub-modules that own post-process rasters follow the `chars_OnDeviceReset()` precedent (`chars.h:49`); the skin module owns no device objects in Phase 1, the cloth/hair dynamic VBs are created lazily per frame like `EnsureSSSRasters` does (`chars.cpp:170-176`) — invalidate all cached device pointers on reset, rebuild on first use.
- **shutdown:** `PedsPipeShutdown()` at DLL detach — pools freed, wrap flag cleared, no `RwRenderState*`/`d3d9device` calls (softbody detach rule, `main.cpp:9087-9091` / comment `softbody.h:55`).

---

## 2. Pipe strategy: wrap, then absorb

### 2.1 Recommendation: **wrap first** (replace nothing in Phase 1)

The hook chain today:

```
game RenderScene → RpAtomic render dispatch
  ↑ InjectHook(0x7491C0, myDefaultCallback, PATCH_JUMP)   [main.cpp:8611]
  myDefaultCallback(atomic)  [main.cpp:1881]
     isPedPipe = (atomic->pipeline == skinPipe)            [main.cpp:1900]
     ped span: entry contract → dual pass → execute → exit contract [1958-2400]
     non-ped span: default-pipe contract (props/pickups)   [1901-1955]
  plus rxD3D9DefaultRenderCallback_Hook at 0x756DFE        [main.cpp:8619]
```

**Recommendation:** `myDefaultCallback` keeps ownership of the hook at `0x7491C0`. The ped branch (everything gated on `isPedPipe` — `main.cpp:1946-2140` entry, `2253-2258` receipt, `2260-2338` dual pass, `2240-2246`-equivalent ambient block `2185-2225`, `2340-2419` exit) is **extracted verbatim** into `PedsPipeRenderPedAtomic()` and `main.cpp` calls it, with the inline code retained as the `pedsPipeEnable=0` fallback. This gives byte-identical diffs per fix location during Phase 1 and a permanent A/B lever.

Why wrap, not replace: (1) the non-ped default-pipe branch (`main.cpp:1906-1954`, the props/pickups contract) shares the same callback prologue — replacing the whole callback risks re-hardcording the props path; (2) `myDefaultCallback` also carries the shader-hygiene unbind pass (`main.cpp:2152-2183`, `defaultCbShaderHygiene`) which serves non-ped atomics as well; (3) wrap keeps single-hook debugging receipts (all existing tags stay on one code path). A later per-clump callback **removal** (`RpClump` sets `renderCallBack` on every skin atomic at stream-in, bypassing `myDefaultCallback` entirely) remains available as Phase-2+ optimization once the module earns the trust; that path is pre-validated in the leaked source: per-atomic render callbacks are how `CVisibilityPlugins` dares to swap the ped callback (`gtasa_src-main/Main.cpp:1441` — skin plugin attach, `Main.cpp:1468` — `CVisibilityPlugins::PluginAttach`).

### 2.2 Per-ped state attach (plugin-style)

One **clump plugin slot** `PdsClump` (guard against `0x131`, the existing atomic-stream rights id used by the building lane, `texdefs.cpp:297-300`): pick an unused 4cc (proposed `0x50647031` "Pdp1") registered once from `PedsPipePluginAttach`:

```cpp
struct PedsClumpData {
    uint8  hasSkin:1, hasHair:1, hasCloth:1;
    uint8  partClass;          // PartType of dominant atomic (chars.h:14-25)
    uint32 flagShadow;         // cached head-part TexAlpha class (texname → class)
    HairAttachTable *hair;     // Phase 3: per-bone strand volumes
    ClothInstance   *cloth;    // Phase 4: sim instances per cloth component
};
```

- **ctor zeroes the slot at clump creation** — a recycled `RpClump*` address can never surface stale ped state (the exact rationale of the TexDefs pair ctor, `texdefs.cpp:16-19`).
- registration is **idempotent and guarded** (`edefOffset <= 0` pattern, `texdefs.cpp:87-95`), impossible to break the game chain (the always-return-1 rule already proven in `TexDefsPluginAttach`, `texdefs.cpp:316`).
- recognition short-circuit: `PedsPipeIsPedAtomic` cheap test = `atomic->pipeline == skinPipe` (matching the inline test today `main.cpp:1900`; `skinPipe` is a live alias dereference, `gta.cpp:66` — no caching of its value, race-safe).

---

## 3. SKIN — absorb the four ped fixes; future shader option

### 3.1 What moves into `skin.cpp` (Phase 1, verbatim)

| Fix | Today | New home |
|---|---|---|
| stage-0 NULL-cache recovery + park/set + L3 pin | `pedStage0Resync` `main.cpp:1781-1872` (NULL path `1798-1846`: recover from `atomic->geometry->matList`, fall back `gpWhiteTexture`; L2 `RwD3D9SetTexture` `main.cpp:1824`; L3 guarded extraction via `0xB4E9E0` ext offset, `main.cpp:1859-1866`, `TexDefsGetD3DTexture` both called) | `SkinStage0Resync` |
| ped opaque contract (capture/divergence/enforce/restore) | `PedOpaqueState` triplet capture `main.cpp:1040-1084`, diverge check `1086-1099`, enforce `1101-1161`, restore `1163-1180` | `SkinEntryContract` capture + `pedOpaqueEnforce`, `SkinExitContract` restore |
| ped dual pass (leg1 GREATEREQUAL@zt ZWRITE-on → leg2 LESS@zt ZWRITE-off **with VERTEXALPHA blend-on** F6 fix) | `main.cpp:2260-2388`, two-layer writes `setAlphaTestTwoLayer` / `setZWriteTwoLayer`, blend-on for leg 2 `2299-2308`, zt clamp `2268-2273`, zwriteThresholdPed INI lever `skygfx.h:364` (read `main.cpp:2272-2278`) | `SkinDrawDual` |
| pAmbient override + black-guard + Sep-3 W3 revert | `main.cpp:2185-2225` (guard `2207-2219`: pure-black timecycle sample vs non-black saved ambient → fallback, throttled `ped_ambient` receipt) | `SkinAmbientOverride` |
| head-part receipt (bandana/hair diagnostic) | `chars_logHeadPartReceipt` call `main.cpp:2253-2258` (impl in `chars.cpp:57`) | keep in `peds_pipe` (diagnostic router), not in skin material path |
| cull-mode A/B lever + alpha-test A/B lever + sticky ZWRITE repair | `main.cpp:2035-2064` cull, `2009-2017` pedAlphaTest receipt, `2075-2084` ZWRITE 3-layer force, sticky exit `2359-2371` | `SkinEntryContract` |
| exit [PedRS] monitor (21 x GetRenderState triplet) | `main.cpp:2391-2419+` | `SkinExitContract` |

No behavior change — the doc's acceptance probes in Phase 1 are exactly the existing tags: `ped_tex_recovered`, `ped_tex_recover_fail`, `ped_opaque_pre`, `ped_contract_first`, `ped_contract2`, `ped_atest`, `ped_cull`, `ped_dual_skip`, `ped_dual_legacy`, `PedDual`, `PedZWrite`, `ped_ambient`, `ped_rs`, `[DefCB]`.

### 3.2 Skin shader option (Phase 2, SM3.0 / FFP swap)

`pipelinecommon.cpp:810-811` already documents the reserved decision: *"`IDR_SKINPBR (cso/SkinSSS.cso)` is deliberately NOT loaded here and no render callback binds it yet (ped/skin PBR decision)."* `chars.h` already declares `extern void *SkinPBR` + `SSS_Blur` and `PartSSSParams g_partSSSParams[NUM_PARTTYPES]` is already tuned per part type (`chars.cpp:9-17` — HAIR anisotropic, CLOTH rough, EYES mirror, SKIN subsurface). The Phase-2 shader reuses those constants *verbatim*, no retuning.

**Register budget (SM3.0, ps_3_0 cap 224 const / 32 samp; vs_3_0 cap 256 const):** the skin PS mirrors the vehicle layout exactly (per AGENTS.md table — proven working under the same cap):

```
PS: c0  surfProps{ambient,0,diffuse,prelit_flag}
    c1  fxParams{fresnel,shininess,specularity,pbrLightMult}
    c2  eyePos
    c3  iblParams
    c4  cloudShadow
    c5..c11   lightCol (7 directs)
    c12..c18  lightDir
    c19 matCol (per-material modulate — the ped TXD frame colour)
    c22 pbrParams — MUST use pipeUploadPBR() (project rule, NEVER manual —
        the manual upload produced flat/dark output per AGENTS pipeline note)
    c23 paintNoise — via pipeUploadPBR() likewise
    c24 ambientColor (shared legacy: uploaded by BOTH vehicle `main` and
        building `main_building` today — the skin callback joins that lane)
    samplers: s0 skin diffuse, s2 optional mask (hairTex for head parts),
              s3 g_iblTex raw IDirect3DTexture9* (gta rule: not an RwTexture)
VS: c0..c3 WVP; c4 ambient; c5..c11 direct colours; c12..c18 dirs; c19 matCol;
    c20 surfaceProps; c24..c27 world; c29..c36 (same as vehicle VS)
```

Total ≈ 37 PS constants + 2-3 samplers — a tiny fraction of the SM3.0 budget, comfortably under the risk thresholds below. The skin shader uses `pipeUploadPBR()` (`pipelinecommon.cpp`) for `c22/c23` and `SetTexture(3, g_iblTex)` for the cubemap as `vehiclePipe` already does.

---

## 4. HAIR — sub-module contract, animal reuse plan

### 4.1 Contract

Hair is a **thread**, not a mesh: `HairThread` = a parametrized instance bound to (a) an anchor bone + local-space anchor rect, (b) a material, (c) strand-generation parameters. Existing precedent: `chars.h:3-4` already classifies ped atomics into `PARTTYPE_HAIR` by frame name + texture; the classification buffer infra exists (`g_charsClassifyTex`, `chars.cpp:118-132`) but is currently dead — `chars_classifyClump` is never called. Phase 3 **enlivens** that path with the `chars_classifyClump` call from `peds_pipe` at clump-attach.

- **mesh type:** card strips (pre-built `RpGeometry` per chunk with its own `RpAtomic`), NOT runtime geometry generation: D3D9/SM3.0 has no geometry shader — card/strip meshes are the only cheap hair representation. Cards are striped into a dynamic vertex buffer only for the anim-follow Pass; static-volumed (shine + silhouette) cards bake at attach.
- **attach:** per-bone anchor transform read off the clump's bone LTM in `HairPreRender` — mathematically identical to the softbody approach ("write by BlendKeyframe slerp into the shipped interp frame" `softbody.h:32-34`; hair always **reads** LTM, never writes anim frames).
- **shader:** anisotropic tangent-frame Kajiya-Kay on ps_3_0 (spec comes from a per-strand tangent attribute in the VS that produces the 2-card-plane frame) with `HairEnhance.hlsl`'s existing helpers — `shaders/ps/HairEnhance.hlsl` already carries the phase 1 shader remainder; new entry only compiles to `ps_3_0`.

### 4.2 Animal reuse plan

The same hair thread is the animal-fur thread. The extension point is the attach table:

```cpp
struct HairAttachTable {
    int boneTagPrimary;    // humans: HEAD (~5); animals: SPINE/TAIL
    float anchorLocal[12]; // rect extents in bone space
    int strandCardCount;   // for furs: high count, small cards
};
```

No pipeline-level conditional on "is human" — the attach table is per-clump data (Section 2.2). Animals reuse: same verlet-free (positional), same shader params template family, different table values. The **existing chars headPart classification** stays reusable because the classifier already keys on frame-texture rather than bone naming.

### 4.3 SM3.0 constraints

- per-card strand data goes through the VS at 4 constants/card upper bound (P0/P1 tangents + stiffness weights); ≤ 96 concurrent strands drawn per clump leaves the register budget trivially safe.
- card orientation needs a world-space tangential basis — cache WindVS-style per-frame globals in a c-registers block shared with `treeWind.cpp`'s conventions so the compositor stays branch-light.
- two passes max: silhouette (depth-only) + surface (spec+alpha). No third pass — the sm30 loop budget is too tight for KT + silhouette + shadow ray marches.

---

## 5. CLOTH — linked to the physics engine, AnimBlend-synchronized

### 5.1 Verlet cloth core

Per cloth chunk:

```cpp
struct ClothChunk {
    uint16 nVerts, springPairs;      // verlet mesh
    RwV3d *pos, *prev;               // two-buffer verlet state
    uint32 *pinnedMask;              // per-vertex: 0 = sim, 1 = kriged/bone pinned
    float clothStiffness, clothDamping, clothGravity;
    RwMatrix *anchorBoneLTMs[k];     // game bone LTMs (read-only)
};
```

- **integration:** explicit verlet (position/prev × 2) — the cheapest stable D3D9 era integrator; chosen over Bullet softbody as the **native default**: it reuses the 0x616E30 update cadence the ragdoll doc already validates, and does not force another `btSoftBody` world.
- **collision:**
  - ground via the **same ground query the native softbody already trusts**: `CWorld::FindGroundZForCoord` (`0x569660`, softbody doc §2.3 — table in `docs/plans/2026-10-29-rw-physics-ragdoll-re.md`, referenced from `softbody.h:27-29`) → per-vertex push-out + tangential friction,
  - water via `SoftbodyWaterQuery / SoftbodyWaterSwell` (`softbody.h:92-99`, verified no-waves core `0x6E8580`),
  - walls: ped-level line checks reuse `CWorld::ProcessLineOfSight` throttled to N samples/frame (not per-vertex) — ped body is far smaller than the collision budget the ragdoll already runs.
- **Bullet interop option (A/B, not the default):** the ragdoll lane's shared `g_bulletWorld` already exists and governs dismember/ragdoll death (`sftbdyphsx.cpp:17-24` — `g_bulletWorld` from `ragdoll_death.h`, Bullet headers included at `sftbdyphsx.cpp:22-24`). The `clothBackend` key (`native|bullet`) mirrors `softbodyBackend` (`physicscfg.h:34` — `"off"|"native"|"bullet"`), same A/B exclusivity discipline proven by `SoftbodyGetEnabled()` (`softbody.h:58-63` — two backends never drive the same corpse).

### 5.2 AnimBlend co-existence (clothes never fight animation)

Cloth **captures no bones and touches no capture bits**. The capture-bit language (`KeyFramesIgnoreNodeOrientation` bit 1 / `KeyFramesIgnoreNodeTranslation` bit 2, ragdoll doc §2.2 table lines 43-56) is reserved **only** for the softbody/ragdoll lane: `SoftbodyIsActiveForPed` (`softbody.h:59-63`) is the ownership gate — cloth falls back to a rigid-follow (pin-all vertices to the bone skin weights) whenever `SoftbodyIsActiveForPed(ped)` returns true, so cloth and the native ragdoll never fight over the same ped's frames. Posed vertices (shirt hem, collar) anchored at fixed bone-skin weights each frame follow the anim cleanly, and only the free vertices run verlet; blend weights `w_i` come from `RpSkin` weight plugins (implicitly: pinned = "weight owned by anchor bone", free = w=0 on all bones).

The atomic order is:

```
game frame → ProcessStepNeighbourhood (SoftbodyUpdate  main.cpp:5657):
               - Ragdoll/SoftbodyCP or native captures their peds
               - ClothUpdate steps ONLY cloth-attributed chunks on live peds
render frame (SoftbodyPreRender  main.cpp:4106):
               - cloth anchors sampled from bone LTMs → hardware buffer refit
               - Ragdoll_ApplyPose / SoftbodyPreRender write anim state
```

Because cloth steps between AnimBlend's own frame evaluations (same cadence the ragdoll doc defines as the safe slot, §3.3), the anim system never sees cloth's writes on the ped's main skeleton (there are none), and the anim system never sees cloth begin a sim step without a fresh LTM snapshot.

### 5.3 Reuse (flags / curtains — "anything that's cloth")

The same `ClothChunk` instance, with a different "anchor source" enum: `(ANCHOR_PEDBONE, ANCHOR_STATIC)(FLAG))(ANCHOR_STATIC)(CURTAIN)` and a collider table (AABBs/OBBs instead of the ped capsule). No sub-module of cloth special-cases peds — that's how user's vision on reuse lands mechanically.

---

## 6. CONFIG — `[Ped]` lane-scoped keys

All keys live in section `[Ped]` inside the per-lane profile files (`skygfx.ini` / the `skygfx1.ini..skygfx9.ini` per-lane profiles that `findInis()` discovers — `main.cpp:6413-6435` — and dual-parse through `readIniFile(modulePath, c)` `main.cpp:6484`). The user's rule ("config changes must affect ONLY this lane") is already mechanically enforced by the lane pattern, and we inherit it: every ped key parses into the `Config` struct being built (`c`), with lane-gated application.

```ini
[Ped]
pedsPipeEnable     = 1   ; Phase 1 wrap lever (0 = legacy inline path, permanent rollback)
skinShaderEnable   = 0   ; Phase 2 FFP→shader swap (0 = vanilla FFP skin pipe)
hairEnable         = 0   ; Phase 3 hair thread
clothEnable        = 0   ; Phase 4 cloth sim
clothBackend       = "native" ; "native" | "bullet" (g_bulletWorld)
clothStiffness     = 0.85
clothDamping       = 0.12
clothBudget        = 16  ; max concurrent cloth chunks (like softbodyBudget, softbody.h:40)
hairBudget         = 64  ; max concurrent hair chunks
hairSpecBias       = 0.35
```

Implementation discipline (verbatim reuses of existing mechanisms):

1. **parse:** new `readint(cfg.get("Ped", ...))` lines appended in `readIniFile`, following the `dualPassGlobal → dualPassBuilding/Vehicle/Ped` inheritance shape (`skygfx.h:337`, `main.cpp:6673,6687`) — each ped key inherits nothing from `[SkyGfx]`; the `[Ped]` section is authoritative for the ped lane only.
2. **isolation:** no `PedsPipe*` code ever writes `config->` fields for OTHER lanes or pushes back into `[SkyGfx]`-owned state; the ped lane consumes key reads only from its own `Config*` (matching the `c == config` active-lane guard used by pipelinecommon/modeManager — e.g. `main.cpp:6511, 6532`).
3. **round-trip:** `saveConfigTo` adds `EMIT("Ped", ...)` rows inside the existing `EMIT` macro blocks (pattern shown by `main.cpp:7384-7386` for `sssPostProcessEnable`), so named configs preserved under `configs/`.
4. **debug menu:** `debugmenu_ui.cpp` gets the ped rows using the existing `zwriteThresholdPed` row as template (`debugmenu_ui.cpp:720/1712`, referenced `main.cpp:2275-2278`). A/B receipt tags accompany each lever (`ped_atest` precedent, `main.cpp:2013-2017`).
5. **mode-manager precedence:** none — briefing `modeManager` for new modes is out of scope; `[Ped]` keys are additive mode-neutral (verified `modeManager.h` keeps an additive-key philosophy, `ApplyMode()` is the ONE writer of mode-owned fields).

---

## 7. PHASED ROLLOUT

### Phase 1 — wrap + absorb (zero visual delta)

- move the `isPedPipe` ped branch of `myDefaultCallback` (entry `1958-2140`, receipt `2253-2258`, dual `2260-2338`, ambient `2185-2225`, exit `2340-2419+`) into `PedsPipeRenderPedAtomic`.
- `pedsPipeEnable=0` fallback keeps the **original inline code** live ( permanence of the rollback lever).
- lane-scoped receipt: first-frame `[PedsPipe] wrap active (atomic=%p)` `dbglog_once` correlating with the existing `ped_opaque_pre` so a test run proves both paths compare identical.
- **acceptance probes:** (1) all Phase-0 receipts fire on the same frames as before (a screenshot-keyed `ped_opaque_pre`, `ped_contract_first`, `ped_rs` visual diff — byte-for-byte textual receipt corpus in `skygfx_dbg.log`); (2) `ped_stage0` recovery receipt (`ped_tex_recovered`) still fires on the same scripted scene (green-facet bandana test, existing `chars_logHeadPartReceipt` fixtures); (3) `pedsPipeEnable=0` toggles back to identical legacy receipts (an A/B session at 20 fps-critical scene).
- **rollback:** `pedsPipeEnable=0` in any lane's `[Ped]`.

### Phase 2 — skin shader (SM3.0 skin PS replacing the FFP skin pipe skin-lane atomics)

- `SkinSSS.cso` (already reserved: `pipelinecommon.cpp:810-811`) + per-material hook at the ped pipe callback: `skinShaderEnable=1` binds the PS; register upload via `pipeUploadPBR()` for c22/c23 (hard rule) sharing the vehicle pipeline's already-proven register block.
- per-part shader param writes driven by `g_partSSSParams` (`chars.cpp:9-17`) — no retuning.
- **acceptance probes:** `[SkinSh] first draw ps=%p]]` `dbglog_once` + side-by-side screenshots (the existing SSS-blur composite path, `chars_drawSSSBlur `chars.cpp:231` with its `sssEnable` master gate `main.cpp:7195` shows the same kind of screenshot key).
- **rollback:** `skinShaderEnable=0` per-lane → FFP skin pipe (each lane INI may hold a different value — the lever is lane-local by construction).

### Phase 3 — hair thread

- `HairAttachTable` per clump slot populated at `HairOnClumpAttach` (clump ctor path — plugin ctor computed lazily only when the headPart classifier finds hair).
- render pass: card-strip atomics drawn in `HairDraw` between the ped opaque pass and the exit contract — careful: exit contract currently restores after execute TODAY, so hair draws before the restore, or hair draws in its own callback with its own entry contract (the 3-layer pattern `main.cpp:1958-2140` is the template).
- **acceptance probes:** `[HairChunk] attach clump=%p cards=%d bone=%d` first-attach receipt; `[HairChunk] budget overflow clump=%p` throttled.

### Phase 4 — cloth sim

- `ClothUpdate` wiring already specified (Section 5.2). Bullet passthrough piped after the `clothBackend` A/B gate; native path first.
- **acceptance probes:** `[Cloth] step chunk=%p verts=%d ground=%d contacts=%d` throttled; anchor sync badness log (`[Cloth] anchor NaN` early-warning — the NaN-audit pattern `softbody.h:35` already proved necessary).

---

## 8. RISKS

| Risk | Grounding | Mitigation |
|---|---|---|
| **SM3.0 register pressure** | ps_3_0 = 224 const / 32 sampler caps. The skin/Hair/Cloth shaders must all co-exist with the vehicle PBR layout (c0..c36 PS) | Section 3.2 budget ≈ 37 PS consts, 3 samplers, per-module concurrency ≤ 4 shaders alive; no unrolled loop pressure (per-chunk hard limit 96 strands; per-PUVB lane caps 32 iterations max, well below the SM3.0 64-slot loop limit) |
| **Per-ped render cost** | The user's current complaint. Status quo per atomic: entry 3-layer contract (~15 setter calls), dual-pass second `RxPipelineExecute` (4 `setAlphaTestTwoLayer` + 4 `setZWriteTwoLayer` each), exit monitor 21 `GetRenderState` reads (`main.cpp:2391-2419`), opaque capture (~12 reads) | (1) monitor reads gated on the `dbglog_throttle` so the only non-throttled cost is 1 bool (already true today); (2) capture-once-per-clump: `PdsClumpData` caches classification + TexAlpha class so `chars_logHeadPartReceipt` and dual-pass `atomicHasAlpha` probes stop re-walking materials every atomic; (3) compare-then-write: the enforced triplet vs the captured `PedOpaqueState` can skip identical 3-layer writes (divergence check `pedOpaqueDiverged` `main.cpp:1086-1099` already computes the info — reuse it before writing); (4) `RxPipelineExecute` count strictly ≤ status-quo (dual-pass skipped defaulting to 1 when chunk is opaque — the phase-1 absorb keeps this). Net: peds_pipe must be net-neutral or cheaper than the fanned-out inline code; a per-atomic pre/post `QueryPerformanceCounter` probe pair (`dbglog_throttle("ped_pipe_perf")`) is added to prove it before Phase 2 lands |
| **RW 3.6 exe-baked constraints** | No RW 3.7 API; all RW entry points are `WRAPPER`/`EAXJMP` (proven at `texdefs.cpp:282`, main.cpp:8611 = raw `PatchJump`) | all new RW touchpoints reuse existing `WRAPPER` targets (RwRenderStateGet/Set, RwD3D9SetTexture, RwD3D9SetRenderState, RxPipelineExecute only); no new raw D3D9 pointer chase beyond the already-verified `0xB4E9E0` extraction and `TexDefsGetD3DTexture` |
| **Race with existing SSS post-process** | `chars_drawSSSBlur` owns a per-frame latch and classify buffer (`chars.cpp:122-132` — TODO notes "Nothing produces the mask today"). Phase 3 hair must not double-draw its own classification | hair classification writes its own chunk in the SAME buffer via `chars_classifyClump` plumbing (the dead TODO becomes the hair entry point), never its own latch |
| **Cloth / ragdoll exclusivity** | native softbody + Bullet ragdoll have the strict A/B gate `SoftbodyGetEnabled` (`softbody.h:58-63`) — cloth adds a third owner | cloth render-side gating is `!SoftbodyIsActiveForPed(ped)`; cloth falls back to pinned-follow (Section 5.2 capture-bit note) so a corpse cannot run cloth verts AND ragdoll verts on the same bones |
| **Lane leakage** | user rule: config changes must affect ONLY this lane | Section 6 items 1-4; agent-write discipline documented — peds lane never writes `[SkyGfx]`-owned `config->` fields, which also matches how `physicscfg.cpp` already splits physics INI ownership (`softbody.h:39-41`) |
| **Per-clump plugin slot ID collision** | existing downstream ids in play: `0xEDED` (atomic shader-defined), `0x131` (PDS building stream rights), `0xbadeaffe`, `0x4E4D3137` (`texdefs.cpp:285-317`) | new id `0x50647031` (`Pdp1`) registered with the same guarded idempotent attach; collision manifests as `RpClumpRegisterPlugin` returning `<= 0` — logged, flag delegates to legacy, chain never breaks (`return 1` rule, `texdefs.cpp:316`) |
| **Boy bandana-style head parts orientation** | prior wave: unfixable without a game-side CClothes hook (leaked CClothes surface barely exercised: only `CClothes::RenderTest()` is called, `gtasa_src-main/Main.cpp:3481`) | NOT in scope of this doc — flagged so Phase 1-4 never silently claims it; if a game-side clothes component hook becomes feasible, it is a separate wave that plugs INTO `peds_pipe`'s clump-attach path, not a rework of it |

## 9. Non-goals

- No buildings/vehicle lane code changes; no `[SkyGfx]` section changes; no new shared INI files beyond the `[Ped]` keys in the existing lane profile format.
- No per-mesh hair runtime reflection on the pipeline level — hair is generalized ped extension via the existing classifier, not a new RX pipeline.
- No GTASA vanilla-pipe replacement beyond the ped atomic callback span — props/pickup default-pipe contract stays untouched (`main.cpp:1906-1954` is never absorbed).
