# 2026-10-21 — Alpha-Reversal + Water/Washout Schematic Findings (post network-crash consolidation)

Authoritative shared record for re-dispatched lanes. Read this FIRST — do not re-derive.
Sources: b75 session state, recovered partial lane transcripts (CPU alpha-state map lane
ses_ea05da62…, water/washout lane ses_ea05c5a25ffeMivk0m6hcQNBU4), log forensics of the
18:19 run (E:\games\gtasa_skygfx_plus\skygfx_dbg.log), video forensics 2026-10-21 18-19-56.mp4.

## User headline directive (m0290)

ONE inverted alpha/blend semantic shared across ped/vehicle/building pipes: solid faces not
solid, cars+peds selectively see-through, intro cutscene chars broken, coronas that should be
translucent/cut-off show as BLACK. Plus water flat teal + washout (cf_gain never fired).

## Verified buildingPipe.cpp state map

- saveRenderState :409 / restoreRenderState :430 — full alpha/blend/fog/zwrite bundle.
  rwRENDERSTATEZWRITEENABLE saved :416, restored :437.
- PS2 cb dual-pass: save :1143; savedVtxAlpha :1267; ZWRITE FALSE via L1 :1292 + L2 :1293
  + raw L3 :1308; comment :1313 "vehicle twin hard-codes ZWRITE=TRUE here"; restore
  :1318-1319/:1335; vtx/alpha via savedVtxAlpha :1328-1329/:1339.
- Xbox exit :1651-1663 same savedVtxAlpha mirror. Sphere :1690/:1757.
- PBR :2077/:2280; zwriteThreshold road-decal dual-pass :2253-2280 (Begin keeps ZWRITE off).
- Switch entry :1849-1852 forces ZWRITE TRUE (IBL pass can leave it off).
- Switch entry three-layer ZWRITE guard :2347-2378 mirrors vehiclePipe.cpp:2570.

## ROOT EVIDENCE — raw device writes vs rw cache

postfx RAW D3DRS_ZWRITEENABLE writes at postfx.cpp:3451, 3630, 3703, 3741, 3791, 5140 leave
DEVICE zwrite off while rw cache says on → recurring "[BUILDING/PED/VEHICLE] ZWRITE
off/desynced on entry" repairs (log: 19/43/26 per run). postfx also raw-sets
D3DRS_ALPHATESTENABLE=FALSE in ~a dozen places (pipelinecommon.cpp:1439 comment).

## Alpha-test doctrine (pipelinecommon.cpp:1380-1530 pipeEnterAlphaMode — VERIFIED)

- Entry forces: blend SRCALPHA/INVSRCALPHA (L1+L2+flush+L3), BLENDOP=ADD, SEPARATEALPHA off,
  COLORWRITEENABLE=ALL, VERTEXALPHA=FALSE baseline, ALPHATESTENABLE=TRUE,
  ALPHAFUNC=preserved rw func (game parks GREATER=5), ALPHAREF ref==0→1 bump.
- **GREATEREQUAL@0 accepts EVERY texel** = degenerate; ref=1 rejects alpha==0 = the
  "solid-black potted plant" fix (documented :1427-1460). pipeForceAlphaBlock (:406)
  mirrors the rule for standalone callers (CSkidmarks).
- pipeAlphaFuncToD3D :1331-1334 = single auditable conversion; rw/D3D enums identical 1..8.

## PRIMARY INVERSION CANDIDATES (ranked)

1. **Corona shield undoes the ref bump**: normalize block sets ALPHAFUNC=GREATEREQUAL +
   ALPHAREF=0 (main.cpp:2541-2544) → corona sprites get NO alpha cut at all. With the
   broken blend signature (log: `src=2 dst=2 blend=0` = ONE/ONE factors + blend DISABLED =
   opaque REPLACE), every texel incl. the RGB=0 soft border draws → BLACK SQUARES.
   Shield forces SRCALPHA/ONE ("vanilla corona additive") while effectsForceAlphaBlock
   claims vanilla = ONE/ONE (gta-reversed) — ADDITIVE-PAIR INCONSISTENCY; wrong pair for a
   premultiplied texture double-multiplies.
2. **Flush-time leak reintroduced**: SilentPatch defers the corona batch flush past
   CCoronas::Render exit restore; coronaDrawRawShield raw-forces L3 ONLY (blend ON
   SRCALPHA/ONE, ZWRITE OFF) at the flush and nobody restores afterward → next consumer
   inherits; repaired only by entry contracts (default-pipe forces canonical main.cpp:918-926).
3. **vertexAlpha whole-mesh inversion**: instData->vertexAlpha=1 if ANY vertex prelit
   alpha != 255 → WHOLE mesh blends → opaque faces beside alpha verts blend invisibly
   ("solid faces not solid"). hasAlpha: building buildingPipe.cpp:1175 (|| material alpha);
   vehicle vehiclePipe.cpp:752; ped = game skin pipeline from instData (prelit folded via
   _rpD3D9VertexDeclarationInstColor); atomicHasAlpha main.cpp:832; dual gate main.cpp:171.
4. **Shader-side alpha-from-texture**: main_building alpha = tex.a*color.a (decals/WIND);
   SA DXT3 map textures with EMPTY alpha blocks sample a=0 → solid faces invisible under
   SRCALPHA/INVSRCALPHA. VehiclePBR returns diff.a. PipeChain classify pack previously
   output alpha=packParams.a (metallicness~0) → alpha test discarded EVERY solid pixel —
   FIXED by echoing diffuse alpha (PipeChain.hlsl:237-247 comment).
5. Corona park-and-set hazard (main.cpp:2584-2607): SEH-swallowed fault between park and
   set leaves OPPOSITE (blend-off/dst-ZERO) in L1/L2; exit restore :2658+ is the backstop.

## NOT bugs (verified safe)

- Default-pipe entry VERTEXALPHA=FALSE baseline (per-mesh instance callback re-sets it).
- pipeEnterAlphaMode vtxAlpha FALSE baseline for peds (skin mesh callback re-sets per mesh).
- RestoreOneRawGeomState postfx_utils.cpp:223-242 — all 9 parks verified correct.
- pipeForceAlphaBlock: if(!dev) return :607 between L1/L2 and L3; ref==0→1 bump.

## Q1 WATER (waterStyle=V tier 3) — partial, lane died before root cause

- Water_V has NO separate source: main_v entry of shaders/ps/Water_Parallax.hlsl (1241 ln)
  → resources/cso/Water_V.cso (fast_build.py multi_entry :426-429). main_xbox →
  Water_Parallax.cso, main_iv → Water_IV.cso. VS = shaders/vs/Water_VS.hlsl (shared).
- Shader constants: c1=(normalStrength, tileScale, fresnelPower, reflectionStrength);
  c7=(camPos.xyz, **reflValid flag in c7.w**); c11=(tier, distFadeK, pierceScale,
  fresnelSplit); s3=g_reflTex = SSR flipped-scene copy. WaterSSRBlend gated on c7.w:
  0 → mix EXACTLY 0 (analytic sky tint only).
- reflValid set by CopySceneFlippedToTexture (waterPipe.cpp:670), called :994 only when
  waterStyle!=0 AND EnsureReflTexture OK (:991-994). PostFX_CopyDepthToTexture at :1006
  (tier≥2) for translucency depth.
- V preset (:1236-1239): reflStr=satf(waterReflectionStrength×1.55)=0.85; pierceScale=0.35
  (c11.z, code default — not 0.5).
- UNRESOLVED hypotheses (re-dispatch scope): (a) reflValid=0 → no SSR + flat analytic tint;
  (b) detail-normal texture missing / normalStrength=0 → no wave normals → flat cyan;
  (c) distant-water-over-sand: PostFX_CopyDepthToTexture (postfx_ssao.cpp:274-398) detaches
  depth-stencil mid-setup; restore early-return when oldDS != g_intzSurf (:298-304) leaves
  no DS surface → Z test inert. Z pin itself landed (waterPipe.cpp:1373-1403).
- Near-white sea-floor blowout likely Q2 washout, not water spec (specInt×0.3 modest).

## Q2 WASHOUT (cf_gain=0 whole run) — partial, lane died before root cause

- Gate: postfx_colourfilter.cpp:416 = `PostfxPipeChainReflRanFrame() && !s_tonemapIdentityGrade`
  (+ pipeChainEnable&&normalBufferEnable predicate); log :423-424.
- s_cfPipeChainReflRan: reset DrawPipeChain top postfx.cpp:3187; TRUE ONLY when REAL
  g_pipeChainClassifyTex bound on s4 (:3504); pc_refloff/normalBufDead paths keep it false.
- Puzzle: log shows c1chk ok×152 (ClassifyBegin running) and pc_refloff=0/pc_nbdead=0 —
  so the real pack SHOULD bind → flag true → gate open. cf_gain still 0 ⇒ either
  sceneLuma never crossed the knee floor (≤0.10 = zero correction) or an earlier early-out
  in the exposure block. Re-dispatch: read postfx_colourfilter.cpp:366-430 + :509-560 fully.
- tonemapMaxExposure: parse main.cpp:3936 (default 1.0; INI had 2.0); clamp main.cpp:391-394
  to [-0.5..1.0] → effective 1.0 (log fired ×2). debugmenu_ui.cpp:2173-2183 clamps [0.2..4.0]
  — CONFLICTING clamp semantics between the two sites; menu can push values main would clamp.

## Log corroboration (18:19 run)

CoronaShield breaker `src=2 dst=2 blend=0 op=1 zw=0 atest=1` (2 caught); [CoronaDraw] 164
flush fires, 2 blend=0 observations; FxAlpha PostFx.exit ONE/ONE leak substituted 42×;
layer drift 106×; ZWRITE force-TRUE 89× (PED 43/VEH 26/BLD 19); c1chk 152/152 ok;
ForwardPlus inert (collected=0 all run, regFed=2,2,0, pool 54569→18457, fed=0 everything
rejected id0=64); SSS dead (ssaoEnable=0 → no depth, 161 bails); cf_gain=0; pc_refloff=0;
amb_floor/ibl_indoor=0.

## Lane re-dispatch scopes

- **Alpha/CPU map (lane 2 remainder)**: postfx.cpp raw blend writes (BLENDOP=REVSUBTRACT
  :814, ALPHATESTENABLE=FALSE ~:800+), vehiclePipe Env cb regions :640-780, :1484-1490,
  :2263-2270, :2396-2431 vehicle Switch, :3135-3140 exit; building PBR exit :2240-2300.
- **Shader alpha audit**: every entry's output alpha source (PBR_Lighting albedo.a,
  VehiclePBR diff.a, main_mobileVehicle col.a+=ReflCol.b*0.125, PipeChain pack echo) +
  which C++ blend each runs under.
- **Water root cause**: hypotheses (a)(b)(c) above; read Water_Parallax.hlsl WaterV
  (:1027-1232) + waterPipe.cpp :602-737 texture helpers + postfx_ssao.cpp:274-398.
- **cf_gain**: exposure block full read; sceneLuma source; predicate truth table.
- **Oracle**: synthesize ONE inverted-semantic verdict across the above.
- **Forward+**: feed timing (regFed=2 but tiles lit=0; collect at scene entry reads
  pre-populate; feed slot runs but cull sees 0 — see b75 L5 notes).

## APPENDIX (2026-10-21, recovered-lane transcript deltas)

### Forward+ is NOT inert — INTERMITTENT (full log timeline)
`collected=` over the 18:19 run: 0 (startup 18:20:05-11) → **10** (18:20:13-23, tiles
lit=8160, instances=65280) → 0 (18:20:25-44) → 2 (18:20:44-18:21:01) → 1-7 varying
(18:21:49-18:23:29) → **long 0 stretch 18:23:31→end 18:26:09**. The long zero stretch
covers the docks/beach/underwater video frames — plausibly LEGITIMATE (morning 08:00+
lights off + no nearby vehicles); fp_fallback=1 engaged legacy 7-light path on those
frames. rawGame==feedRaw in every line (registry persists across feed→cull window, by
design). Anomalies to chase: capDrops=10 with collected=10 (what drops at <32?),
instances=65280 = 8160 tiles × 8 = every tile maxed (radii too large / grid test bug),
rawGame=10 at scene entry (pre-render chain 0x53BEE0 registers between clear 0x53E977
and RenderScene 0x53EABF — CollectLights at scene entry legitimately sees these).
ISS-5 distant-black-objects therefore survives on FALLBACK frames — legacy 7-light path
also fails for distant/secondary geometry (ambient/material path), not just FP-inert.

### Corona interleave audit (VERDICT in code comments main.cpp:2390-2470)
Frame glue ascending order (byte-verified): CClouds 0x53DCA2 → CoronaReflections
0x53DCAC → SunReflection 0x53E12B → clear 0x53E977 → RenderScene 0x53EABF →
RenderEffects 0x53EAD3. NO effectsForceAlphaBlock site can interleave between
CCoronas::Render's blend-set and its draws → the black-quad leak MUST originate
INSIDE CCoronas::Render's window (entry shield + draw-time raw shield are the only
covers). coronaDrawRawShield = raw L3 only (blend ON SRCALPHA/ONE, BLENDOP ADD,
ZWRITE OFF); effectsForceAlphaBlock = 3-layer drift detect + ONE/ONE→5/6 canonical
substitution + stale-Im2D-override sweep + pipeForceAlphaBlock. dbglog_throttle is a
64-bucket hash (idx*31+c mod 64) — inter-tag collisions can silently starve a tag's
logs (explains only 3 [CoronaFeed] lines despite the body running every frame).

### PipeChain classify-pack alpha evidence (PipeChain.hlsl:237-247 comment)
Pipes force D3DRS_ALPHATESTENABLE=TRUE during classify re-draw; pack mode previously
output alpha=packParams.a (metallicness ~0) → alpha test DISCARDED every solid pixel
(classify buffer never filled). Fix: pack mode echoes diffuse alpha (s0 = mesh diffuse
during classify) reproducing the main draw's cutout silhouette. PBR_Lighting returns
albedo.a; VehiclePBR returns diff.a. This proves the classify path is alpha-test-
sensitive and that "solid faces not solid" can manifest wherever output alpha ≈ 0
under alpha-test-ON or under SRCALPHA/INVSRCALPHA blend.

### Water constants (main.cpp + shader)
waterReflectionStrength parse main.cpp:4357 default 0.55; waterStyle map :4399
("V"→2); waterQuality :4406 clamp 0-3. WaterSSRBlend's F already carries the
waterReflectionStrength cap (saturate(F) <= 0.85 for V) — the SSR mix is doubly
gated on c7.w reflValid AND F; reflValid comes only from CopySceneFlippedToTexture
(waterPipe.cpp:670/:994, skipped if EnsureReflTexture fails or style==0).

## RESOLVED 2026-10-21 (exp-2 / exp-3 diagnosis complete)

### cf_gain ROOT CAUSE (closed)
Gate was OPEN all run; the knee never engaged because **sceneLuma is a timecycle
proxy** (postfx_colourfilter.cpp:338-340 = luma(tc.ambient)+0.5*luma(tc.directional))
and with altTimecycEnable=0 vanilla CColourSet ambient is near-black — logged samples
0.000/0.074/0.034/0.011, all ≤ knee floor 0.10 → cfGainScale=1.0 → the `if(<0.999)`
at :420 (which also wraps the cf_gain log) never entered. Call order ruled out:
DrawPipeChain :1460 runs inside ColourFilter_switch BEFORE ColourFilter_Modern :1821;
flag set :3504, reset only :3187. **Washout = pass-3 ~2.01× re-presentation (uncond)
+ knee never arms + shader autoExp window pinned to [0.5,1.0]** because main.cpp:391
clamps tonemapMaxExposure INI 2.0→1.0 (debugmenu_ui.cpp:2173 allows [0.2..4.0] —
conflict) → net 2.6-2.9×. RunFrameExposure :304 measures lum AFTER doubling (so
autoExp wants to dim but 0.5 floor + base 0.79 bounds it) — TonemapPass.hlsl:204-217.
PATCH: move knee into TonemapPass.hlsl after :212 on measured lum, flag via c8.w
uploaded at postfx_colourfilter.cpp:566; move cf_gain log outside the 0.999 gate;
align tonemapMaxExposure clamps.

### Shader output-alpha audit (closed) — master table
**buildingPBRPS = main_building of VehiclePBR_Modern.hlsl** (fast_build:423 →
BuildingPBRPS.cso). PBR_Lighting.hlsl, standalone Rubber_Vehicle.hlsl,
main_normMapVehicle = ORPHANS (loaded, never bound).
- **main() :776** returns RAW tex.a, DROPS IN.color.a entirely → highest risk
  (hasAlpha mesh + tex.a≈0 → invisible; vertex fades broken).
- **main_building :1167** = saturate(diff.a*IN.color.a); buildingPipe.cpp:2127-2134
  per-mesh sets VERTEXALPHAENABLE only, **NO L2 D3DRS_ALPHABLENDENABLE mirror**
  (vehiclePipe.cpp:2267 has it) → device blend stays pipeEnterAlphaMode OFF
  baseline → dual gate (main.cpp:215 reads L2) skips cutout pairs; alpha test
  GREATER@ref≥1 **discards tex.a==0 texels** = holes on DXT3-empty-alpha solid
  faces = "solid faces not solid" PRIMARY mechanism.
- **main_mobileVehicle :862** col.a += ReflCol.b*0.125 (env-coupled alpha, a not
  saturated :879) — inversion in disguise.
- main_rubber :1232 safe practically (tire alpha 255). main_specCarFx /
  main_ps2EnvSpecFx SAFE as wired (FX legs ONE/ONE + ALPHAFUNC=ALWAYS;
  ps2EnvSpecFx is black-quad-payload if a drift bypasses fxAdditiveBlend).
- PipeChain classify echo :245 confirmed current (tex2D(s0).a).
- Ped skin: NO skygfx PS bound (SkinSSS orphan) — ped invisibility is game-side.
FIX DIRECTION (chosen): per-mesh alpha-test bypass when !hasAlpha (3-layer, restore
via existing exit) in building PBR cb + vehicle Env cb — !hasAlpha in these pipes
means solid architecture/panels (vegetation rides CPlantMgr dual path, not these
cbs) → tex.a==0 empty-block texels must draw, not discard; PLUS buildingPipe L2
ALPHABLENDENABLE mirror :2134; PLUS mobileVehicle :862 guard. Shader floors only
if hasAlpha read-back disproves the !hasAlpha⇒solid invariant.
