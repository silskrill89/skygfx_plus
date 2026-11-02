# SA Destruction Mod — Inventory (2026-10-29)

Source: https://github.com/chris-efpunkt/San-Andreas-Destruction-Mod-Asi cloned to `E:\SDKs\SADestruction`.
Clone succeeded first attempt. `git tag -l` → `gtasanandreas`; tag == `main` == `origin/main` (32c18d1), i.e. the release tag
(https://github.com/chris-efpunkt/San-Andreas-Destruction-Mod-Asi/releases/tag/gtasanandreas) points at the default branch HEAD —
no checkout needed. Version in code: **v3.6.2**, plain C, freestanding (no CRT), 32-bit ASI.

---

## 1. File tree overview

```
E:\SDKs\SADestruction\
├── README.md / README.txt      (identical, 99 lines, EN)   LIESMICH.txt (41 lines, DE)
├── SADestruction.asi           prebuilt binary, 133,632 bytes
├── build.sh                    clang+lld build (LLVM 18), compiles ONLY src/sa_runtime.c
├── build\kernel32.def, user32.def   import defs for llvm-dlltool
├── sa_runtime.c                167 lines — ORPHAN: old v2.0 "Open World Surface Survey" probe
│                                tool (read-only F8 ray inspect). Not referenced by build.sh.
├── src\
│   ├── sa_runtime.c          1632 lines — platform/game glue, hooks, blast pipeline  (the only TU)
│   ├── crater_core.c         1410 lines — pure mesh-math core (#included by sa_runtime.c:145)
│   └── rock_texture.c          47 lines — procedural 128×128 rock texture (#included :146)
├── test\
│   ├── host_test.c           1074 lines — runs the real logic against simulated game memory (-DHOST_TEST)
│   ├── core_test.c            338 lines, hole_test.c 321 lines — offline mesh-core tests
│   ├── run_break.sh            (test driver: ground/roof/bridge/slab/stack scenarios)
│   ├── make_synthetic.py, export_models.py, tools\validate_terrain.py — terrain test data tools
```

Total tracked files: 18. C/H line counts: 4,989 (main logic 3,089 = sa_runtime 1632 + crater_core 1410 + rock 47).
Single translation unit: `build.sh:7-10` compiles `src/sa_runtime.c` → lld-link `/dll /entry:DllMain@12 /nodefaultlib`.

Architecture in one line: **runtime retopology of live world meshes** — every explosion carves a spherical crater into the
*original* geometry in memory (render `RpGeometry` + `CCollisionData`), no game files touched, no cached engine pointers
(stateless w.r.t. streaming, `src/sa_runtime.c:12-13`).

---

## 2. HOW destruction works

### 2.1 What breaks / what doesn't

| Category | Behaviour | Where |
|---|---|---|
| Static world (buildings, terrain, roads, bridges, walls, roofs) | **Carved**: render + collision mesh rebuilt with crater / hole / break-through | `blast()` `src/sa_runtime.c:1230-1365` |
| Props: trees, palms, lamp posts, poles | **Knocked over**: hidden static entity + spawned dynamic `CObject` of same model | `topple()` `src/sa_runtime.c:1132-1200` |
| Vegetation classification | modelInfo special type 1 (tree) / 2 (palm), or tall-thin collision extents, or small model placed ≥2× | `classify()` `src/sa_runtime.c:1079-1110` |
| Multi-instanced large models | **Never carved** — all copies share one geometry; carving one would deform all | `src/sa_runtime.c:1058-1061`, skip at `:1254-1261` |
| Vehicles | Not carved. A vehicle *explosion* (type 4/5/6) just creates a crater in the ground under it | spec table `src/sa_runtime.c:999-1013` |
| Peds / interiors | Untouched — `CGame::currArea != 0` → whole blast refused | `src/sa_runtime.c:1236` |
| Sheet surfaces (fence, railing, hedge, grille) | **Hole** punched (triangles deleted), not a bowl — decided by surface-info flag `0x00002000` of `g_surfaceInfos` | `load_sheet_surfaces()` `src/sa_runtime.c:285-292`, `gm_delete_holes` `crater_core.c:436` |
| Thin slabs / roofs / bridge decks | **Break-through**: far side opened, tunnel joining | `gm_break` `crater_core.c:1043-1154` |

### 2.2 Damage model — geometry, not HP

There are **no hit points, no impulse accumulation, no damage flags**. A blast is one geometric operation:

1. **Candidates**: scan `CPools::ms_pBuildingPool` (0xB74498), skip empty (`flags&0x80`), hidden/no-collision (`+0x1C & 0x81`),
   interiors (`area` ≠ 0/13); accept when blast is inside col bounding sphere + box inflated by reach — `find_candidates()` `src/sa_runtime.c:1016-1047`, max 16.
2. **Surface & normal**: closest collision-surface point `S`; normal = weighted sum of nearby triangle normals with
   visibility (occlusion ray `gm_probe_blocked`), incidence cosine, distance falloff, area cap, and a side-flip ramp behind
   the plane — `gm_probe()` `crater_core.c:572-618`. A "clarity" diagnostic = |Σw| / Σ|w| (`src/sa_runtime.c:1309`).
3. **Crater sphere** per explosion type `{rim r, depth d, reach}` (`g_spec[13]`, `src/sa_runtime.c:999` — grenade 2.2/0.9/2.5 …
   aircraft 4.0/1.5/3.5, molotov = 0 = no crater):
   - distance scale `scale = 1 − 0.5·dist/reach` (`src/sa_runtime.c:1294`)
   - `R = (rim² + depth²)/(2·depth)`, centre `C = S + n·(R−d)`, pole `Q = C + n·R` (`:1295,1305-1306`)
   - **inverted-crater guard**: `blast_behind()` (`:1210-1226`) checks the visible surface doesn't show the blast its back
     (car wreck slightly under the road) and flips `n` — this was the v3.6.1 "mound instead of crater" bug.
   - rockets/shells (types 2/3/10) use the **camera** as viewer to pick the hit side (`:1240-1243`, `GM_VIEW_BAND` `crater_core.c:537`).
4. **Per-object cut pipeline** `gm_crater()` `crater_core.c:1375-1391`:
   `gm_refine` (local subdivision until edges crossing the sphere ≤ h = 0.6·rim; guard-sphere confining so repeated craters
   don't explode triangle count) → `gm_cut` (exact split on the sphere = crisp rim) → `gm_break` (break-through walk, see below)
   → `gm_after_cut` callback (runtime flags/veto) → `gm_delete_holes` → `gm_project` → `gm_drop_debris`.
5. **Bowl projection** `gm_project()` `crater_core.c:368-396`: every vertex inside the *ground-side half* sphere is mirrored
   from pole `Q` onto the far side of the sphere (rim vertices are fixed points ⇒ bowl stays welded to the rim). Air-side
   geometry (fence tops, poles) untouched. Vertex colour darkened quadratically toward centre; normals re-pointed at `C`.
6. **Material-end clamp ("how deep is the material")** `gm_break_walk()` `crater_core.c:770-876`: per vertex, ray from `Q`
   through the vertex; sorted parity walk over hits (front/back faces, rock counted separately) ⇒ `gm_clamp[v] = −1` (solid
   to the sphere) or `≥0` (material ends after that many metres; 0 = doesn't move). Projection stops at the clamp
   (`crater_core.c:378-381`) — this is what prevents a crater "tunnel" being dragged through the air under a bridge.
   `gm_break_split()` (`:936`) gives the opening its true outline by bisection along mixed edges.
7. **Hollow-shell veto**: `gm_enclosed()` (`crater_core.c:1187-1214`, 13-ray backs-vs-fronts test) — a building that is only a
   skin shows a hole into the void, so no hole; `gm_unbreak()` (`:1164`) undoes the cut with 3 modes; `g_veto` propagated
   from render → collision (`src/sa_runtime.c:674,715,742`).
8. **Rock material**: procedural 128×128 mipmapped texture (`rock_texture.c`), appended as an extra `RpMaterial`; bowl
   triangles re-assigned to it with **world-planar UV** (`rock_mapping()` `src/sa_runtime.c:649-659`, `gm_bowl_material`
   `crater_core.c:402-432` — shared vertices duplicated so surrounding surface keeps its mapping).
9. **Fragment/debris**: no fragment spawning anywhere. The only removal is `gm_drop_debris()` (`crater_core.c:1254-1287`):
   union-find over vertices, drop components wholly inside the sphere with no vertex outside (floating scraps after a
   break-through). Knocked-over props are *whole models* handed to the game's own physics, not fragments.
10. **RpHAnim / atomic flags**: not used at all. No custom `RpAtomic` plugin data, no per-frame render flags. Entity state
    is mutated via `CEntity+0x1C` bits (0x81 = not visible / no collision) and LOD `+0x30→+0x1C` (`hide_entity()` `src/sa_runtime.c:1118-1128`).

### 2.3 State storage (all static arrays in the ASI, none in engine memory)

| Store | Size | Purpose | Line |
|---|---|---|---|
| `g_undo[512]` | ≤96 MB budget | per-**model** F8 undo: raw geometry snapshot (`snap`, malloc'ed), original `CCollisionData*` kept-not-freed, current geom/col ptrs, saved bounds | `src/sa_runtime.c:431-446` |
| `snapshot_take/load` | header+arrays | plain-data copy of RpGeometry contents (verts, tris, prelight, UVs×2, normals, day/night colours) — **not** an engine object, so streaming can't invalidate it | `:831-881` |
| `g_hidden[256]` | fixed | entity + LOD flags before hiding (F8 restores) | `:1112-1114` |
| `g_fallen[128]` | fixed | spawned `CObject*` + model + removal deadline | `:1115-1117` |
| `g_mark[256]` | ring | crater spheres, for "was the player standing in one" on F8 | `:1227-1229` |
| `g_slab[20000]` | 1 byte/model | per model: has shown a far side (rock-aware for later blasts) | `:675` |
| `g_queue[32]` | ring | pending blasts; **2 processed per frame** (`CRATERS_PER_FRAME`) | `:85,1368-1394` |
| `g_seenActive/Stamp/Pos[16]` | per explosion slot | edge-detection of `CExplosions` slots | `:1370` |
| Scratch meshes | ~static BSS | `g_cPos/g_cTri` collision, `g_rPos/g_rTri` + UV/norm/prelight/col buffers, `g_extra[64K]`, `g_groups`, shell/hole tri lists | `:271-283,670` |

Everything is keyed by **model id** and *ptr re-validated on use* (`commit_col` re-checks `cm+0x2C == t->colData` `:488`;
undo entries discarded if model was streamed back in `:538,974,1466`). Reset (F8) rebuilds geometry from snapshot and
re-frees; caps: mesh limit **32 000** verts/tris (deliberately under the signed-16-bit 32 767 crash cliff — comment
`:73-76`), 512 undo models, 96 MB, 256 hidden, 512 marks.

### 2.4 Collision rebuild specifics (often the harder half)

`read_col` (`:303-359`) converts **boxes → 12 outward-facing triangles each** (buildings are mostly boxes; only triangles
can be carved), vertices are int16/128 quantized (`quant()` `:360`), spheres/lines carried over raw. `commit_col`
(`:484-545`) allocates **one** block via `CMemoryMgr::Malloc` laid out exactly like the game's loader
(`CCollisionData | spheres | boxes | lines | verts | face groups | group count | tris`), builds the optional shadow-mesh
alias, recomputes AABB + radius, calls `CCollision::RemoveTrianglePlanes` on the old block (planes rebuilt lazily by the
game), then swaps bounds → pointer → frees old. **Face groups**: 64×64 bucket grid, cell ≥ 8 m (doubled until ≤64 cells
across), ≤48 tris/group with AABB, flag bit 2 in `CCollisionData.flags` (`build_groups()` `:368-413`) — otherwise the
game's sphere-vs-face broadphase crawls.

---

## 3. How it hooks the game

**Single hook, no render callbacks, no D3D state.**

- `DllMain` (`src/sa_runtime.c:1606-1621`) → `install_hook()` (`:1582-1599`):
  - Verifies `byte[0x53E981] == 0xE8` (the `call CGame::Process` inside `CIdle`), reads the rel32 to recover the previous
    target `g_chain`, checks exe signature at `0x401000` ∈ {`0x53EC8B55` compact, `0x16197BE9` hoodlum} or target == known
    `CGame::Process` (0x53BEE0). Fails → log "HOOK FAILED", mod inactive.
  - `patch_call()` (`:1574-1581`): `VirtualProtect` → overwrite the 4 rel32 bytes → restore → `FlushInstructionCache`.
    **It is a pure call-target patch** (no trampoline/InterceptCall framework), and it **chains**: the previous target is
    called first (`hook_game_process` `:1570-1573` calls `((VoidFn)g_chain)()` then `tick()`), so other mods hooked at the
    same site survive (README "chained, not replaced").
- `tick()` (`:1557-1567`): F7/F8 via `GetAsyncKeyState` (only when this process is foreground) → `frame_tick2`:
  toggle props → `poll_explosions()` → F8 `reset_all()` → `process_queue()` (≤2 blasts/frame). Runs once per game frame
  on the game thread, after world update, before rendering (`:1568-1569`).
- **Explosion detection is polling, not a hook**: `CExplosions[16]` at 0xC88950, stride 0x7C — active byte `+0x28`,
  expire stamp `+0x20`, pos `+4/+8/+0xC`, type `+0`; edge-detected (active+stamp+pos change) → 32-entry ring queue
  (`:1376-1387`).
- **Engine calls** are raw absolute-address function pointers (no WRAPPER/EAXJMP, no headers):
  `RpGeometryCreate/Destroy/Unlock`, `_rpMaterialListAppendMaterial`, `RpAtomicSetGeometry`,
  `RpD3D9GeometrySet/GetUsageFlags`, `RwRasterCreate/Lock/Unlock/GetNumLevels`, `RwTextureCreate`,
  `RpMaterialCreate/SetTexture`, `CMemoryMgr::Malloc/Free`, `CCollision::RemoveTrianglePlanes`,
  `CWorld::ProcessLineOfSight` (buildings only, `:1533-1535`), `CObject::Create`, `CWorld::Add` — all in `:1536-1555`.
- **Memory access** goes through `ReadProcessMemory`/`WriteProcessMemory` on self with an `addr_ok` range check
  (`:1513-1521`) instead of raw stores — every read/write is validated and failure is a logged skip, never a crash.
- Geometry swap reaches **all** instances: `CModelInfo::ms_modelInfoPtrs[0xA9B0C8]` master atomic (`mi+0x1C`) **plus** a
  full building-pool sweep replacing every atomic whose `RpGeometry == old` (`write_geometry()` `:979-991`). 2Dfx effect
  plugin block is *moved* to the new geometry (model-info count stays right, `:964-970`); day/night `extraVertColour`
  buffers reallocated via `CMemoryMgr` so the plugin destructor frees them correctly (`:943-954`).

---

## 4. Reusable for skygfx_plus_expIV

### (a) `src/entities/sftbdyphsx.cpp` softbody / gib system (Bullet ragdoll + dismemberment)

1. **Spawn debris as a game-physics `CObject` instead of (or beside) a Bullet body** — `topple()` `src/sa_runtime.c:1168-1194`:
   `CObject::Create(model, 0)` → write matrix from entity frame → mass 400 (`+0x8C`), turn mass ≈ `m·L²/12` (`+0x90`),
   friction 0.97 / elasticity 0.1 (`+0x98/+0x9C`), CoM (`+0xA4`), flags `+0x40 |= gravity|collidable`, `+0x1C &= ~static`,
   type 3 temporary (`+0x13C`), removal time now+120 s (`+0x150`), `CObject::nNoTempObjects++`, `CWorld::Add`, then
   linear velocity away from blast (`+0x44`) and **height-scaled angular velocity** (`+0x50`, `0.12/height` so a 30 m palm
   doesn't whip — `:1193`). → cheap "gib that collides with SA world (sphere-only) and is auto-cleaned by the game".
   Pair with the existing Bullet gib path when we need true geometry collision.
2. **Trunk/limb collider synthesis from mesh**: lower-half vertex centroid + max radius, clamped 0.3–1.2 m, spaced column
   of ≤8 spheres along Z (`src/sa_runtime.c:1138-1166`) — reusable to build sphere-chain colliders for gib/limb proxies when
   a model has no spheres (moving objects only collide via spheres in SA!).
3. **Deterministic crack-free mesh cutting** — `crater_core.c` entire core:
   canonical edge ordering (`gm_less` `:74`) so both triangles of a shared edge (and duplicated seam verts) split
   identically; edge→vertex open hash (`gm_hash_slot` `:129`) dedups splits; `gm_refine` guard-sphere band (`:211`) confines
   refinement to the wound so repeated cuts don't multiply triangles. Directly applicable if we want *geometric* gib
   separation (cut a limb off a clump's mesh) instead of only hiding/disappearing vertices.
4. **Sliver cleanup after repeated deformation**: `gm_weld_short()` `crater_core.c:1317-1364` (collapse < eps edges where
   only deformation-material meets, refusing any weld that would flip a triangle normal) + `gm_drop_degenerate()` `:1394`
   + `gm_compact()` `:1291` — exactly the post-pass a repeatedly-dented/de-boned mesh needs.
5. **Floating-scrap removal**: `gm_drop_debris()` union-find anchoring `crater_core.c:1254-1287` — prune components not
   connected to anything outside the damage sphere.
6. **Undo pattern**: raw-bytes geometry snapshot keyed by model with byte budget (`snapshot_take` `src/sa_runtime.c:836`),
   never holding engine pointers across streaming (`:424-430`) — same discipline we need for "vehicle repaired".

### (b) Tree bending / cracking (`src/render/treeWind.cpp` + `grasssys`)

1. **Prop classification without trusting the bounding box** — `classify()` `src/sa_runtime.c:1079-1110`: measure the
   *collision shapes* (bottom-40 %-of-height footprint vs full extent) because a palm's bbox includes a 16 m crown; flags
   read straight from `CBaseModelInfo+0x12`: `nSpecialType = (w>>11)&0xF` (1 tree / 2 palm), road bit `0x100` kills both
   prop and tall-thin test. Our `TreeWind_IsTreeAtomic` (treeWind.cpp:88) identifies trees at atomic level; this gives the
   *geometry-derived* fallback (any tall-thin collision = pole/tree) — useful for map-modded trees that lack special types.
2. **Knock-over = hide static + replace with dynamic instance** (`hide_entity` `:1118`, `topple` `:1132`) with the
   state records needed to stand it back up (`g_hidden`/`g_fallen`, F8 restore `:1472-1486`). For "tree crack and fall"
   we can reuse the whole pattern (our treeWind currently only leans vertices via transform — see `TreeWind_ApplyToTransform`
   treeWind.cpp:191).
3. **Bend-limit analogue**: `gm_clamp[]` per-vertex displacement budget + `gm_project` stopping exactly at material end
   (`crater_core.c:378-381`) is the general "move vertices toward a target but stop at a physical boundary" primitive —
   directly reusable as a crack/bend limiter (e.g. stop bending where trunk thickness changes) and as the guard against
   foliage vertices being dragged with the trunk (air-side half excluded, `gm_inside` `crater_core.c:309`).
4. **Occlusion-aware normal/direction field** — `gm_probe` (`crater_core.c:572`): weighted normals with visibility rays,
   incidence and area caps. Any "damage direction" for a leaning/cracking tree (or wind-direction ground truth) can reuse it.
5. **Procedural texture with its own mips** — `rock_texture.c:21-46`: wrapping value-noise (4/8/16/32/64-cell lattices,
   ridge-noise crack lines) + `rock_halve` mip generator → port for procedural bark-damage/foliage-break textures without
   shipping assets (matches our shader-side procedural habits).
6. **grasssys cross-over**: `gm_bowl_material`'s *world-planar UV with vertex duplication* (`crater_core.c:402-432`) and
   `rock_mapping` dominant-axis projection (`src/sa_runtime.c:649`) are the right way to texture a disturbed/damaged ground
   patch under broken grass so it lines up across neighbouring objects — same trick our grass instances could use for a
   "trampled/blasted" material layer.

### (c) Vehicle deformation softbody

1. **`gm_crater` as a dent operator**: run `gm_refine → gm_cut → gm_project` (without break-through) with a small sphere
   at the impact point: crisp rim, watertight mesh (no triangles deleted — `crater_core.c:22`), per-vertex clamp gives
   panel-depth limits (`gm_clamp`), `shade` parameter darkens the dent centre. Impact normal from `gm_probe`
   (camera-as-viewer variant `src/sa_runtime.c:1280` for point-blank hits).
2. **Bounded re-topology budgets**: mesh limit 32 000 with the signed-16-bit rationale (`src/sa_runtime.c:73-76`),
   per-blast outline-split budget `GM_MAX_OUTLINE 600` / mid-point `GM_MAX_MID 150` (`crater_core.c:934-935`),
   refine pass cap 48 (`:33`) — the numbers to copy so a deforming car mesh never blows up (indices are U16).
3. **Geometry capture/restore for "repair"**: `GeoInfo`/`read_render` (`src/sa_runtime.c:548-597`) documents the exact
   `RpGeometry` layout we must preserve (morph target, prelight `flags&8`, UV sets ≤2, normals `flags&0x10`,
   `extraVertColour` plugin offset, 2Dfx plugin) and `snapshot_take/load` (`:831-881`) shows a CRT-free save/restore that
   survives streaming. `write_geometry` (`:915-995`) is the safe "put a new RpGeometry on an atomic" recipe: copy material
   list in order (triangle material indices stay valid), copy D3D9 usage flags (`:962`), `RpGeometryUnlock` must build the
   mesh (`:963`), grow the bounding sphere (`:955-959`), swap master atomic + all instances, drop our creation ref (`:992`).
4. **Per-instance cloning caveat (important)**: SA Destruction can only carve *uniquely placed* models — vehicles share
   geometry across instances too (`src/sa_runtime.c:1058-1061`). For vehicle deformation we must build the dented geometry
   **once per vehicle instance** and swap only that vehicle's atomic (the pool-sweep in `write_geometry` `:982-991` would
   hit every car of the same model — vehicle pool instead of building pool: `CVehicles` pool, different address).
5. **Collision side**: if deformation should affect collision, `read_col`/`commit_col` (`src/sa_runtime.c:303,484`) give the
   exact `CCollisionData` single-block layout incl. face groups + `RemoveTrianglePlanes` ordering (bounds first, then
   pointer, then free old — `:529-533`). Vehicles use spheres/lines mostly, so the box→triangle conversion is only needed
   for box-based collision.
6. **Anti-inverted-result guard** — `blast_behind()` (`src/sa_runtime.c:1210-1226`): when the impact normal points the
   "wrong" way relative to the visible surface, flip before deforming; the same class of bug (dent becoming a bump) applies
   to panel deformation.

**Not reusable here**: no physics/impulse model, no fragment spawning, no RpHAnim usage, no render/D3D-state hooks — this
mod never enters a render callback. Anything impulse-based must come from our Bullet side or the game's own
`CObject`/`CEntity` physics (as `topple()` does).

---

## 5. Licensing note

- **No `LICENSE` file exists** in the repository (verified: `git ls-files` = 18 files, none is a license; GitHub API
  `"license": null`).
- No license/copyright text in `README.md`, `README.txt`, `LIESMICH.txt`, `build.sh`, sources or tests (grep for
  license/copyright/GPL/MIT/all-rights found only incidental word matches such as "limit of a game model").
- Consequence: source is published-but-unlicensed → **all rights reserved by the author**. Reading it for study is fine;
  copying code into skygfx_plus_expIV (or distributing derived bits) requires the author's permission. Treat everything
  above as *reference/ideas*, not copy-paste material, until a license is obtained or the author grants permission.

---

## Quick reference — key addresses (GTA SA 1.0 US)

| Address | What |
|---|---|
| 0x53E981 | hook site: `call CGame::Process` in Idle() |
| 0x53BEE0 | `CGame::Process` (expected chain target) |
| 0xC88950 | `CExplosions[16]`, stride 0x7C (active +0x28, stamp +0x20, type +0, pos +4) |
| 0xB74498 | `CPools::ms_pBuildingPool` (storage/flags/cap at +0/+4/+8; entry 0x38) |
| 0xA9B0C8 | `CModelInfo::ms_modelInfoPtrs` (colModel +0x14, master atomic +0x1C, +0x12 flags) |
| 0xB79538 | `g_surfaceInfos` (0x90 adhesion table, 179×12-byte entries; bit 0x2000 = sheet) |
| 0x401000 | exe signature: 0x53EC8B55 (1.0 US) / 0x16197BE9 (hoodlum) |
