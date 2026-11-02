> SUPERSEDED by 2026-10-21-alpha-reversal-findings.md (kept for history)

# 2026-10-20 Crash Recovery + Video Evidence Review + Wave-3 Plan

Status doc consolidating everything from the 2026-10-20 crash-recovery session through the
post-deploy video review. Supersedes the scattered checkpoint notes for planning purposes.
Companion: `.serena/memories/crash-recovery-checkpoint-2026-10-20.md` (wave details).

---

## 1. Session arc (what happened)

1. Context crash + PC crashes → memory restored from OpenCode DB dumps + round6-directives.
2. fix-26..30 (ghosting/blending class) reviewed by 3 split Oracle lanes → nits applied → GREEN.
3. User new direction: **PBR modern pipes + GTA V water + Crysis features, NO IV mode**;
   fix water draw order + light alpha blending; then backwards sweep; then minimal-modular refactor.
4. Wave 1 (4 lanes): INI reconfig + water Z-state + PipeChain edge-fade/pack-gate + blend-leak probe.
5. Oracle systemic root-cause (layer model L1/L2/L3 desync) + 13-item patch plan.
6. Wave 2 (6 lanes, plan order 7→1→3→2→4→12→11→8): all landed, integrated gate GREEN,
   ASI 2,247,680 B deployed, binary string-verified.
7. User retest: "still fucked" + gameplay video recorded → frame extraction + review (this doc).

## 2. Deployed state (as of video review)

- ASI: `E:\games\gtasa_skygfx_plus\skygfx.asi` 2,247,680 B (wave-2 integrated build).
- INI: pipeline=PBR, buildingPipe=PBR, vehiclePipe=Modern, colorFilter=Modern, ivMode=0,
  gamePreset=-1, ssaoEnable=0, ssaoTemporalEnable=0, waterStyle=V, waterQuality=3,
  pipeChainEnable=1, pipeChainIntensity=0.3, **pipeChainReflEnable=1** (set this session),
  profilerEnable=0, postfxDumpDebug=0. Backup: skygfx.ini.bak.
- Log forensics (21:23-21:24 run): CoronaShield=72 (broken entry state every window),
  **corona_draw=0** (draw-time shield NEVER fires), pc_nbdead=0, ibl_indoor=0, pc_badpack=0,
  pc_refloff=0, sss_nomask=0, blend_leak=0.

## 3. Video evidence — `C:\Users\aaaaaaaaa\Videos\2026-10-20 21-21-36.mp4` (in-game 10:22→11:58)

Frames extracted to `%TEMP%\opencode\vidframes\f001.jpg`…`f013.jpg` (fps=1/8, 960w) via
imageio_ffmpeg bundled ffmpeg (`…\Python310-32\site-packages\imageio_ffmpeg\binaries\ffmpeg-win32-v4.2.2.exe`).

| Frame | Time | Evidence |
|---|---|---|
| f001 | 10:22 Ganton | **Whole viewport BLACK** except HUD — "screen geo black at certain angles" confirmed |
| f003 | 10:38 Grove St | **Black ink-blot pools** on lawn (2 large), black rectangles near houses, dark/desaturated scene, grey horizon band |
| f005 | 10:54 road | Grey haze washout; **black square dots in sky = corona quads still squares**; car roof/glass black |
| f007 | 11:10 Willowfield | **Distant cars PURE BLACK** (lighting collapse), poles black, grey washed horizon |
| f009 | 11:26 top-down | Extreme white/grey washout; **giant white blotch over whole road = reflection pool at full-screen scale** |
| f011 | 11:42 beach | Green fog horizon band; **black triangle wedge in water** (pool on water); water plane wrong/see-through |
| f013 | 11:58 swimming | **Blown-out WHITE beach/water plane**, CJ legs dark — exposure/water ordering wrong |

## 4. Live bug classes after wave 2 (the wave-3 targets)

1. **B1 — Reflection composite still painting garbage** (f003/f009/f011, f001 likely same class):
   PipeChain pass-3 reflection draws huge wrong samples — black pools on land AND water,
   full-screen white blotch, and plausibly the f001 whole-screen black (a black reflection
   sample at grazing angles covering the viewport). Lane D's shader depth-consistency gate did
   NOT stop it. Hypotheses (in order):
   - (a) depth-gate CSO not actually in the deployed ASI (compiled to resources/cso but
     Resource.rc embedding stale / build order race);
   - (b) depthTex not bound / wrong register at pass-3 → gate reads garbage;
   - (c) projInfo constants wrong at pass-3 → linear-Z reconstruction garbage → gate never rejects;
   - (d) gate logic inverted/wrong tolerance (1.0m thickness too loose; pr.z vs envLin mismatch).
   Immediate relief valve: `pipeChainReflEnable=0` in INI (kill-switch works, pack=0 path).
2. **B2 — Corona quads still black squares** (f005 + corona_draw=0 log): the draw-time raw shield
   (coronaDrawRawShield + SpriteFlushIndexedDraw_hook at 0x70CF62/0x70CF9B) NEVER executes.
   Hypotheses: InterceptCall install failed/not reached; g_inCoronasRender flag never set
   (hook not entering trampoline path?); the buffered-flush draw entries not the actual quads path
   for these sprites (maybe marker/other batch). CoronaShield entry repairs (72×) don't stick
   because vanilla body re-breaks mid-window.
3. **B3 — Lighting collapse on distant objects** (f005/f007): distant cars/poles render pure black
   while haze washes the scene. Likely ambient path: Forward+ registers 0 lights (known
   "tiles lit=0 instances=0"), ClampAmbientToSky outdoor law still crushing ambient at distance,
   or Lane E/Lane D luminance changes over-darkening lit objects. Needs a lighting-term audit.
4. **B4 — Global washout / overexposure + wrong water** (f009/f011/f013): white-grey haze over
   scenes, blown-out beach/water, water plane see-through with terrain visible, black wedge on
   water. Water Z fix landed (wave 1) but look/order still wrong; exposure chain (ColourFilter_Modern
   + haze + reflection white-blotch contamination) suspect.
5. **B5 — Screen black at angles** (f001): likely extreme case of B1 (reflection sample covering
   viewport) — verify by disabling pipeChainReflEnable and re-checking the angle.

## 5. Known root causes already established (do not re-research)

- L1/L2/L3 layer model: raw device writers desync caches permanently; double no-op lock.
  Park-and-set idiom is the canonical fix (pipeForceCullMode pattern).
- CCoronas::Render (0x6FAEC0) batches via RW TempBuffer → flush 0x70CF20 draws 0x70CF62/0x70CF9B
  → 0x734EA0. 0x70E780 is CPointLights-only.
- INI precedence: menu_inject.cpp direct writes bypass INI; gamePreset=-1 (PRESET_CUSTOM) inert.
- chars_classifyClump unwired = SSS composite gated to no-op (CJ depth-mask ghost fix, wave 2).
- envmap.cpp:349 non-deref CGame__currArea compare is a latent always-false bug (sweep item).
- Deferred sweep items: bldEnvP[2] gate (buildingPipe.cpp:2202-2205), c20.x SkyHemisphereTint
  gates (buildingPipe.cpp:1983-1984, vehiclePipe.cpp:2828-2835) need `&& *CGame__currArea == 0`.

## 6. Wave-3 plan (execution order)

### Wave 3A — Diagnosis (parallel, read-only, ~3 lanes)
- D1: **Reflection gate audit** — verify PipeChain.cso embedded in deployed ASI vs
  resources/cso (hash/strings); trace pass-3 depthTex binding + projInfo upload; check gate logic
  against actual depth range. Deliver: exact reason B1 survives.
- D2: **Corona shield reachability** — verify InterceptCall installs at 0x70CF62/0x70CF9B fire,
  g_inCoronasRender lifecycle, whether these sprite draws are the corona/marker path at all
  (f005 squares + yellow marker triangle = same class?). Deliver: exact reason corona_draw=0.
- D3: **Lighting/exposure audit** — why distant objects pure black + scene washout: ambient terms,
  Forward+ zero lights, ClampAmbientToSky outdoor behavior, ColourFilter_Modern gain.

### Wave 3B — Fixes (after 3A findings; parallel by file)
- F1: Fix reflection gate per D1 (or gate correctly + keep kill-switch). Target: B1+B5 gone.
- F2: Fix corona draw shield per D2 (move to actual draw entry / fix install). Target: B2 gone.
- F3: Fix lighting collapse + exposure per D3. Target: B3+B4 reduced.
- F4: Water look pass if D3 implicates water exposure (waterPipe style-2 tone).

### Gates
- One integrated build gate after 3B (no per-lane builds — proven build-race rule).
- INI A/B lever for user: pipeChainReflEnable 0/1 to disambiguate B1/B5 instantly.
- User retest with video again; success = no black pools/squares, no full-screen black/white,
  distant objects lit, water behind geometry.

## 7. After wave 3 (unchanged backlog)

- Backwards sweep: similar draw-order/state bugs in all layers (deferred currArea gates,
  envmap.cpp:349, motion-blur velocity ped handling, RenderEverythingBarRoads ped-skip).
- Refactor to minimal footprint + maximal modularity (post-wave-3, per user).
- Halloween (Oct 31) deadline for the full open-issue backlog.
