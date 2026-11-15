# Junior-rewrite master plan + provenance audit + bug-hunt ranking (2026-11-09)

Lane: planner (READ-ONLY on source; this doc is the only write). No builds, no commits.
Root: `E:\dev(dave)\skygfx_plus_expIV` (the `C:\Dev\shaisse_hub` CWD is a stale mirror — never truth).
Branch `very-experimental`. Reference checkouts used (all existence-verified on disk):
`E:\SDKs\skygfx_original` = **aap/skygfx** (HEAD 926616a, 2020-06-06), `E:\SDKs\skygfx_junior` =
**JuniorDjjr/skygfx = "SkyGfx Extended"** (HEAD fb69df1, 2026-01-13), `E:\SDKs\skygfx` = Dryxio/skygfx
(MTA-integration fork; not in our lineage), `E:\SDKs\skygfx_aap_v4.2b` = aap SA 4.2b release tree,
and in-repo `backup_original/` = the **Zeneric09 snapshot** (see §1.2 — the only surviving copy).

Line-number rule (same as `docs/plans/2026-11-08-master-plan.md:7`): this tree moves under sibling
lanes; durable anchors are function names + receipt strings. Every `file:line` below was
spot-verified against live source during this recon (2026-11-09); re-verify before editing.

---

## §1 Provenance inventory

### 1.1 Method

1. File-level: three-way set comparison of `src/**` vs `skygfx_original/src` (aap), `skygfx_junior/src`
   (junior), `backup_original/src` (Zeneric snapshot). Presence/absence + line-count deltas give the
   fork layer a file entered at.
2. Region-level: `diff aap→junior` isolates junior's actual contribution per shared file (junior added
   220 lines to main.cpp, 117 to buildingPipe, 43 to pipelinecommon, 19 to postfx, 28 to texdb);
   `diff junior→backup` isolates Zeneric's (main.cpp +331, postfx +293, plus 10 new files); then each
   junior/Zeneric marker phrase (`zwriteThreshold`, `setWindParams`, `windPos`, `stochastic`,
   `CreateRoadsign*`, `D3D9RenderDual`, `CWaterLevel__RenderAndEmptyRenderBuffer`, `makeVSfromFile`,
   `COLORFILTER_VCS`, `vcTrails`, `0xEDED`, `texdb`) was grepped in the live tree.
3. Docs cross-check: `CREDITS.md:9-14` (lineage), `.serena/memories/wave-15-record.md`,
   `wave-16-record.md`, `CODEBASE_DOCUMENTATION.md`, TU header comments (merge dates).

Verdict classes: **A** = aap-original-derived · **J** = junior/SkyGfxExtended-derived ·
**Z** = Zeneric-snapshot-derived · **O** = skygfx_plus-original · **V** = vendored (out of scope).

### 1.2 The Zeneric finding (important, corrects CREDITS.md:13)

`backup_original/` is **not** a pure junior snapshot: it is strictly junior-plus on every shared file
(main.cpp 2266 vs junior 1935 vs aap 1733 lines; postfx 1901/1608/1589; pipelinecommon 434/389/346)
and carries 10 files absent from junior's GitHub src (`SMAA.cpp/.h`, `AreaTex.h`, `SearchTex.h`,
`PC_GrassRenderer.cpp/.h`, `config.cpp`, `debugmenu_ui.cpp`, `normmap_stubs.cpp`, `postfx.h`).
That is exactly Zeneric09's layer per the fork chain aap → junior → Zeneric → us (`CREDITS.md:9-14`,
readme lineage per commit `5cc5d25`). Web research (2026-11-09): Zeneric publishes as
`Zeneric09` / `ZENJWG` on GitHub (EdgeFX, Project2DFX-zen, CameraResponse, DistantStreaming) but
**no public Zeneric skygfx fork exists** — `backup_original/` is the only surviving copy of the
snapshot we inherited. Treat it as read-only evidence, never as a build input.

Canonical URLs confirmed: aap = https://github.com/aap/skygfx · junior/SkyGfxExtended =
https://github.com/JuniorDjjr/skygfx (README: "Extended alternative version … Junior_Djjr";
feature changelog on MixMods `[SA] SkyGfx v4.2b + Extended v1.7`, 2024-03-25) · Zeneric skygfx = GONE.

### 1.3 TU-level table (every translation unit in `src/`)

| TU (current path) | Lines | Verdict | Evidence (durable anchors) | Conf |
|---|---|---|---|---|
| core/main.cpp | 7805 | A+J+Z+O (layered) | aap skeleton; J regions §1.4 J1-J4; Z: config/INI cluster seed (`backup_original/src/config.cpp`, now absorbed); O: contracts/receipts/shields | high |
| core/rw_fixes.cpp | 2026 | O | header: "RenderWare-level FIX machinery … extracted from main.cpp (2026-11-09)"; owns coronaDrawRawShield/spriteFlushShield/spriteEnforceDepthContract; kill-switch `rwFixesEnable` at `rw_fixes.cpp:1791-1804`, decl `skygfx.h:1016` | high |
| core/shadows.cpp | ~804 | O (extracted) | header: "Mechanically extracted from main.cpp (byte-identical bodies…)" per wave-16 E1; `shadows_installHooks` | high |
| core/debug_handl.cpp/.h | — | O | dbglog receipt idiom, crash fortification (wave-16); C6 `va_start(ap, level)` UB at `debug_handl.cpp:358` (`dbglog_loc` signature; master-plan C1.3 cited :343-350 pre-drift) | high |
| core/debugmenu_ui.cpp | ~4800 | Z seed + O growth | Zeneric added `debugmenu_ui.cpp` (`backup_original/src/`); massively expanded by us (overlay contract `debugmenu_ui.cpp:4680-4682`, `:4762` per visual-triage) | high |
| core/debugmenu_theme.cpp | — | O | menu theming, no upstream analogue | high |
| core/modeManager.cpp/.h | — | O | mode profiles own levers (`modeManager.cpp:217-218` etc. per master-plan C21) | high |
| core/presets.cpp/.h | — | O (Z seed?) | preset table; C1/C2 anchors `presets.cpp:5-21,:126-141` | med |
| core/physicscfg.cpp/.h | — | O | no upstream analogue | high |
| core/weather.cpp/.h | A+O | aap weather hooks + our Dir-column fix (wave-15 "weather (sun fix)") | high |
| render/buildingPipe.cpp | 4183 | A+J+O | aap pipe; J wind region §1.4 J4 (`buildingPipe.cpp:85` REG_windPos, `:297` setWindParams, `:308` c21 upload); O PBR/N5/`[BldBind]` (wave-15) | high |
| render/vehiclePipe.cpp | 4330 | A+O | aap carpipe + our PBR layers (`e39a6f8` sept wave) | high |
| render/pipelinecommon.cpp | 2721 | A+J+O | aap 3-layer core; J `makeVSfromFile` REMOVED (dead sweep, `pipelinecommon.cpp:732`); O contracts: `pipeDeviceStateSet` `:1198/:1234`, `StateBlockEnforceCanonical` `:1547/:1571`, `pipeRepairSceneStartSync` `:1609/:1647` | high |
| render/postfx.cpp | 5382 | A+J+Z+O | aap postfx; J VCS filter/trails (§1.4 J6); Z SMAA seed moved out to postfx_smaa; O SMAA guards/H5/quad brackets | high |
| render/postfx_{colourfilter,luminance,motionblur,newfx,smaa,ssao,underwater,utils}.cpp | — | Z(smaa)+O(rest) | `postfx_smaa.cpp` header "SMAA passes + static resources (m0060 split)" ← Zeneric `SMAA.cpp` ← iryoku SMAA (`SMAA_reference/LICENSE.txt`); others ours | high |
| render/neopipe.cpp | 2520 | A+J+O | header: "Merged 2026-10-27 from THREE former TUs" (neo/neoCarpipe/neoWaterdrops, bodies verbatim); aap neo core + our SEH spans (b7396c4 round 2) | high |
| render/envmap.cpp | 1259 | A+O | aap env reflections + our swap-window SEH rewrite (b7396c4 round 3) | high |
| render/texalpha.cpp/.h | ~1430 | O | header "per-TEXTURE alpha classification for the PBR pipes"; gate `texalpha.cpp:1405-1421` (`texAlphaClassify`) | high |
| render/renderstate_monitor.cpp | — | O | header "three-layer render-state divergence monitor"; `[zfix2]` comparator (master-plan D4) | high |
| render/depthhook.cpp/.h | — | O | INTZ depth capture; `[depthdim]` assert (wave-16) | high |
| render/forwardplus.cpp | 1978 | O | our forward+ lighting; routed writer through `pipeDeviceStateSet` (wave-15) | high |
| render/grasssys.cpp/.h | 395 | Z seed + O | header: "Merged 2026-10-27 from TWO former TUs: PC_GrassRenderer.cpp + PC_GrassInstancer.cpp" (both Zeneric); O stage-0 bind contract, latch fixes (wave-15) | high |
| render/treeWind.cpp/.h | 521 | J+O | junior wind-shader system (§1.4 J4) grown into our module | high |
| render/noise.cpp/.h | — | O | header "Universal noise service"; replaces junior's ad-hoc stochastic noise + `utilitynoise.cpp` | high |
| render/particles_gpu.cpp/.h | — | O | GPU particles module (84ef232) | high |
| render/mirrorPass.cpp/.h | — | O | `MirrorRestoreState3L` (relay-unify P4) | high |
| render/profiler.cpp/.h | — | O | PF_SCOPE instrumentation | high |
| render/waterPipe.cpp/.h | — | A+J+O | aap water + junior temp-buffer interaction (§1.4 J3) + our M4-M9 (wave-15) | high |
| src/texdefs.cpp/.h | 1587 | J+O | header cites "src/extras/texdb.cpp texdb.txt metadata (TexInfo)" `texdefs.cpp:4`; J texdb layer `:121-129` + EDED slot `:49`, `texdefs.h:76`; O lookup receipts + wave-16 cross-dict recovery | high |
| peds/peds_pipe.cpp/.h, peds/skin.cpp/.h | — | O | our ped contracts (`SkinEntryContract` etc., `skin.h:19-36` absorption table); sits ON TOP of junior's ped-zwrite behaviour (§1.4 J7) | high |
| entities/chars.cpp/.h | — | O | head-part diagnostics `[PedHeadBind]`/`[PedHeadScan]` (`chars.cpp:79,:120,:180`; wave-16) | high |
| entities/softbody.cpp/.h, entities/sftbdyphsx.cpp/.h | — | O | softbody + ragdoll manager; `RagdollPedIsLive` `sftbdyphsx.cpp:461/:513` (wave-16) | high |
| extras/ragdoll_death.cpp/.h | — | O | Dismember; duplicate `RagdollPedIsLive` `ragdoll_death.cpp:1369`, gate `:1459` | high |
| extras/vehicles.cpp, extras/wheels.cpp/.h, extras/veh_shaders.cpp | — | O | no upstream analogue | high |
| extras/water_swell.h | 111 | O | canonical M9 swell law (wave-16 D9) | high |
| extras/brdfLibrary.h | — | O | academic BRDFs (CREDITS.md:43-48) | high |
| rw/gta.cpp/.h, rw/pipeplg.cpp, rw/Pools.h, rw/MemoryMgr.h, rw/LinkList.h | — | A | aap RW layer; gta.h carries our static_assert pins (wave-15 "addresses") | high |
| rw/normmap_stubs.cpp | — | Z | Zeneric-added file, survives verbatim-role | high |
| rw/ColData.h, rw/rphanim.h, rw/rtanim.h, rw/rtquat.h, rw/rtslerp.h | — | A/V | RW header lineage | med |
| render/AreaTex.h, render/SearchTex.h | — | Z (← iryoku SMAA data) | Zeneric-added; BSD via `SMAA_reference/LICENSE.txt` | high |
| entities/PlantSurfPropMgr.h | — | O/Z-med | plant surf props; no aap/junior analogue | med |
| core/main_exports.h, skygfx.h | — | A+J+Z+O | config struct carries junior keys (`zwriteThreshold*`, `forceWindShader`, `stochasticTexturing`, `dualPass*`) + ours (`rwFixesEnable` `skygfx.h:1016`, levers) | high |
| third_party/bullet, external/imgui, external/injector, external/d3d9, external/ini_parser.hpp | — | V | out of scope (`CREDITS.md:34-41`, `DEPENDENCIES.md`) | high |

### 1.4 Junior-derived REGIONS still live in our tree (the rewrite backlog)

| # | Region | Live anchors (verified 2026-11-09) | Junior feature (MixMods changelog) | Parity must keep |
|---|---|---|---|---|
| J1 | per-texture alpha-test ref: `zwriteThreshold`/`zwriteThresholdGrass`/`zwriteThresholdPed` applied inside `D3D9RenderDual` | `main.cpp:252` (D3D9RenderDual), `main.cpp:278-290` (ref select + clamp 254 + `setAlphaTestTwoLayer` GREATEREQUAL/LESS); parse sites per master-plan C13 (`readint2` "Core"/"SkyGfx" zwriteThreshold trio, save→[SkyGfx] asymmetry) | v1.4 "dualPass/zwriteThreshold per texture", v1.6 zwriteThresholdGrass, v1.7 zwriteThresholdPed | transparency ordering fixes (grass behind/in-front bug, power-line wires, parachute strings) |
| J2 | roadsign transparency hooks | `main.cpp:2757-2778` `CreateRoadsignTexture_RwTextureSetName(_orig)` EAXJMP 0x7F38A0, `CreateRoadsignAtomicA_RpAtomicSetGeometry(_orig)` EAXJMP 0x749D40 | v1.4 "Correction of transparency now does not cause errors in the texts of road signs" | roadsign text must stay readable; hooks must stay byte-verified (C20 class) |
| J3 | water temp-buffer re-render hook | `main.cpp:138` `TempBufferRenderIndexList` (0xC4B958), `main.cpp:4954-4988` `CWaterLevel__RenderAndEmptyRenderBuffer(_hook)`, DrawIndexedPrimitiveUP at `:5081`, second call `:5138-5139`, installs `main.cpp:7431-7434` (0x6E8790/0x6E8EF1/0x6E91E4/0x6E9963); interacts `render/waterPipe.cpp` | junior water hook + forced `VERTEXALPHAENABLE` (aap→junior diff) | water must still render when our M-pipes are off; no double-draw |
| J4 | wind shader system + EDED plugin slot | `buildingPipe.cpp:85` REG_windPos=21/REG_windIntensity=22, `:297` `setWindParams`, `:308` c21/c22 upload, `:766` wind flag; `render/treeWind.cpp/.h`; INI `forceWindShader` (main/debugmenu_ui/texdefs); EDED slot: `texdefs.cpp:49` "moved from src/rw/extendedplg.cpp", `texdefs.h:76` "per-atomic defined vertex shader (EDED plugin slot)", `main.cpp:4524` comment | v1.5 wind shader (vertex-alpha stiffness mask, .dff `0xEDED` section, `forceWindShader`) | Wind-Project-adapted .dff models must still sway; non-adapted models must NOT go transparent (junior's own warning) |
| J5 | texdb.txt metadata layer (TexInfo map: stochastic/dualPass/zwriteThreshold/detail per texture) | `texdefs.cpp:4` provenance line, `:121-129` (`static std::map<…TexInfo*> texdb`, `texdbOffset`) | v1.4-v1.7 texdb.txt corrections; `stochasticTexturing=1` | per-texture overrides must survive as DATA in our format, not junior's parser |
| J6 | VCS colour filter + VCS trails | `COLORFILTER_VCS` in `skygfx.h`, `core/main.cpp`, `core/presets.cpp`, `render/postfx.cpp`; `vcTrails` in `render/pipelinecommon.cpp` + `render/postfx.cpp` | "trails do GTA VCS", extended colour filters | VCS look + trails remain selectable (presets reference them) |
| J7 | ped/skin zbuffer write (parachute-string fix) | consumed via `peds/skin.cpp`+`peds/skin.h`+`peds/peds_pipe.h` (zwriteThreshold refs) under OUR contracts `SkinEntryContract/SkinExitContract` (`skin.h:19-36`) | v1.7 "pessoas/skins são escritas no zbuffer … corrige a corda do para-quedas" | peds/skins keep writing depth; parachute strings not see-through |
| J8 | stochastic texturing plumbing | `stochastic` markers in `texdefs.cpp`, `treeWind.cpp`, `render/noise.cpp`, `core/main.cpp`, `core/debugmenu_ui.cpp` (flags/noise service now ours; shader binding still junior-shaped) | "Procedural Stochastic Texturing" | repeat-pattern fix on texdb-listed textures |

Dead junior code already swept: `makeVSfromFile` (`pipelinecommon.cpp:732` CLASS-6 removal),
`simpleStochasticPS` global (no hits), `extendedplg.cpp` TU (absorbed, comment-only refs remain).

---

## §2 Per-subsystem rewrite table

Design idiom for every replacement: route device writes through pipelinecommon's contracts
(`pipeDeviceStateSet` `pipelinecommon.cpp:1234`, `pipeEnter/ExitAlphaMode`, `StateBlockEnforceCanonical`
`:1571`, `pipeRepairSceneStartSync` `:1647`), classify textures via texalpha, log via the dbglog
receipt idiom (`debug_handl`), never raw L3-only writes (the H1 class, master-plan §1.2 H1).

| # | Junior region | Replacement design in OUR systems | Parity checklist (SkyGfxExtended behaviour preserved) | Tier | Risk notes |
|---|---|---|---|---|---|
| R1 | J5+J1 data half: texdb.txt parser + TexInfo map | Replace junior's texdb.txt parser with a generated/INI-backed table inside texdefs (`TexDefsGetInfo`), keeping keys (stochastic/dualPass/zwriteThreshold/detail) as OUR schema; feed J1 ref selection from it | all v1.4-v1.7 per-texture fixes listed in changelog reproduce from the shipped data table; `[texdefs] miss`/`dictfallback` receipts unchanged | small | data migration: ship converted table; miss-path already fail-visible |
| R2 | J1 ref application inside `D3D9RenderDual` | Move alpha-test ref pinning out of the dual-render callback into `pipeEnterAlphaMode`-style bracket (enter pins ref via 3-layer `setAlphaTestTwoLayer`, exit restores via StateBlock); D3D9RenderDual keeps only mesh-gate logic | GREATEREQUAL/LESS pair semantics + 254 clamp + parachute/grass ordering | small | hottest draw path; gate on `[zfix2]`/`rs_mon` silence + stable-config re-shoot |
| R3 | J2 roadsign hooks | Re-implement as byte-verified entry wrappers in rw_fixes.cpp idiom (install-time prologue verify + MISMATCH→SKIP + throttled `[shield_integrity]`, closing C20) instead of naked EAXJMP trampolines in main.cpp | roadsign texture rename + atomic geometry fix behaviour identical | micro | hook-byte sites 0x7F38A0/0x749D40 must stay SilentPatch-free |
| R4 | J4 wind + EDED | treeWind becomes sole owner: EDED slot read stays as compat IMPORT in texdefs, but shader selection/constant upload (c21/c22/c23) moves to treeWind via pipelinecommon constant contract; `forceWindShader` becomes a debug lever with receipt; non-adapted-model guard (vertex-alpha transparency) becomes an explicit texalpha-aware policy | Wind-Project .dff sway; no transparency on non-adapted map objects; ENB-style force mode for testing | multi | buildingPipe.cpp is a contended file (F1 lane, master-plan §1.5) — serialize |
| R5 | J3 water hook | Delete `CWaterLevel__RenderAndEmptyRenderBuffer` temp-buffer re-render (main.cpp:4954-5139 + installs :7431-7434); waterPipe M-series + `WaterSwell::Height` (`water_swell.h`) become single water renderer; keep a legacy FFP water fallback lever for pipe-off configs | vanilla water still renders with all our toggles off; no double-draw; shore foam/caustics unchanged | multi | main.cpp single-writer wave; highest visual blast radius — gate on water re-shoot + `[water_rt]` receipts |
| R6 | J6 VCS filter/trails | Fold into postfx_colourfilter.cpp as one declarative filter table entry (ours already owns the other filters); trails = velocity-buffer consumer, not a separate PS fork in postfx.cpp | VCS look + trails selectable via presets/INI | small | preset round-trip (C1/C10 cluster) must keep the VCS names |
| R7 | J7 ped-zwrite under our contracts | Relay-unify P3 (`docs/plans/2026-11-09-relay-unify-spec.md:116`): single ped state machine in skin.cpp on StateBlock; delete main.cpp legacy inline arm only AFTER `pedsPipeEnable=0` retirement (spec FLAG 3) | depth-write for peds/skins (parachute), sticky-Z rules verbatim | multi | live 0/1 fork; sequence after lever default-flip |
| R8 | J8 stochastic binding | noise.cpp tile service already ours; rebind stochastic PS samplers through vehiclePipe/buildingPipe bind contracts (`bldStage0DevContract` shape) and drop junior-shaped global shader pointers | repeat-pattern fix on listed textures | small | shader CSO regen in same gate |

Wave sizing key: micro ≤1 h 1 file · small ≤½ d ≤2 files · multi 1-2 d 2-4 files · heavy ≥3 d or main.cpp surgery.

## §3 Wave order (file-disjoint, one system per wave, build gate each)

Rule set (master-plan §0.3 + relay-unify §file-disjoint): main.cpp = single writer, at most one ★
leaf per wave; never schedule buildingPipe.cpp/pipelinecommon.cpp while sibling lanes hold them
(master-plan §1.5 F1/serialization note); every wave ends with ONE orchestrator build
(`python tools/fast_build.py`) + binary-token verify + user retest.

| Wave | Content | Files (disjoint) | Tier | Gate |
|---|---|---|---|---|
| RW0 | R3 roadsign wrappers (rw_fixes idiom) | rw_fixes.cpp (+ main.cpp★ hook deletion) | micro | tokens; roadsign shot |
| RW1 | R1 texdb data migration | texdefs.cpp/.h (+ data table) | small | `[texdefs] dictfallback` ≥31/32 again; slab/marker family unchanged-or-better |
| RW2 | R2 alpha-ref bracket | main.cpp★ (D3D9RenderDual region only) + pipelinecommon (additive StateBlock use) | small | `[zfix2]`/`rs_mon` silence; grass/parachute re-shoot |
| RW3 | R6 VCS fold | postfx.cpp + postfx_colourfilter.cpp + presets.cpp | small | preset round-trip; VCS shot |
| RW4 | R4 wind/EDED | treeWind.cpp/.h + buildingPipe.cpp + texdefs.h (slot read only) | multi | wind sway shot; non-adapted opacity shot |
| RW5 | R7 ped machine (relay P3) | skin.cpp + peds_pipe.h (+ main.cpp★ legacy-arm delete, post-lever-flip) | multi | `[PedZW]`→0 at pedsPipeEnable 0 AND 1 |
| RW6 | R5 water hook retirement | main.cpp★ (4954-5139,7431-7434) + waterPipe.cpp | multi | water re-shoot; `[water_rt]` clean; hole-sheet gone-or-explained |
| RW7 | R8 stochastic rebind | buildingPipe.cpp + vehiclePipe.cpp + noise.h | small | stochastic textures shot |
| RW8+ | heavy extractions E2/E3 (corona window, pedContract) per master-plan W7/W8 | main.cpp★ + new TUs (vcxproj join! wave-16 lesson) | heavy | token parity |

RW1→RW2 order is deliberate: data table first so the ref bracket reads OUR schema from day one.
RW5 before RW6 because the ped machine is the smaller blast radius and de-risks the main.cpp★
slot for the water surgery.

## §4 THE BUG HUNT — ranked suspects for the corruption family

Symptom family (2026-11-09 W0.2 retest, shot `gta_sa_bGkwqtjQlA.jpg`, overlay F 117414 C 0x1CAA6,
session 14:17:00-14:18:03, gate binary 2,608,640 B md5 80ebc9a9…): CJ's house renders as a HOLE
(black void where the doorway/interior should be, mid-ground world missing), pedestrians render as
JUST LEGS (hard waist cut — red-trouser legs + heels-only ped, torsos/heads absent), elevated pale
blue water sheet across the mid-ground, yellow marker cone, CJ hovering. Older specimens: floating
bottles, severed vehicles, head-height slabs (W0/W0.1, `docs/plans/2026-11-09-visual-triage.md` §1-2),
green head blob (= foreign geometry, wave-16 chars verdict).

September timeline (git): last-good shots 2026-09-23; last commit before the window `95cf544`
(2026-09-22, SMAA guard, 2 files); first commit after = `b7396c4` (2026-10-02) "swarm rounds 1-3",
53 files, +12,775/−1,292 — its own message says the rounds were *chasing* occluder/colour-filter/
SMAA-ghosting bugs "reported in testing", i.e. the family was already live in the Sept-23→Oct-2
working tree and the commit froze 16 lanes of simultaneous state-surgery (3-layer save/restore
mirrors in buildingPipe/vehiclePipe/neoWaterdrops/neoCarpipe, Xbox CB TEXTUREADDRESS/sampler/
vertex-alpha bundles) on top of it. Onset window = that working-tree interval; b7396c4 content =
first frozen suspect set.

### Ranked suspects (junior-derived first, then cross-cutting)

**S1 (junior, J3) — water temp-buffer hook double-render vs our water sheet.**
`CWaterLevel__RenderAndEmptyRenderBuffer_hook` (`main.cpp:4954-4988`) re-draws the game's water temp
buffer with forced vertex-alpha and its own z states at four intercepted call sites
(`main.cpp:7431-7434`), while waterPipe M-series + `WaterSwell` also render water. A stale/wrong
height matrix or state leak from the junior hook reproduces exactly the elevated pale-blue sheet
that visually "eats" the house band in the W0.2 shot. Discriminators: water INI toggles
(waterBodyClassify/waterRefractionValid/…) one-at-a-time; `[water_rt]` receipts; temporarily
no-op the hook's DrawIndexedPrimitiveUP (`main.cpp:5081`) in a bisect build.

**S2 (junior, J1/J5) — alpha-test reference corruption.** The ref applied at `main.cpp:278-290`
comes from config OR per-texture TexInfo; master-plan C13 records the parse/persist asymmetry
(read `[Core]`, save `[SkyGfx]`) so a save→reload silently changes the value the engine runs with.
A wrong-high GREATEREQUAL ref discards every alpha-carrying facade/foliage pixel → world reads as
HOLES; the same machinery explains olive/ghost bands historically. Discriminators: `[zfix2]`
atest fields, `[PedOpaque] … atest=1` lines (present in W0.2 log 14:18:01), INI round-trip test,
R1/R2 rewrite removes the class.

**S3 (ours, wave-16 residual — do NOT mislabel as junior) — ragdoll/dismember bone-matrix writes on
churned slots.** Root found+fixed in wave-16 (`Ragdoll_c::Update` wrote matArray through freed
ped+0x18 → reissued RW pool memory = the floating-props/head-slab class, and the crash-forensics
stale-vptr mechanism), BUT wave-16 records the residual: "Ragdoll_ApplyPose write-back still
attempted for churned clumps (bounds-checked, not liveness-gated)". `Dismember_ApplyZeroScale`
sits behind `RagdollPedIsLive` (`ragdoll_death.cpp:1369`, gate `:1459`; twin `sftbdyphsx.cpp:461/:513`)
— a slot-reused ped pointer that passes liveness wrongly gets torso bones zero-scaled: **torsos
vanish, legs remain = the W0.2 "just legs" hard-cut look**. Discriminators: `[ragdoll] boneidx oob`
(0 in W0.2 — silent), `[PedHeadBind]` (fired once, `atomic=18285400 tex='head' raster=181285A8
stage0raster=18127E18` — rasters DIFFER = wrong-bind leg), liveness-gate the ApplyPose write-back
and re-shoot the Grove/crowd spots.

**S4 (cross-cutting, H1 residual) — depth/blend divergence still live in W0.2.** `[frame_repair]`
×8 ("ALPHABLEND re-pinned OFF … rw=1 drv=1 dev=1"), terminal `[zfix2] CONTRACT site=depthct_in
pipe=-1 rw=10 drv=10 dev=50 | ZWRITE(1)… | ZFUNC(4) … dev=8` (device ZFUNC=ALWAYS inside the
effects phase) and `[zfix] pipe entry depth drift repaired` at 14:18:02 prove the master fault
survives the W0.2 gate. Depth-ALWAYS windows let near geometry (water sheet, slabs) paint over the
house band and reorder ped/world compositing. Discriminators: `rs_mon`/`[zfix2]` site census
(master-plan W1-L2 attribution receipt), relay-unify P2.

**S5 (junior-adjacent data, J5) — texdefs recovery under-delivery.** Wave-16 predicted 31/32
`dictfallback` recoveries; W0.2 logged **2** (`gras07Si`, `sm_des_bush1`) against 34 `[texdefs] miss`
— the remaining misses fall back to untextured/marker quads (slab/cone family; fallback-marker
comment `texdefs.cpp:259-261`, receipt string `:312-313`, verified live; visual-triage's :292-294
has drifted). Not the hole/legs mechanism but keeps the frame noisy and confounds triage.

### §4b W0.2-era suspects that must NOT be misattributed to junior code

The retest build was W0.2, so hole+legs may be a FRESH regression of our own new machinery:
1. **`RagdollPedIsLive` false-negatives/positives** (S3 gate itself): CPool byteMap is INVERTED
   (0x80 SET = FREE — wave-16 texdefs note); a wrong stride/polarity read either skips a needed
   repair or green-lights a dismember write onto a recycled live ped (S3).
2. **`StateBlockEnforceCanonical` frame-end pin** (`pipelinecommon.cpp:1547-1571`, new in W0.2):
   pins L3 blend OFF at frame end; any path that legitimately needs device blend at next scene
   start now depends on `pipeRepairSceneStartSync` catching it — the 8 `[frame_repair]` hits are
   this assert DOING ITS JOB, but a mid-frame foreign writer (S1/S4) still wins inside the frame.
3. **texalpha classification now ACTIVE** (`texAlphaClassify=1`, gate `texalpha.cpp:1405-1421`):
   a world texture misclassified CUTOUT/FADED gets alpha-tested → facade holes. Ped textures were
   audited OPAQUE (visual-triage §0); the WORLD set has not had the same audit — do it before
   blaming junior.
4. **The churn gates themselves** changed which writes happen at all (wave-16): pre-W0.2 screenshots
   and post-W0.2 screenshots are not the same experiment.
Discriminating evidence to collect next retest: `[PedRS]` (15 in W0.2), `[PedZW]` (4), `[PedHeadBind]`
raster-vs-stage0 equality, `[ragdoll] boneidx oob`, `[rwfix] install`/`install SKIPPED` lines,
`[frame_repair]`/`[zfix2]` site census, `[texdefs] dictfallback` count vs 31/32, plus the
`2026-11` ShareX folder shot set. Both hypotheses (long-standing Sept bug vs W0.2 regression)
stay ALIVE until a lever-off retest separates them.

## §5 Attribution aid for retests (kill-switches + receipts)

- `rwFixesEnable=0` (`skygfx.h:1016`, gate `rw_fixes.cpp:1791-1804`, receipt
  `[rwfix] install SKIPPED (rwFixesEnable=0)`) — landed UNBUILT. Bisects "a FIXER broke it" vs
  "a FEATURE broke it" in one INI flip: with 0, none of the corona/flush/font/shield machines install.
- Per-system receipts for the second bisection axis (junior-legacy vs ours): J1/J2 `[zfix2]` atest
  fields + roadsign shields; J3 `[water_rt]` + water toggles; J4 wind = treeWind lever; J5/R1
  `[texdefs] miss`/`dictfallback`; J6 presets; J7 `[PedZW]`/`[PedOpaque]`/`[PedRS]` + `pedsPipeEnable`
  0/1 fork; ours: `[frame_repair]`, `[scene_repair]`, `[ragdoll]`, `[PedHeadBind]`, `rs_mon`.
- Procedure: retest matrix = {rwFixesEnable 0/1} × {suspect lever off/on} × {same 3 Grove/interior
  shots}; every cell logs the SESSION-START banner (`built=` stamp, wave-16 fortification) so the
  binary under test is always identifiable (crash-forensics E14 showed a deployed ASI can predate
  the tree — never triage a shot without matching `built=` to a gate md5 from wave-15/16 tables).

## §6 Verification record

- 24 live anchors re-grepped this session (all matched): main.cpp 138/252/278-290/820/1754/2235/
  2757-2778/3713/4524/4954-4988/5081/5138-5139/7431-7434; buildingPipe.cpp 85/297/308/766;
  pipelinecommon.cpp 732/1198/1234/1547/1571/1609/1647; sftbdyphsx.cpp 461/513; ragdoll_death.cpp
  1366/1369/1459; chars.cpp 79/120/180; texalpha.cpp 1405-1421; texdefs.cpp 4/49/121/127/129;
  texdefs.h 76; rw_fixes.cpp 12/1791/1801/1804; skygfx.h 1016. TU headers read live: neopipe.cpp,
  grasssys.cpp, noise.cpp, postfx_smaa.cpp, rw_fixes.cpp, shadows.cpp, texalpha.cpp,
  renderstate_monitor.cpp.
- All cited docs exist on disk: master-plan, relay-unify-spec, visual-triage, crash-forensics,
  n-registry-rootcause, CREDITS.md, wave-15/16 records, build-gate-checklist.
- No §-refs in this doc point outside §1-§6 or the cited docs; no source edits, builds or commits
  were made by this lane.

---

## §7 W0.3 wave status (addendum, 2026-11-09 — landing, build pending)

This addendum records the W0.3 wave without rewriting §1-§6. Where it conflicts with a row above,
this section wins.

### 7.1 Noise based variation (replaces stochastic texturing)

The project-wide rename is landing: **"stochastic texturing" → "noise based variation"**, driven by
`src/render/noise.{cpp,h}` (new `NoiseMaterialVariation` API) over the packed RGBA utility tile
(`shaders/include/Noise.hlsl`). The building shaders are rewritten and `buildingPipe.cpp` plumbing is
rewired: `buildingPipe_bindNoiseFor` binds the s9 packed tile + c49-c52 knob block before the
noise-detail PS bind. Compat shims keep the old spellings parsing only (`stochasticTexturing` /
`stochastic` INI aliases in `main.cpp`, `stochastic=` texdb alias in `texdefs.cpp`) — they are not
feature names.

Rows affected: **J8** (stochastic plumbing), **R1** (texdb data half) and **R8** (stochastic rebind)
land under the new name; **RW1/RW7** in §3 are re-read as the noise-based-variation rows. Re-run
§2 R1/R8 and §3 RW1/RW7 with the new vocabulary; no behaviour change is intended beyond the naming +
`NoiseMaterialVariation` routing.

### 7.2 Motion FX family (wind restructure)

Wind animation leaves the solo tree-wind feature and becomes part of a motion-FX family, so motion,
randomness and physics share one owner instead of solo features stacked on top:

| TU | Role |
|---|---|
| `src/render/motion_fx.cpp` / `motion_fx.h` (new) | PARENT — motion orchestration, wind animation, randomness/physics-family integration |
| `src/render/wind.cpp` (new) | CHILD — wind field / gust math |
| `src/render/treeWind.cpp` | slims to the `TreeAlpha` path only |

Both new TUs join `build/skygfx.vcxproj`. This supersedes **§1.4 J4 / §2 R4**'s "treeWind becomes
sole owner" wording: treeWind keeps the EDED/constant-compat reads, but wind animation now lives in
`motion_fx` → `wind`. **RW4** in §3 is re-read as the motion-FX landing row.

Family seams (as landed in `motion_fx.h`): one motion clock `MotionFxClockSeconds()` (pause-safe
CTimer `ms_fTimeStep` accumulator — not a QPC wall clock); randomness enters through
`NoiseMaterialVariation` (`noise.h`, added by the noise lane in the same gate build); physics coupling
is strictly read-only (softbody/ragdoll/`water_swell` state is never consumed or written by wind).

### 7.3 Bug-hunt fixes landing in main.cpp this wave

| Suspect (§4) | Status in W0.3 |
|---|---|
| **S1** (junior J3) water temp-buffer hook double-render | being fixed in `main.cpp` this wave — the junior `CWaterLevel__RenderAndEmptyRenderBuffer` re-render path is removed so waterPipe M-series + `WaterSwell::Height` remain the single water renderer |
| **S2** (junior J1/J5) alpha-test INI asymmetry (read `[Core]`, save `[SkyGfx]`) | being fixed in `main.cpp` this wave — parse/persist symmetry restored |
| **S3** (ours, wave-16 residual) recycled-ped bone-matrix writes | being liveness-gated this wave — `Ragdoll_ApplyPose` write-back / `Dismember_ApplyZeroScale` gated on `RagdollPedIsLive` |

`main.cpp` is **7,805 lines** after the `rw_fixes.cpp` extraction (`rwFixesEnable` kill-switch;
receipts `[rwfix] install OK (6 machines)` / `install SKIPPED`). The S1/S2 fixes stay inside the
single-writer `main.cpp` slot (§3 rule set).

### 7.4 Open runtime finding — texdefs recovery under-delivery

S5 measured worse than predicted: the W0.2 log's `[texdefs] dictfallback` recovered only **2/32**
texture names (`gras07Si`, `sm_des_bush1`) against the predicted **31/32** (against 34 `[texdefs]
miss`). It is still not the hole/legs mechanism, but it keeps the frame noisy and confounds triage.
Needs a dedicated measurement wave before RW1 is called green: log the miss list + fallback hits,
compare against the 32-name table, then close S5.
