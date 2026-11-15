# Visual triage — W0.1 build (2026-11-09 04:57–04:59)

Read-only triage of the user's W0.1 retest screenshots. Source build:
`skygfx.asi` 2,602,496 B, md5 `58b2190be48c7e5de298bc7e3ccc5c0c`
(`2026-11-07-build-gate-checklist.md:84-91`). All shots are from the
04:57–04:59 session and carry the F9 correlation overlay (top-left
`F <frameId> | C 0x<code>`, `src/core/debugmenu_ui.cpp:4680-4682`,
`:4762`), so the on-screen pair maps to the session log
`E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (stamps every line as
`[#<frameId>:<code>]`). `nfs_*.jpg` in the same folder is a different game
and was ignored.

Severity: CRIT = destroys the frame, HIGH = obvious, MED = noticeable,
LOW = cosmetic.

## 0. Session facts pulled from the runtime log

These are the receipts the W0.1 gate said to watch
(`2026-11-07-build-gate-checklist.md:90-91`), measured over the whole
session (04:57:50–04:59:16):

| receipt | W0.1 count | meaning / status |
|---|---|---|
| `[SpriteDepthFix]` | **29** (expected ~0; W0 = 50) | `SpriteFlush_70CF20` + `CloudRegionFlush_7273D0` still enter with `dev zen=0 zfunc=8` and are force-corrected — the leaker is upstream of the fixes, only the symptom is caught |
| `[PedZW]` | **13** (expected ~0; W0 = 7) | every hit is `depth drift on entry … dev zfunc=8` → forced `ZTEST=1 ZFUNC=LESSEQUAL ZWRITE=1`; victim text: "CJ overdrawn by props/buildings" |
| `[scene_repair]` | **26** | `scene_start: repaired ALPHABLEND ZWRITE ZTEST ZFUNC -> rw-cache baseline (blend rw=0 dev=1 … zwrite rw=1 dev=0 ztest rw=1 dev=0 zfunc drv=4 dev=8)` — H1 divergence is still live EVERY scene |
| `[FxAlpha]` | 46 total; `PostFx.repaired` **26**; `Coronas.SunReflection` layer drift **9**; `Coronas.Reflections` drift 4 (+ `camRas=2048x1024 != screen 1920x1080` 4) | alpha-layer drift is not gone; same 26-repair cadence as scene_repair |
| `[texdefs] miss` | `total=191 distinct=32` (incl. `greyground256128`, `sf_pave6`, `ws_trans_concr`/`LODtnsfn`, `stormdrain4_nt`/`lod_countn2`, `blue3`/`generic`) | failed texture lookups fall back to "untextured-white sprite or the red fallback marker" (`src/texdefs.cpp:292-294`) |
| `[CoronaDraw]` | 23 (`pre-force dev blend=1 src=5 dst=6`, `black-quad=blend 0`) | the corona/sprite-flush path is the named black-quad producer (`src/core/main.cpp:2966`) |
| `[BldZW]` | **78** | PBR blended meshes park ZWRITE (`tex='sjmhoodlawn41' matA=150`, `mp_diner_woodwall` 230, `antenna1`, `lasdkcrtgr1s`) |
| `[PedHeadPath]` / `[PedHead]` | **1 / 1** | ped head-part path fires, only once |
| `[PedOpaque]` | repaired divergent entry (present) | ped opaque contract still has to repair |
| `[BldBind]` / `bldBindDiffuse3L` | **0 / 0** | the W0 "bind/texture leak" receipt did NOT fire this run |
| `[VehElide]` | 21 (`elided=647308 writes` last window) | vehicle bind-elision alive |
| `[PostFX] UpdateFrontBuffer` | every frame `pRasFB=2048x2048 camRas=1920x1080` | H5 dims mismatch still live |
| levers | `coronaBlendShield=1 spriteFlushShield=1 spriteStateShield=1 texAlphaClassify=1 pedOpaqueContract=1 debugOverlay=1` | `skygfx.ini:37,39,142,176,177,209` — the safety levers were ON, so nothing below is a "lever off" artifact |
| ped texalpha | `torso/head/legs/feet` all **OPAQUE**, 0 cutout, 0 faded | the green head mass is NOT an alpha-class misread of ped textures |

Session end: `CRASH[1] READ at 0x42480000` (0xC0000005) at 04:59:16,
immediately after `[SMAA] stage-0 restore`, dumps
`skygfx_crash_2026-11-09_045917.dmp` + `EarshotCrash_2026-11-09_045917.dmp`
(`skygfx_dbg.log:5415-5449`). **W0.1 was not a clean retest — it crashed.**
Device: 1920x1080 A2R10G10B10 / D24S8, ~114 fps (`gta_sa_d3d9.log`).

## 1. W0.1 per-shot classification (9 shots)

Overlay numbers are quoted as rendered on the frame; a 14 px overlay is
partially OCR-ambiguous, and the code documents the pair as the same
frame id (`debugmenu_ui.cpp:4680-4682`).

| # | file | clock | overlay F / C | scene | visible artifacts (family — severity) |
|---|---|---|---|---|---|
| 1 | `gta_sa_hI8m7BKhQZ.jpg` | 10:20 | 61064 / 0x8AEC | Ganton street/court, over CJ | HUGE hard-edged dark-grey slabs across the mid/lower frame, one covering CJ's head, plus a large dark disc; long thin black bars across the upper-left/upper-right sky. Black-quad/decal-fallback family — **CRIT**; black sky bars — **HIGH**. Light haze only. No vehicles, no peds-over-vehicle. |
| 2 | `gta_sa_WBo3HLv9mG.jpg` | 10:38 | 64560 / 0xFCF39 | Ganton pavement, parked red/white convertible | A large flat dark-grey slab lies through the convertible's rear deck (car half swallowed). Black-quad family — **HIGH**. Pale/white translucent horizontal band across the mid-ground in front of the trees — milky-sheet family — **MED**. A few small objects floating near the tree line — frame-transform family — **LOW**. Pavement/car otherwise correctly textured, no severing. |
| 3 | `gta_sa_D8AEMKgMlp.jpg` | 10:15 | 27346 / 0xA082 | CJ house interior, carrying chair | GIANT flat-shaded YELLOW triangular cone in front of the door (~15–20% of frame height, hard edges) — marker family (C3dMarkers), **NEW variant** — **HIGH**. Green angular patch on the back of CJ's neck — head-blob family — **HIGH**. Interior walls/floor/rug/textures otherwise clean, no haze. |
| 4 | `gta_sa_EwTrlZY2Uc.jpg` | 10:22 | 46368 / 0x94A4 | Ganton street, same view as #1 | Massive dark-grey arrow/trapezoid slabs fill the lower half and hide CJ's body (only the white tank top shows between slabs). Black-quad family — **CRIT**. Long black diagonal sky bars remain — **HIGH**. Olive/yellow translucent band along the hedge/grass — olive-band family — **MED**. Haze moderate. |
| 5 | `gta_sa_a7syCdP5WN.jpg` | 10:15 | 24908 / 0x614C | interior close-up, CJ + armchair | ENORMOUS dark-green/black angular mass covering CJ's head and upper torso (~30% of frame) — head-blob family, worst specimen in the set — **CRIT**. Yellow marker cone at right — marker family — **HIGH**. Interior otherwise clean. |
| 6 | `gta_sa_NbpaYnjYXH.jpg` | 10:13 | 19494 / 0x4C26 | kitchen, CJ facing camera | Green triangular blob over CJ's face — head-blob family — **HIGH**. Yellow marker cone overlapping the doorway — marker family — **MED**. A bottle and a potted plant floating near the ceiling at top-centre/upper-left — frame-transform family (floating props) — **MED-HIGH**. Interior textures/render clean, no haze. |
| 7 | `gta_sa_82TuIUXBGj.jpg` | 10:58 | 128691 / 0x1F683 | Grove St, crowd of green/khaki peds | Broad pale milky translucent band across the mid-ground (over wall/pavement) — milky-sheet/haze family — **MED-HIGH**. Yellow-green grass strip behind the wall — olive-band family — **MED**. Brown-pants ped appears elevated/overlapping the ped in front — possible ped-over-ped/depth — **MED**. No slabs, no vehicles. |
| 8 | `gta_sa_yJeS21UEhz.jpg` | 10:55 | 117748 / 0x1CBF4 | same crowd, close-up | Heavy ped-ped merge: the group is interpenetrating, one torso clipping through another — ped depth/overdraw (or crowd collision) — **HIGH**. A translucent olive-yellow sheet crosses the peds' upper bodies and the wall — olive-band/alpha-sheet family — **HIGH**. A white bottle floats above the cluster — frame-transform family — **LOW-MED**. CJ's red car fender at bottom-right is intact with correct occlusion. |
| 9 | `gta_sa_gNOII9lSko.jpg` | 10:53 | 108275 / 0x1A6F3 | crowd wide, CJ foreground + convertible | Translucent olive/yellowish sheet crossing the mid-ground peds' torsos — alpha-sheet/olive family — **MED**. Peds otherwise readable; the red/white convertible renders intact and CJ occludes correctly (no draw-over-CJ here). Some white specks (lamps/props) near the wall — **LOW**. |

Cross-cutting: every interior shot (#3, #5, #6) has clean textures, correct
lighting and **no haze**; every outdoors shot has at least one translucent
sheet or wash. The W0 "whole-scene milky whiteout" is not present in any
W0.1 shot — outdoors is a wash/band now, not a whiteout.

## 2. W0 -> W0.1 family deltas (6 W0 shots vs 9 W0.1 shots)

| family | W0 (04:02–04:04) | W0.1 (04:57–04:59) | verdict |
|---|---|---|---|
| Vehicles over peds | 3 clear specimens: `sPPOvVhK5r` (cars at ped shoulder height), `Omo2nO1Wav` (car nose-down over a ped's head), `w2fWDjGoq4` (convertible at ped shoulder) | **no traffic-over-ped composite in the 9 shots** (autos are parked, peds are separated) | **UNPROVEN** — the direct W0.1 acceptance question was not exercised. `[PedZW]` 13x proves the leak is still live, so the W0.1 claim "`[PedZW]` 7x -> ~0" did not hold |
| Severed / floating vehicles | `sPPOvVhK5r`, `Omo2nO1Wav`, `w2fWDjGoq4`: wheels/roofs detached, cars hovering off the road | no moving-traffic specimen; every photographed car (`WBo3`, `gNOII`, `yJeS`) is intact and on the ground | **IMPROVED / not observed** — but the sample cannot prove the family is gone (family-C subsystem still initialised: Dismember/Softbody/Ragdoll receipts) |
| Black quads / decal slabs | giant flat charcoal slabs lying ON the pavement (`xCmWCSjOqG`, `JfTIzVW2cs`, `YYPdkmdyFT` arrows), black diagonal bars in the sky (`sPPOvVhK5r`, `Omo2nO1Wav`) | slabs are now **airborne at player height**, occluding CJ (`hI8m7`, `EwTrl`) and one lies through a parked car (`WBo3`); black sky bars persist (`EwTrl`, `hI8m7`) | **SAME family, WORSE** — bigger, in front of the camera, hiding the player |
| Red square | `YYPdkmdyFT`: flat bright-red square floating mid-air | no red square; instead a giant **yellow** marker cone in the house (`D8AEM`, `a7sy`, `Nbpa`) | **SAME root, MUTATED** — the red fallback marker path is documented in `texdefs.cpp`; the yellow cone is the C3dMarkers geometry/scale defect showing through |
| CJ head green blob | known family in W0 | `a7syCdP5WN` giant green-black head/shoulder mass; `D8AEM` neck patch; `Nbpa` face patch | **SAME / WORSE** |
| Floating bottles in sky | W0: bottles floating in the sky | `Nbpa`: bottle + potted plant floating at the ceiling; `82Tu`/`yJeS`: bottle floating in the crowd | **SAME** — frame-transform family (family-C) is still alive |
| Olive/yellow translucent grass bands | `xCmWCSjOqG`, `YYPdkmdyFT`, `JfTIzVW2cs`: olive band across the mid-ground | `EwTrl` olive band at the hedge; `82Tu` yellow-green strip; `yJeS` olive sheet through the peds | **SAME** |
| Milky haze / white sheets | `YYPdkmdyFT`/`xCmWCSjOqG`/`JfTIzVW2cs`: whole-scene wash + pale sheets | interiors CLEAN (`D8AEM`, `a7sy`, `Nbpa`); outdoors `82Tu`/`yJeS`/`gNOII` pale band + wash, `WBo3` pale sheet | **IMPROVED indoors, SAME outdoors** — consistent with the log: 26x `scene_start` ALPHABLEND repair, untouched by W0.1 |
| Whole-scene milky whiteout | full whiteout on several W0 frames | no full whiteout; a wash only | **IMPROVED (mild)** |
| Giant yellow marker cone | — | `D8AEM`, `a7sy`, `Nbpa` | **NEW presentation** (marker family) |
| Ped-ped interpenetration in crowd | not a W0 specimen | `yJeS` heavy merge, `82Tu` elevated/overlapping ped | **NEW / needs a discriminator** (ped depth vs ordinary crowd collision) |

## 3. Sept-23 baseline ("what fine looked like")

- `gta_sa_Mtjbnldaze.jpg` (07:09): golden-hour street, **clean geometry** —
  no slabs, no black bars, no severed/floating vehicles, no haze. Already
  present: a dark-green blob under the red car and a strongly over-bright /
  blown sky.
- `gta_sa_IQ6sgtu9ol.jpg` (07:11): a **giant blue cone** in the sky (looks
  like the sun-reflection/corona class — the same lane as the 9x
  `[FxAlpha] Coronas.SunReflection` layer-drift receipt) and a **bicycle
  floating** next to the ped (family-C). Ground otherwise clean.
- `gta_sa_KVw7nw322X.jpg` (07:12): menu open, clean geometry, pale band on
  the ground.

So "fine" = no quad flood, no floating vehicles, clean pavement. The
families that predate W0 are: corona/cone artifact, floating props, car
green blob, over-bright bloom. The families that arrived between Sept-23
and W0 are the severed/floating vehicles, the black-slab flood, the red
square and the wash. W0.1 removed the *photographed* severed vehicles but
made the slabs worse and did not touch the wash or the head blob.

## 4. Verdict

**#1 visual defect: the airborne untextured dark slabs / black bars that
occlude the player** (`hI8m7`, `EwTrl`, plus the slab through the car in
`WBo3`). This is what makes every outdoors W0.1 frame read as "ass": the
world itself is buried under flat dark polygons and the player is cut in
half. It is a regression in *placement* versus W0 (on the ground -> in
front of the camera), not in kind.

**Mechanism best fit: texture-lookup fallback / bind-leak class, not ped
depth.**
- The loud, concrete producer this run is `texdefs`: 191 failed lookups,
  32 distinct world textures, including pavement/ground names
  (`greyground256128`, `sf_pave6`, `ws_trans_concr`). `texdefs.cpp:257-262`
  states that a failed lookup is the root of the "bright-red fallback
  marker" and the "milky-white untextured sprite quads (missing texture ->
  FFP white + vertex alpha)". The slabs read exactly like untextured
  full-size decal/ground quads (court circle, pavement arrows, tiled
  ground) rendered flat grey/dark, not like a shading bug.
- The black *bars* in the sky have a second named producer: the corona /
  sprite-flush path with blend disabled — `[CoronaDraw] pre-force … 
  black-quad=blend 0` (`main.cpp:2966`, 23x).
- The W0 hypothesis `[BldBind]`/`bldBindDiffuse3L` is **not supported**:
  zero receipts this session. Deprioritise it until a receipt fires.
- Family-C (frame-transform corruption) is still alive and is the better
  fit for the *floating props* (bottle + plant at the ceiling, bottle in
  the crowd) and possibly supplies the airborne placement of the slabs —
  but it does not explain the flat untextured shading.

Ped depth is **not** the #1 driver of the current screenshots: `[PedZW]`
fires 13x and the head blob persists, but the W0 "cars over peds" look is
not visible anywhere in this set. It is still unresolved, not fixed.

**Single fix that would most improve the screenshots:** fail-closed the
texture-miss draw path — when a lookup fails, do not draw the untextured
full-size fallback quad; skip the mesh (or bind a 1x1 magenta sentinel) and
throttle the receipt. That one behavioural change removes the slabs, the
pale/white sheets and the red fallback marker in a single stroke; then fix
the TXD parent resolution for the 32 named textures (the case/extension
recovery already exists in `TexDefsTryNameVariant`, `texdefs.cpp`).
Verify with the same three Grove St shots (`hI8m7`, `EwTrl`, `WBo3`).

Priority after that:
1. **#2 blend/alpha haze + olive sheets** — 26x `[scene_repair]`
   ALPHABLEND/ZFUNC repair proves the H1 divergence is untouched; a W1 H1
   fix, not a W0.1 rerun.
2. **#3 PedZW / head blob** — 13x entry drift, `PedHeadPath` 1x, ped
   textures all OPAQUE; the green mass is stray geometry/alpha part, not
   texture classification.
3. **W0.1 acceptance gaps**: the retest produced no traffic-over-ped shot
   (the stated question), and the session crashed at 04:59:16 — the gate
   needs a clean run and a moving-traffic capture before "vehicles over
   peds" can be called better or fixed.

## 5. Evidence index

- W0.1 scope + expected deltas: `docs/plans/2026-11-07-build-gate-checklist.md:84-91`
- Overlay contract ("same numbers stamped in the log"):
  `src/core/debugmenu_ui.cpp:4680-4682`, format `:4762`
- Texture-fallback root cause + receipt:
  `src/texdefs.cpp:257-262`, `:292-294`
- Black-quad producer note: `src/core/main.cpp:2966`
- `[PedZW]` entry-drift receipt + victim text: `src/core/main.cpp:1754`
- Runtime receipts (session 04:57:50–04:59:16), crash at `:5415-5449`,
  overlay init `:511`, CoronaDraw `:2646`, PedHeadPath `:2834`:
  `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`
- Levers ON: `E:\games\gtasa_skygfx_plus\skygfx.ini:37,39,142,176,177,209`
- Screenshots: `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-11\`
  (W0.1 set above; W0 set `sPPOvVhK5r`, `Omo2nO1Wav`, `w2fWDjGoq4`,
  `xCmWCSjOqG`, `JfTIzVW2cs`, `YYPdkmdyFT`); baseline
  `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-09\`
  (`Mtjbnldaze`, `IQ6sgtu9ol`, `KVw7nw322X`).
