# RW Physics / Ragdoll RE — Research & Unified Softbody Architecture Plan

**Date:** 2026-10-29
**Mode:** READ-ONLY research. No source files edited. One new file (this document).
**Goal:** Reimplement ragdoll physics *primarily* from RenderWare primitives + the leaked partial GTA SA source, replacing/upgrading `src/entities/sftbdyphsx.{cpp,h}` toward a full softbody system: ped flesh deformation, tree bending/cracking, vehicle softbody.
**Predecessor:** `docs/plans/2026-07-30-ragdoll-restoration.md` (ragdoll-only restoration plan). This document supersedes and widens its scope; findings below correct two of its assumptions (see §4.2).

---

## 0. TL;DR

* **RW itself provides no physics** — there is no `RwPhysics` plugin in either SDK (header inventory, §2.4). What RW *does* provide is everything a physics layer needs to **read and write skeletal state**: RpHAnim hierarchy matrix arrays, keyframe/interp-frame quaternion editing, per-frame capture bits in the game's RpAnimBlend plugin, RpSkin weight/index access, quaternion math (RtQuat/slerp), and triangle/sphere collision queries (RpCollis, RtRay, RtIntSec).
* **The game's own ragdoll system exists in two documented halves**: Mark Nicholson's 2003 prototype (`gtasa_src-main/Ragdoll.cpp`, fully commented out — velocity integrator, gravity, friction, parent-space axis→quat rotation, `BlendKeyframe` slerp back into animation) and the *shipped* bone-node layer (`BoneNode_c` / `BoneNode_c::BlendKeyframe` at 0x616E30, fully reversed in `gta-reversed-master`), which the prototype was designed to sit on. Together they specify the complete native design: **capture anim frames → integrate per-bone linear velocity → convert to parent-space rotation → slerp into the RpAnimBlend interp frame → let the game's own matrix update propagate to the skin**.
* **Our current code splits into two systems with different quality**: the death ragdoll (`ragdoll_death.cpp`, Bullet, 11 capsules + cone/hinge constraints) is feature-rich but writes *world-space* matrices into the hierarchy (§4.3); the living overlay (`sftbdyphsx.cpp` `Ragdoll_c`) redeclares `AnimBlendFrameData` at the **wrong stride (0x8 vs shipped 0x18)** and applies Rodrigues rotations directly on top of animation matrices (§4.2).
* **The upgrade path is to port the native design** (BoneNode_c + Ragdoll_c completed with the fixes the prototype left commented out: all-bone processing, euler limits, ground collision), keep our game integration (ped-pool scanning, death polling at `CPed+0x530==66`, dismemberment bake pipeline), and house it all in one TU with a TexDefs-style lifecycle that also serves tree bend/crack and vehicle panel softbody later.

---

## 1. Evidence base

| # | Source | Path | What we used |
|---|--------|------|--------------|
| S1 | Leaked partial GTA SA source (Mark Nicholson snapshot, 2003) | `E:\dev(dave)\releases\GTASource\gtasa_src-main\` | `Ragdoll.cpp` (435 lines, entire body inside `/* */`), `Game.cpp`, `Init_overlay.cpp`, `Main.cpp` (ragdoll lifecycle call sites), `Ped.cpp` / `PlayerPed.cpp` (bone manipulation precedents), `Automobile.cpp` (damage/panel code) |
| S2 | gta-reversed (community full reversal, SA v1.0 US addresses) | `E:\dev(dave)\releases\GTASource\gta-reversed-master\source\game_sa\` | `BoneNode_c.{h,cpp}`, `BoneNodeManager_c.{h,cpp}`, `tBoneInfo.h`, `Enums\eBoneTag.h`, `Animation\AnimBlendFrameData.h`, `Animation\AnimBlendClumpData.h`, `BouncingPanel.h`, `Entity\Ped\Ped.cpp` (ShoulderBoneRotation), `World.cpp` (LOS/ground-Z addresses), `Collision\*` |
| S3 | RenderWare 3.6 / 3.7 SDKs | `E:\SDKs\RW36_031126\`, `E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9\` | `rphanim.h`, `rpskin.h`, `rtquat.h`, `rtslerp.h`, `rpcollis.h`, `rtray.h`, `rtintsec.h`, `rpworld.h`; full d3d9 header inventory (§2.4) |
| S4 | SA_PC leak tree | `E:\dev(dave)\releases\GTASource\SA_PC\sa_pc_src\gta_source\Ragdoll\` | **Empty** — directory skeleton only (verified: 0 files in all three fork trees). The PC-final `BoneNode.cpp` / `Ragdoll.cpp` / `IKChain.cpp` are *not* available; S1+S2 remain the complete picture |
| S5 | Our tree | relative paths below | `src/entities/sftbdyphsx.{cpp,h}`, `src/extras/ragdoll_death.{cpp,h}`, `src/render/treeWind.cpp`, `src/core/main.cpp` hook sites, `src/texdefs.h` (lifecycle pattern), `docs/plans/2026-07-30-ragdoll-restoration.md`, `docs/plans/2026-10-26-cj-ring-investigation.md` |
| S6 | OpenSA / plugin-sdk cross-check | referenced via S2 headers (plugin-sdk attribution blocks) | `eBoneTag` values, `CAnimBlendClumpData` size 0x14, ped offsets |

**Not found / ruled out:** a `RagdollManager_c` reversal in gta-reversed or plugin-sdk (only `BoneNode*` and `IKChain*` are reversed — the shipped manager layer was never documented by the community); any Ragdoll folder content in the SA_PC leak (S4).

---

## 2. What RW natively provides for skeletal deformation

### 2.1 RpHAnim — the deformation substrate (SDK `rphanim.h`)

* **Hierarchy = matrix array + node info** (`rphanim.h:217-232`): `RpHAnimHierarchy { flags, numNodes, pMatrixArray, pMatrixArrayUnaligned, pNodeInfo, parentFrame, parentHierarchy, currentAnim }`. `pMatrixArray` is the *the* write target: one `RwMatrix` per bone, indexed by `RpHAnimIDGetIndex(hierarchy, boneTag)` (`rphanim.h:405-407`). Skinning reads this array directly; anything written here before render deforms the mesh.
* **Runtime matrix update modes** (`rphanim.h:182-195`): `rpHANIMHIERARCHYUPDATEMODELLINGMATRICES (0x1000)`, `rpHANIMHIERARCHYUPDATELTMS (0x2000)`, `rpHANIMHIERARCHYLOCALSPACEMATRICES (0x4000)`. `RpHAnimHierarchyUpdateMatrices()` (`rphanim.h:399-400`) recomposes child = keyframe × parent down the array — this is how a *local* rotation edit on a parent bone propagates to descendants without touching them.
* **Keyframe editing primitives** (`rphanim.h:416-439`): `RpHAnimKeyFrameApply/Blend/Interpolate/Add/MulRecip`, plus `RpHAnimKeyFrameToMatrixMacro` (`rphanim.h:263-279`) which converts an interp frame `{RtQuat q; RwV3d t}` straight into an `RwMatrix`. The interp-frame struct is exactly `RpHAnimInterpFrame { keyFrame1, keyFrame2, q, t }` (`rphanim.h:132-140`).
* **Quaternion math baked into the exe**: `rtquat.h` (`RtQuatConvertFromMatrix`, `RtQuatRotate`, `RtQuatReciprocal`, `RtQuatTransformVectors`, `RtQuatUnitConvertToMatrixMacro`) and `rtslerp.h` (`RtQuatSetupSlerpCache`, `RtQuatSlerp`). We already call all of these via game addresses (`src/entities/sftbdyphsx.cpp:34-43`, `src/rw/rtquat.h`, `src/rw/rtslerp.h`).

### 2.2 Capture bits — the animation hand-off (game's RpAnimBlend plugin)

The shipped `AnimBlendFrameData` (reversed at `gta-reversed-master\...\Animation\AnimBlendFrameData.h:12-39`, `VALIDATE_SIZE 0x18`) carries per-frame bit flags:

| bit | name | effect |
|-----|------|--------|
| 1 | `KeyFramesIgnoreNodeOrientation` | anim system stops writing this node's rotation — physics owns it |
| 2 | `KeyFramesIgnoreNodeTranslation` | same for translation |
| 3 | `HasVelocity` | root-frame translation drives ped movement |
| 5 | `NeedsKeyFrameUpdate` | defer `RpAnimBlendNodeUpdateKeyFrames` |

The 2003 prototype captured every frame with `rpFRAMEROTCAPTURED (0x100)` / `rpFRAMETRANSCAPTURED (0x200)` (`gtasa_src-main\Ragdoll.cpp:64-70`, released at 110-114) — **in the shipped struct those constants don't exist; bits 1/2 are the equivalent**. `CAnimBlendClumpData` (0x14, `AnimBlendClumpData.h:14-56`) hangs off the clump at **+0x5C** and owns `m_FrameDatas` (stride 0x18).

### 2.3 Skinning read-side — `rpskin.h`

`RpSkinGeometryGetSkin`, `RpSkinGetNumBones`, `RpSkinGetVertexBoneWeights` (4×`float`), `RpSkinGetVertexBoneIndices` (4×u32), `RpSkinGetSkinToBoneMatrices` — already used verbatim by the dismemberment limb bake (`sftbdyphsx.cpp:578-587`, `837-884`). This is the exact API needed for **flesh deformation** (per-vertex spring offsets applied after bone transforms, §5.3) and for **vehicle crumple** (bake a mutable copy of a damage atomic, §5.5).

### 2.4 Collision & world queries — what exists, what doesn't

* **No physics plugin.** Full d3d9 header inventory (`RWSDK37\Graphics\rwsdk\include\d3d9\`): `rpcollis.h`, `rtray.h`, `rtintsec.h` are the only motion/intersection-related plugins. There is no rigid-body, constraint, or softbody plugin in any RW SDK revision. "RW physics" must be *our* code.
* **RpCollis** (`rpcollis.h:527-959`): per-world-sector collision trees (`RpCollisionWorldSectorGetCollTree`), `RpCollisionWorldForAllIntersections`, `RpCollisionGeometryForAllIntersections`, build via `RpCollisionWorldSectorBuildData`. Usable for static-world collision of softbody particles *if* we build coll trees for the sectors we care about — but the game already ships a better-integrated query API:
* **Game collision (recommended):** `CWorld::ProcessLineOfSight` = **0x56BA00**, `CWorld::FindGroundZForCoord` = **0x569660** (`gta-reversed-master\...\World.cpp:51,72`). These see buildings, dummies, vehicles, peds — everything RpCollis would not, without rebuilding coll data.
* **RtRay / RtIntSec** (`rtray.h`, `rtintsec.h`): raw ray/intersection primitives; unnecessary given the game queries above.

### 2.5 Native deformation precedents inside the game exe (not RW, but shippable patterns)

* **`CPed::ShoulderBoneRotation`** — 0x5DF560, reversed at `gta-reversed-master\...\Entity\Ped\Ped.cpp:1207-1238` (leaked at `gtasa_src-main\Ped.cpp:8669-8722`). After `UpdateRpHAnim`, copies the upper-arm node matrix onto the breast node, halving its X rotation in clavicle space. **This is the shipped flesh-deformation idiom**: post-update, per-node matrix surgery on `RpHAnimHierarchyGetNodeMatrix`, no skinning changes at all. Our flesh layer is a generalization of exactly this.
* **`CBouncingPanel`** — `gta-reversed-master\...\BouncingPanel.h:5-31` (`VALIDATE_SIZE 0x20`): per-frame spring-damper for car doors/bonnet/boot/exhaust with shipped constants at 0x8D3954-0x8D3964 (`SPRING_DAMP_MULT 0.95`, `SPRING_RETURN_MULT 0.1`, `VEL_CHANGE_LIMIT 0.1`, `HANGING_DAMP_MULT 0.98`, `HANGING_RETURN_MULT 0.02`). Used from `CAutomobile` (leak `Automobile.cpp:332-347` sets `BouncingPanels[0].SetPanel(CAR_MISC_B/CAR_BOOT/CAR_EXHAUST, …)`). **This is the shipped vehicle-softbody atom** — phase 3 generalizes it (§5.5).
* **Damage atomics** — `Models\DamageAtomicModelInfo.{h,cpp}` (S2): vehicles swap to pre-baked damage atomics; combined with panel springs this is SA's entire vehicle deformation story. No vertex crumpling exists natively (that's GTA IV).

---

## 3. What the GTASource leak shows the game does for ragdoll

### 3.1 The prototype (`gtasa_src-main\Ragdoll.cpp`, commented out but complete)

**Class shape** (lines 27, 38-97, 299-434): `Ragdoll_c` owns `BoneNode_c m_boneNodes[BONETAG_MAX_NUM]` (32), a root pointer, and `float m_blend`; `RagdollManager_c` owns a pool `m_ragdolls[NUM_RAGDOLLS]` + active list. Mirror-identical to our `Ragdoll_c` / `RagdollManager_c` (`sftbdyphsx.h:98-151`).

**Init** (54-97):
1. Marks every `AnimBlendFrameData` frame with `rpFRAMEROTCAPTURED | rpFRAMETRANSCAPTURED` and zeroes the clump velocity (60-70) — freezes the anim system on those bones.
2. For each of `aBONETAG_ENUM_TAB[0..BONETAG_MAX_NUM)`: `boneId = RpHAnimIDGetIndex(pHierarchy, boneTag)`, grabs `frameData[boneId].pStdKeyFrame`, and `m_boneNodes[i].Init(boneTag, pAnimKeyFrame)` (75-82). **The bone node reads its bind pose `q`/`t` straight out of the RpAnimBlend interp keyframe.**
3. `SetupBoneHierarchy()` (255-273): root = node for `BONETAG_ROOT`; parent links built from `BoneNodeManager_c::ms_boneInfos[i].parentBoneTag`.

**Integrator** — `ResolveSystem` (176-248), semi-implicit Euler on per-bone **linear velocity**:
1. Gravity: `m_vel.z -= 9.81f * deltaTime` for all bones (181-184).
2. Rotation update — **the core native trick** (187-241), per bone (prototype: HEAD only, 189-192):
   * `parentToNodeVec = node.pos − parent.pos` (from the world matrices computed by `CalcWldMat`) (194-196);
   * `axis = cross(parentToNodeVec, m_vel)` — the rotation axis that swings the bone along its velocity (198-200);
   * `speed = normalize(axis)` — magnitude becomes the angle (202-203);
   * axis rotated into parent space via `RtQuatReciprocal(parentQuat)` + `RtQuatTransformVectors` (208-220);
   * applied as `RtQuatRotate(&parent->m_keyFrameQuat, relativeAxis, speed, rwCOMBINEPOSTCONCAT)` (226);
   * `CalcWldMat` refreshed down the subtree (231-239).
3. Friction: `m_vel.x *= 0.98f` — **x-component only** (prototype bug; 244-247).

**Blend back** — `Ragdoll_c::Update` (125-143): `root.CalcWldMat(pedMatrix)` → `ResolveSystem(dt)` → per bone `BlendKeyframe(m_blend)`. `SetBlend` exposes the anim↔physics crossfade (150-153). `ApplyForce` injects `dir*force` into a bone's velocity (160-169) — the bullet-impact entry point.

**Lifecycle** (live call sites in the same leak):
* `Init_overlay.cpp:917-922` — `g_boneNodeMan.Init()` **active**, `g_ragdollMan.Init()` **commented out**.
* `Game.cpp:809-810` — `g_boneNodeMan.Exit()` active; `Game.cpp:742-745` ragdoll Exit commented; `Game.cpp:1252-1255` — `g_boneNodeMan.Reset()` active, `g_ragdollMan.Reset()` commented.
* `Game.cpp:569-570` — `BoneNodeManager_c::ms_renderDebugInfo` wired to VarConsole ("Render Ragdoll & IK Info").
* `Main.cpp:296, 3461-3466` — includes `Ragdoll.h`; `g_ragdollMan.RenderDebug()` commented.

**Conclusion:** the shipped game kept the BoneNode/IK layer alive (it feeds ped IK and related systems) but the *ragdoll manager itself* was never enabled — this is the "cut ragdoll" the2026-07-30 plan restores. There is no hidden shipped ragdoll to conflict with.

### 3.2 The shipped bone-node layer (`BoneNode_c`, the ragdoll's substrate)

Fully reversed with addresses (`gta-reversed-master\...\BoneNode_c.cpp`):

* `Init(boneTag, interpFrame)` — 0x6177B0 (32-45): stores the `RpHAnimBlendInterpFrame*`, captures `m_Orientation = interpFrame->q`, `m_Pos = interpFrame->t`, `InitLimits()`, `m_Speed = 1.0f`.
* `InitLimits()` — 0x617490 (48-60): derives min/max euler limits from `ms_boneInfos[id]` via the (odd but shipped) mixing `m_LimitMin = {Max.x−Min.x, Max.y−Min.z, Max.z−ABC.y}`, `m_LimitMax = {Min.y+Max.x, ABC.x+Max.y, ABC.z+Max.z}`.
* `Limit(blend)` — 0x617650 (156-178): `QuatToEuler` → clamp X/Y to `[min,max]` → **head-specific Z coupling**: for `BONE_HEAD`, the Z clamp range is warped by `multy = max(|eulerX|/−45+1, 0)` around the table's head `Max.z` — i.e. you can nod further before the yaw limit tightens. → `EulerToQuat`.
* `BlendKeyframe(blend)` — 0x616E30 (181-187): `RtQuatSlerp(&interpFrame->q, anim_q, m_Orientation, blend, cache)` — **writes the physics orientation into the animation interp frame**, so the game's own `RpAnimBlendClumpUpdateAnimations` → `UpdateRpHAnim` pipeline delivers it to the skin. This is the correct write-back point; direct `pMatrixArray` pokes fight the per-frame anim update.
* `CalcWldMat(parentMat)` — 0x616CD0 (218-232): local rot-mat from `m_Orientation` + `m_Pos`, `RwMatrixMultiply` by parent, recurse children — used for debug draw / world-space queries, *not* for skinning.
* `GetIdFromBoneTag` — 0x617050 (100-107): linear search of `ms_boneInfos` by `m_current`.
* `BoneNodeManager_c` (`BoneNodeManager_c.h:10-31`): **pool of 128 `BoneNode_c`** (`VALIDATE_SIZE 0x4C0C`), global at **0xC10820**, `ms_boneInfos` = `std::array<tBoneInfo, 32>` at **0x8D26D0**.
* `tBoneInfo` (`tBoneInfo.h:6-13`, size 0x28): `{ eBoneTag m_current; eBoneTag m_prev /* parent tag */; CVector m_Max, m_Min, m_ABC; }` — **`m_prev` is the parent link our `SetupBoneHierarchy` already reads** (`sftbdyphsx.cpp:403-418`).
* `eBoneTag` (`Enums\eBoneTag.h:11-55`): 0-8 trunk/head, 21-26 right arm, 31-36 left arm, 41-44 left leg, 51-54 right leg, 201 belly, 301/302 breasts, `MAX_BONE_NUM = 32`. Our `aBONETAG_ENUM_TAB` (`sftbdyphsx.cpp:59-66`) lists exactly these 32 tags in the same order — verified match.

### 3.3 Shipped kinematics order (where physics must slot in)

Per-frame ped order (leaked `Ped.cpp` + reversed `Ped.cpp`): `RpAnimBlendClumpUpdateAnimations` writes interpolated `{q,t}` into each `AnimBlendFrameData.KeyFrame` → `CPed::UpdateRpHAnim()` (leak `Ped.cpp:956`, called again at PreRender `Ped.cpp:8803`) pushes keyframes into `pMatrixArray` and calls `RpHAnimHierarchyUpdateMatrices` → `CPed::ShoulderBoneRotation` matrix surgery → render. **Physics that writes into the interp frame (BlendKeyframe idiom) rides this pipeline for free; physics that writes `pMatrixArray` directly must run after `UpdateRpHAnim` every frame or be overwritten** — the tension our current write-backs live in (§4.2/§4.3).

---

## 4. Gap analysis: our `sftbdyphsx` vs native-possible

### 4.1 Inventory of what we run today

| System | File | Integrator | Write-back | Trigger |
|--------|------|-----------|------------|---------|
| Living overlay `Ragdoll_c` | `sftbdyphsx.cpp:170-559` | per-bone velocity → Rodrigues rotation applied **directly to `pMatrixArray[i]`** (`230-304`) + `RpHAnimHierarchyUpdateMatrices` (`307`), 0.85 all-axis friction (`310-314`) | matrix poke, re-derived every frame by `Update` | `ProcessAllPeds` pool walk (`479-559`): every ped within 60 m gets a ragdoll; blend 0.5 (<15 m) / 0.25 |
| Death ragdoll | `ragdoll_death.cpp` | Bullet: 11 capsules, cone-twist/hinge constraints, flat ground plane at ped Z, hit impulse from `m_nBodypartToRemove`/`m_vecMoveSpeed` (`738-852`) | `Ragdoll_ApplyPose` (`1150-1209`) writes **world-space** quat→matrix into `pMatrixArray` | ped-pool poll for `m_ePedState(+0x530) ∈ {65,66}` (`1006-1007`) |
| Dismember | `sftbdyphsx.cpp:563-1184` | Bullet gibs; limb mesh baked from RpSkin weights/indices | zero-scale bone matrices (`1045-1071`) | `m_nBodypartToRemove` at `CPed+0x754` (`ragdoll_death.cpp:1020`) |
| Tree wind | `src/render/treeWind.cpp` | none (kinematic): absolute `at.x/at.y` lean write + `RwFrameUpdateObjects` (`276-309`), or WVP-space tilt (`190-252`) | modelling matrix / transform16 | `CEntity::ModifyMatrixForTreeInWind` hook at 0x534E90 (`main.cpp:7805`) |

Hook sites (`main.cpp`): `Ragdoll_ApplyPose()` at 3913 (RenderScene_before), `Ragdoll_Simulate()` at 5399 (post-Process), `Ragdoll_Init()` at 7705, treeWind hook at 7805.

### 4.2 Replace — living overlay (`Ragdoll_c`)

| # | Finding | Evidence | Action |
|---|---------|----------|--------|
| G1 | **`AnimBlendFrameData` redeclared at wrong stride.** We declare `{RpHAnimKeyFrame* KeyFrame; uint32 Flags}` = 0x8 with `static_assert` (`sftbdyphsx.h:38-42`) and capture constants `0x100/0x200` (`sftbdyphsx.h:22-23`). Shipped layout is 0x18 with a **uint8 bitfield** (bit1 `KeyFramesIgnoreNodeOrientation`, bit2 `IgnoreNodeTranslation`) | S2 `AnimBlendFrameData.h:12-39`; prototype struct matches 2003 layout only | Redefine correctly; use bits 1/2 for any capture. (Today's code path never indexes the array — `Init` explicitly "do NOT capture bones" (`sftbdyphsx.cpp:187`) — so nothing is corrupt *yet*, but any restoration of capture must fix this first.) |
| G2 | **Integration fights the anim system.** `Update` rotates `pMatrixArray` bones by a velocity-derived Rodrigues angle (`sftbdyphsx.cpp:230-304`) *after* the game composed them; next frame `UpdateRpHAnim` recomposes from keyframes and the effect is whatever the two writes disagree about. Blend is faked by scaling the angle (`m_blend * 100.0f` at 254), not a real slerp | §3.3; native idiom is `BlendKeyframe` into interp frame (S2 `BoneNode_c.cpp:181-187`) | Replace integrator+write-back with the native pair: velocity → parent-space axis/angle (§3.1 step 2) → `RtQuatRotate` on a **local orientation quat** → `RtQuatSlerp(anim_q, phys_q, blend)` written into `AnimBlendFrameData.KeyFrame->q` → game update pipeline. |
| G3 | **`Limit()` uses a different euler decomposition + no head coupling** than shipped (`sftbdyphsx.cpp:131-168` vs S2 `BoneNode_c.cpp:84-97,156-178`), and is currently never called from `Update` | S2 | Port `QuatToEuler`/`EulerToQuat`/`Limit` verbatim (incl. head-Z warp); enable in the resolve loop (the prototype left it commented, Ragdoll.cpp:132-136,228-229). |
| G4 | **Limits table half-trusted.** We read `tBoneInfo` at 0x8D26D0 for parents (`sftbdyphsx.cpp:403`) and copy `m_Min/m_Max` raw as euler limits (`91-97`), but shipped `InitLimits` applies the Max/Min/ABC mixing first | S2 `BoneNode_c.cpp:48-60`; `tBoneInfo.h:6-13` | Port `InitLimits` formula; verify against live memory (see R6). |
| G5 | Friction inconsistency vs prototype and vs itself (0.85 all axes `310-314`; prototype 0.98 x-only Ragdoll.cpp:244-247; death system Bullet damping 0.05/0.85 `ragdoll_death.cpp:615`) | — | One shared damping convention in the unified module (§5.2). |

**Keep (game integration, portable as-is):**
* `aBONETAG_ENUM_TAB` (`sftbdyphsx.cpp:59-66`) — exact match to `eBoneTag` (§3.2).
* Parent links via `ms_boneInfos` at 0x8D26D0 / `m_prev` (`399-419`) — same table, same semantics as `BoneNode_c::GetIdFromBoneTag`.
* Ped pool walk (`479-559`): `CPool*` at 0xB74490, stride 0x7C4, clump at `CPed+0x18`, frame LTM for distance; distance-based assignment/blend.
* `RagdollQuatSlerp` fallback via `RtQuatSetupSlerpCache` (`45-57`) — fine until we bind `RtQuatSlerp` (0x7EB210) directly.
* All address-macro plumbing (`571-624`) and the SEH-wrapped particle call (`1182-1183`).

### 4.3 Replace/repair — death ragdoll (`ragdoll_death.cpp`)

| # | Finding | Evidence | Action |
|---|---------|----------|--------|
| G6 | **World-space write-back.** `Ragdoll_ApplyPose` converts each Bullet body's world transform to a matrix and writes it into `pMatrixArray[idx]` (`1184-1196`), but `pMatrixArray` entries are **root-frame-relative** (§2.1). Works today only because a dead ped's placement is effectively frozen; any ped turn/teleport double-transforms the corpse | S2 hierarchy semantics; `RpHAnimKeyFrameToMatrixMacro` is local | Convert Bullet world transforms → *local* bone quats (`inv(parentWorld) * childWorld` via `RtQuatReciprocal`+`RtQuatTransformVectors`) before write; or adopt BlendKeyframe write-back (G2) and let the game compose. |
| G7 | **Animation overwrite race.** Write-back happens once per render frame from `RenderScene_before` (`main.cpp:3913`), but the game recomposes `pMatrixArray` during PreRender `UpdateRpHAnim` (leak `Ped.cpp:8803`). The corpse is animated-then-overwritten in a frame-order-dependent way | §3.3 | Same fix as G2: capture bits 1/2 on the 12 ragdoll bones at activation (releases at deactivation, mirroring prototype 60-70/110-114), *then* matrix write-back is safe; or write interp frames. |
| G8 | **Flat ground plane at ped-Z** (`716-726`), not terrain/buildings | `CWorld::FindGroundZForCoord` 0x569660 and `ProcessLineOfSight` 0x56BA00 available (§2.4) | Replace plane with per-bone `FindGroundZForCoord` at activation + `ProcessLineOfSight` sweep when speed is high (see also 2026-07-30 plan Task 6). Bullet-vs-world stays approximated: query game, push result into Bullet as a moving plane or apply positional correction — do **not** build full game-world collision meshes in Bullet (cost, §6 R3). |
| G9 | dt handling: `timeStep/50.0` with clamps (`1070-1074`), frame-gated by `renderFrame` (`966-972`) — good pattern, keep; but Bullet is stepped once *per instance loop iteration* (`1085-1087` inside the per-pool loop) — steps the **shared world N times** when N ragdolls are active | — | Step `g_bulletWorld` once per frame outside the instance loop (trivial fix in the unified module). |

**Keep:** the whole Bullet body/constraint setup (`551-714` — capsule dims, damping, cone/hinge limits from INI), hit-reaction mapping `ePedBone→body` (`765-801`), settle detection (`1113-1137`, `1215-1228`), INI config layer (`63-214`), death polling (`994-1055`), dismember triggers (`1026-1040`), `diag_registerScope` wiring (`372-378`).

### 4.4 Missing entirely (native-possible, currently absent)

| Gap | Native means | Where it lands |
|-----|--------------|----------------|
| Joint limits on the *native* integrator | `BoneNode_c::Limit` euler clamp + head coupling (§3.2) | Phase 1 |
| Collision response for the native integrator | game LOS/ground-Z (§2.4) applied as positional correction + velocity reflection on bone particles | Phase 1 |
| Blend-in/out lifecycle (capture → simulate → release → anim resume) | capture bits + `m_blend` slerp (prototype 87, 150-153) | Phase 1 |
| Flesh jiggle on living peds beyond bone sway | ShoulderBoneRotation idiom generalized: extra virtual bones (201/301/302 already exist) + optional per-vertex offsets via RpSkin read (§2.3) | Phase 2 (ped half) |
| Tree *cracking* (multi-segment break) | no native system; build an RwFrame/RpHAnim sub-hierarchy per tree at load (RpHAnim supports arbitrary node IDs — §2.1) and run the same particle chain as bones | Phase 2 (veg half) |
| Vehicle crumple (vertex level) | none natively; reuse dismember's bake-from-skin pipeline (`sftbdyphsx.cpp:747-954`) to make a mutable copy of the damage atomic | Phase 3 |

---

## 5. Architecture proposal — unified softbody module

### 5.1 Shape: one TU, TexDefs-style lifecycle

New files (future implementation — not part of this research task): `src/entities/softbody.{h,cpp}` absorbing `sftbdyphsx.cpp`'s ragdoll half, `ragdoll_death.cpp`, and the tree-softbody extension of `treeWind.cpp`; dismember stays in `sftbdyphsx.cpp` (it is mesh surgery, not simulation) but is driven by the same lifecycle. Lifecycle contract mirrors `src/texdefs.h:11-20` (attach/init/load/shutdown, idempotent, ordered against RW engine availability):

```cpp
int  SoftBody_PluginAttach(void);   // myPluginAttach chain — no RW state needed yet (reserved)
void SoftBody_Init(void);           // DllMain/InjectDelayedPatches: pools, INI, BoneManager.Init (cf. leak Init_overlay.cpp:917)
void SoftBody_LoadConfig(void);     // InitialiseGame_hook: ragdoll.ini
void SoftBody_Update(void);         // per game-frame: integrators, collision queries, Bullet step (ONCE)
void SoftBody_PreRender(void);      // per render-frame: BlendKeyframe / write-back, tree/vehicle apply
void SoftBody_Shutdown(void);       // DLL_PROCESS_DETACH: pools, Bullet world (cf. Game.cpp:809 order)
```

Frame split matches what already works: `Ragdoll_Simulate` at `main.cpp:5399` (post-Process, renderFrame-gated) becomes `Update`; `Ragdoll_ApplyPose` at `main.cpp:3913` becomes `PreRender`.

### 5.2 Core primitives (ported, not reinvented)

| Primitive | Ported from | Delta |
|-----------|-------------|-------|
| `SoftBone_c` | `BoneNode_c` (S2, all of it: Init/InitLimits/Limit/BlendKeyframe/CalcWldMat/Euler↔Quat) + `m_vel : RwV3d` (the field the prototype needs, Ragdoll.cpp:166-168) | Keep 0x98-compatible layout optional; ours can be freer since we don't patch the game's pool |
| `SoftBoneManager_c` | `BoneNodeManager_c`: 128-node pool + free list (S2 `BoneNodeManager_c.cpp:17-48`) | Ours is a static pool like today's `m_ragdolls[MAX_RAGDOLLS=24]` |
| `SoftRagdoll_c` | prototype `Ragdoll_c` completed: **all 32 bones** (not HEAD-only, Ragdoll.cpp:189), 3-axis friction, `Limit()` enabled, ground collision, `SetBlend` | Uses `SoftBone_c`; capture bits 1/2 (G1); BlendKeyframe write-back (G2/G7) |
| `SoftRagdollDeath_c` | today's `ragdoll_death.cpp` | Fixed: local-space write-back (G6), capture-then-write (G7), game collision (G8), single world step (G9). **Bullet stays as an optional backend** for death — it is genuinely better at constraint-solved collapse than the native velocity trick, and the INI tuning already exists |
| Integrator choice per phase | native = "velocity→axis→quat" (§3.1); Bullet = current | Both behind `SoftBody_Update`; INI `backend=native|bullet` for A/B |

### 5.3 Mode: ped flesh

* **Living:** `SoftRagdoll_c` at low blend (0.1-0.25) on nearby peds — exactly today's `ProcessAllPeds` intent (`sftbdyphsx.cpp:479-559`) but with the native write-back so it survives `UpdateRpHAnim`. Forces from acceleration/arm-sway stay (`ProcessMotion`, `333-379`); impulses from bullets via `ApplyForce` (prototype 160-169).
* **Wobble bones:** drive 201/301/302 with a spring toward the ShoulderBoneRotation rest pose (S2 `Ped.cpp:1207-1238`) — pure matrix surgery, zero skinning cost.
* **Optional vertex flesh (later):** per-vertex offset in the ped's skin morph target is *not* safe to mutate (shared geometry across instances of the model). Virtual-bone deformation only, unless we clone geometry per close ped (budget-gated, §6 R3).

### 5.4 Mode: tree bend/crack (hooks into `treeWind`)

* **Bend:** replace the absolute `at.x/at.y` kinematic lean (`treeWind.cpp:297-308`) with a 2-DOF angular spring-damper per tree instance (state map keyed by `RpAtomic*`, same seeding idiom as `treeWind_computeLean`, `135-170`); constants follow the shipped BouncingPanel damping family (§2.5) scaled by `CWeather__Wind`. The render-time WVP tilt path (`TreeWind_ApplyToTransform`, `190-252`) reads the spring state instead of recomputing lean. **Ownership rule preserved**: WIND-flagged atomics keep the shader path (`197-198`, `281-282`); the spring only feeds paths with no wind VS.
* **Crack:** at first fracture, split the tree's RwFrame hierarchy into trunk + canopy nodes (RpHAnim sub-hierarchy, §2.1) and hand them to the same `SoftBone_c` chain; trunk snaps when accumulated angle > threshold, canopy becomes a short-lived gib (reuse dismember gib pool, `GibInstance` at `sftbdyphsx.h:168-176`). SA tree models are single atomics — phase 2a ships bend only; crack (2b) is opt-in per model via INI model list.

### 5.5 Mode: vehicle softbody

* **Phase 3a — panels:** adopt `CBouncingPanel` semantics (§2.5) for doors/bonnet/boot where the game doesn't already run them (the game already runs its own `BouncingPanels` — leak `Automobile.cpp:332-347`; we extend, never double-run: query `GetCompIndex() == -1` before claiming a panel).
* **Phase 3b — crumple:** on `EventVehicleDamage`-equivalent hook, bake a mutable copy of the hit atomic via the dismember bake pipeline (§2.3), displace vertices along the damage normal with a gaussian falloff, recompute normals (already have the 0x7EDDC0/0x7ED9B0 calls, `sftbdyphsx.cpp:876-881`), and swap with `RpAtomicSetGeometry` (0x749D40, used at `944`). Static: one baked crumple per damage event, not a simulation — no per-frame cost.
* Damage-atomic interplay: if the model has `DamageAtomicModelInfo` swapped atomics (§2.5), crumple the *current* atomic and invalidate on swap.

### 5.6 Phasing

```
Phase 1 — ragdoll parity on RW primitives (this plan's core)
  1.0 Fix AnimBlendFrameData struct + capture bits (G1) — prerequisite for everything
  1.1 Port SoftBone_c from BoneNode_c (verbatim math incl. InitLimits/Limit head-coupling)
  1.2 Rewrite living overlay onto BlendKeyframe write-back; keep ProcessAllPeds trigger
  1.3 Death path: keep Bullet backend; fix G6/G7/G8/G9
  1.4 Native integrator behind INI flag; A/B vs Bullet on the same death impulse
  1.5 Blend-in/out + capture lifecycle; settle→release→anim resume
  Exit criteria: visual parity with current death ragdoll; living overlay no longer
  fights UpdateRpHAnim (verified by stepping frames in a debugger / matrix dumps)
Phase 2 — vegetation + ped flesh polish
  2a Tree angular spring feeding TreeWind_ApplyToTransform + matrix lean
  2b Wobble-bone springs (201/301/302) on nearby peds
  2c Tree crack (opt-in model list, gib canopy)
Phase 3 — vehicle
  3a BouncingPanel extension for unclaimed panels
  3b Baked crumple via dismember bake pipeline
```

---

## 6. Risk register

| ID | Risk | Impact | Likelihood | Mitigation |
|----|------|--------|-----------|------------|
| R1 | **SEH / crash in matrix surgery** — raw address calls + hand-built matrices (existing precedent: Dismember SEH around `AddParticle`, `sftbdyphsx.cpp:1182-1183`; `diag_registerScope` at `ragdoll_death.cpp:372-378`) | High | Med | Keep `__try/__except` only around *game* calls with proven instability; guard every `normalize`/quat-inverse with 1e-7 epsilon (project rule); NaN audit on BlendKeyframe output before it can reach skinning |
| R2 | **Struct-layout drift** — G1 proved our redeclarations can silently diverge from the shipped exe (`static_assert` on our own wrong struct passes!) | High | Med (already happened once) | Every layout redeclaration in the softbody TU gets an *address-derived* check: stride probe on a live ped's `m_FrameDatas` (boneTag sentinel walk) behind a startup dbglog; sizes asserted against S2, not against ourselves |
| R3 | **Per-frame cost** — living overlay on all peds <60 m (`RAGDOLL_DIST_HIGH`), Bullet world with N instances | Med | Med | Budget: ≤0.2 ms/frame for ≤4 active death ragdolls + ≤12 living overlays (32 bones × ~20 flops native path is trivial; cost is hierarchy walk + slerp cache ≈ 32×2 slerps). Distance culling already exists (keep 15/60 m tiers). Bullet: single step per frame (G9). Trees: spring state only for atomics already paying `ModifyMatrixForTreeInWind` |
| R4 | **SM3.0 shader implications** | Low | — | *None required*: all deformation is CPU-side bone/matrix work feeding the existing skinning path; the wind VS (VS c23 contract, `treeWind.cpp:3-12`) is untouched in phase 1-2a. Only phase 2c/3b touch geometry, and they reuse existing atomics/pipelines. No new cso, no register pressure change |
| R5 | **Hook conflict with shipped IK** — `g_boneNodeMan` (0xC10820) and `IKChainManager_c` are live in the exe (Game.cpp/Init_overlay.cpp active calls; `IKChain_c.cpp` consumes `GetBoneNode`) | Med | Low | We never patch `BoneNode_c`/manager — we port *code*, own *instances*. No InjectHook on 0x616B30-0x6177B0 range |
| R6 | **`ms_boneInfos` contents unverified** — we read 0x8D26D0 blind (`sftbdyphsx.cpp:403`); the InitLimits mixing formula (§3.2) assumes S2 correctness and the leak's older `parentBoneTag` naming differs | Med | Med | Startup dump of all 32 entries (dbglog, one-shot) cross-checked against S2's table semantics; if `m_ABC` reads as garbage, fall back to 2026-07-30 plan Task 3's hard-coded table |
| R7 | **Double-transform / corpse glitch** (G6) during migration — changing write-back semantics mid-flight | Med | High (transition) | Feature-flag `writeback=world|local|blendkeyframe`; default local after parity testing on turning/teleported corpses |
| R8 | **Capture-bit side effects** — freezing frames 1/2 on a ped that gets revived/scripted | Med | Low | Capture only on death activation; release in `Deactivate` (prototype Exit pattern, Ragdoll.cpp:104-118); never capture living peds (today's decision, `sftbdyphsx.cpp:187` — keep until phase 2 flesh needs it) |
| R9 | **SA_PC leak false hope** — S4 is empty; no hidden PC-final Ragdoll source to consult | Info | Confirmed | Accept S1+S2 as the complete native picture; S1's commented sections (Limit calls, all-bone loop) are *design intent*, not shipped behavior — treat S2 as authority whenever they disagree |

---

## 7. Citation index

**Leak (S1):** `Ragdoll.cpp:54-97` Init/capture; `:125-143` Update; `:160-169` ApplyForce; `:176-248` ResolveSystem (gravity `:183`, HEAD-only `:189-192`, axis/cross `:194-203`, parent-space quat `:208-226`, friction `:244-247`); `:255-273` SetupBoneHierarchy via `ms_boneInfos[i].parentBoneTag`; `:323-410` manager pool; `Game.cpp:569-570, 742-745, 809-810, 1252-1255`; `Init_overlay.cpp:917-922`; `Main.cpp:296, 3461-3466`; `Ped.cpp:8669-8722` ShoulderBoneRotation; `Automobile.cpp:332-347` BouncingPanels setup.

**gta-reversed (S2):** `BoneNode_c.cpp:7-29` address table; `:32-45` Init; `:48-60` InitLimits; `:63-97` Euler↔Quat; `:100-107` GetIdFromBoneTag; `:156-178` Limit (+head coupling); `:181-187` BlendKeyframe; `:218-232` CalcWldMat. `BoneNode_c.h:56-66` fields, size 0x98. `BoneNodeManager_c.h:12` `ms_boneInfos@0x8D26D0`; `:15-17` 128-pool; `:32` `g_boneNodeMan@0xC10820`. `tBoneInfo.h:6-13`. `Enums\eBoneTag.h:11-55`. `Animation\AnimBlendFrameData.h:12-39` (0x18, bits). `Animation\AnimBlendClumpData.h:14-56` (0x14). `BouncingPanel.h:5-31`. `Entity\Ped\Ped.cpp:1207-1238` ShoulderBoneRotation (0x5DF560). `World.cpp:51,72` LOS 0x56BA00 / GroundZ 0x569660.

**RW SDK (S3):** `RWSDK37\...\d3d9\rphanim.h:111-117` RpHAnimKeyFrame; `:132-140` InterpFrame; `:182-195` hierarchy flags; `:217-232` hierarchy struct; `:263-279` KeyFrameToMatrixMacro; `:393-407` matrix-array API; `:416-439` keyframe ops. `rpskin.h` (via `sftbdyphsx.cpp:578-587` address usage). `rpcollis.h:527-959`. Header inventory: no physics plugin.

**Ours (S5):** `src/entities/sftbdyphsx.h:22-23,36-46,56-151`; `src/entities/sftbdyphsx.cpp:34-66,78-168,180-205,212-315,333-397,399-419,479-561,563-624,642-661,747-954,959-1040,1045-1184`. `src/extras/ragdoll_death.cpp:63-214,220-309,346-382,411-873,879-946,963-1143,1150-1228`. `src/render/treeWind.cpp:135-170,190-252,257-330`. `src/core/main.cpp:3913,5396-5399,7705,7805`. `src/texdefs.h:11-20`. `docs/plans/2026-07-30-ragdoll-restoration.md` (superseded by §5.6). `docs/plans/2026-10-26-cj-ring-investigation.md` (bone-tag enum provenance).
