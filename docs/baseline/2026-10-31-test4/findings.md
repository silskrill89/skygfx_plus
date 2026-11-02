# Test-4 findings — user INI (01:26) run, log 01:24:45→01:27+, frames #2→#284810

## INI state (user-edited, mtime 01:26)
- `pipeChainEnable = 0` (:142) — **KILLS THE WATER PIPE CORE** → `[water] chain` = 0 hits all run; every water draw = FFP fallback ("pipe unavailable" ×14)
- `waterStyle = Xbox` (:261), `pipeline = Xbox` (:145), `stateBaselineEnable = 1` (:195)
- Repairs DISABLED by user: coronaBlendShield=0 (:37), spriteFlushShield=0, spriteStateShield=0, depthHookEnable=0, ragdollEnable=0, dualPass*=0 (:50-55), pedAlphaTestEnable=0, shadowContractEnable=0
- **Verdict**: bisect (docs/baseline/2026-10-31-stripped-ini-bisect/findings.md) already proved the disabled features ARE the repairs — diseases returning is expected. Goal of next wave: make core renderer correct WITHOUT shields.

## Log receipts
- `[water] chain` = 0; `flatpath` = 14 (site1 s0=1035AF80 staleVS=0CF94508 stalePS=0CF94CD8; site2 s0=10355B10 staleVS=0); `flatroute` = 0 (route never succeeded — pipe gated); `floodplane` = 0 (M1 guard lives in pipe path — not running)
- `corona_blend` = 79: persistent **dev blend=1 | drv blend=0** L2 divergence (reverse of prior runs); many samples `rw raster=0 dev s0=0`; spriteBatch=0 (noon, pool legitimately sparse)
- `PedTex` = 1: frame #1722 `stage0 resync cached=00000000 dev=00000000` (mannequin moment; recovery fails when cache=NULL)
- `[BldTex]` = 24: lw_seat2 / CJ_CHROME2 blend=0 zw=1 (interior, benign)
- `FIRST_DIVERGE` = 29 (rs_mon) — unanalysed
- splashfoam pin firing; CoronaPool repair working; pAmbient non-black

## Shots (8, 01:0x–01:2x, game 11:56→12:15)
1. **Mirrored-world band across mid-screen** (CBChOoMWvF F283276, pr0FfL4pBB F282117, ku9Ggz68xk F281965): vertically-reflected world content drawn as a band over LAND, hard horizontal seams. NOT our reflRT (reflRT=none in Xbox, 0 log hits). Prime suspect: FFP water quads sampling a leaked screen-family raster (s0=1035AF80/10355B10 — identity unknown) OR scene-copy raster.
2. **Black road/ground, hard-edged giant blotches** (vnwB61NvXL F280654 worst, Al1TEazrsa F279305, +2): building-pipe ground stage-0 binds still single-layer cache-gated (vehBindDiffuse3L hardened vehicles only) → NULL/leaked raster on road.
3. **Grey-mannequin CJ exterior** (3 shots): PedTex stage0 NULL family; pedStage0Resync can't recover when rw cache=NULL.
4. **Black ped silhouettes** (2 shots).
5. **Dark-at-noon** (7OrRKjc2U8 F284810 12:15 dark vs 12:09-12:12 bright): lighting/exposure blackout family.
6. **Interiors mostly GOOD** except: bandana-over-face green mask (S5), black TV/doorway quads + black plant silhouette (F6/S6 family).
- GOOD: sky, palms, fences, houses, HUD, map, water right-side textured where pipe-adjacent.

## Wave-4 lane assignments (10 agents, disjoint file ownership)
| Lane | Type | Files | Task |
|---|---|---|---|
| fixer-waterpipe | fixer (RESUME fix-2) | waterPipe.cpp | decouple water pipe core from pipeChainEnable; fallback s0 identity + 3-layer waterclear256 bind; mirrored-band kill |
| fixer-building | fixer | buildingPipe.cpp | 3-layer stage-0 diffuse binds (mirror vehBindDiffuse3L) → black road |
| fixer-main | fixer | main.cpp | PedTex mannequin recovery (cache=NULL → material walk); pipeline-flip residual |
| fixer-postfx | fixer | postfx.cpp | dark-at-noon exposure blackout; corona drv=0 L2 divergence source |
| fixer-chars | fixer | chars.cpp | bandana green mask (S5) |
| fixer-tex | fixer | texalpha.cpp/texdefs.cpp | F6 interior black quads + S6 plant silhouettes classification |
| fixer-veg | fixer | grasssys.cpp/treeWind.cpp | black plant silhouettes (S6) bind path |
| fixer-neo | fixer | neopipe.cpp | verify pins healthy under new INI; WaterDrops residual |
| gen-rsmon | explorer (RO) | log only | 29 FIRST_DIVERGE samples → state/site table |
| gen-band | explorer (RO) | log+code | identify raster 1035AF80/10355B10; band draw path |
