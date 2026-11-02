# RAGE Ragdoll Study + Bullet Backend Plan — Finishing `softbody.cpp`

**Date:** 2026-10-29
**Task:** m0963 — study how RAGE (GTA V) implements ragdoll, then finish our softbody
implementation by adding **Bullet** as a second backend, patching Rockstar's leftover
game logic (bone tables, pools, death polling) around it.
**Predecessor (SA side):** `docs/plans/2026-10-29-rw-physics-ragdoll-re.md` — READ FIRST;
this doc does **not** repeat the SA/RW analysis, it cross-references it as [RE-doc §x].
**Mode:** READ-ONLY research; one new file (this document).

---

## 0. TL;DR

* **RAGE's ragdoll is a constraint-solved articulated body, not a velocity integrator.**
  Bone chain = `phArticulatedBody` (1DOF/3DOF joints with *muscle* strength/targets and
  drive states) hung off a `fragInstNMGta` instance; the "physics vs animation" question
  is answered by **instance ownership** (ped switches its active physics inst to the
  ragdoll inst), not by slerping keyframes.
* **RAGE itself is built on Bullet code.** The leaked core physics module
  (`rage/base/src/physics/`) contains Bullet-derived broadphase files with Erwin Coumans'
  copyright (`btAxisSweep3.h:1-2`) plus a full impulse constraint-solver family
  (spherical/hinge/prismatic/rotation/distance constraints, `constraint*.cpp`). Using
  Bullet in our plugin is the *most authentic* free analogue of RAGE's own stack.
* **We are already 80% there.** Bullet 2.87 is vendored (`third_party/bullet/src`,
  115 .cpp compiled into the DLL by `premake5.lua:282`), the death ragdoll
  (`src/extras/ragdoll_death.cpp`) and dismember gibs already run on it, and the P1
  native integrator just landed in `src/entities/softbody.cpp` (INI `softbodyNative`,
  default 0, lifecycle wired at `main.cpp:3918/5408/7760/8500`).
* **What's left:** (a) unify the two Bullet users behind one world owned by
  `softbody.cpp`; (b) add `softbodyBackend = off|native|bullet`; (c) port the RAGE-side
  ideas that cost little and pay big — ragdoll pool with priority eviction, 3-tier LOD,
  single-frame world step, local-space write-back, stiffness ramping, hit-impulse
  attenuation; (d) fix compliance (no Bullet LICENSE file vendored, no DEPENDENCIES.md entry).

---

## 1. How RAGE does ragdoll (GTA V source tree)

Tree: `E:\dev(dave)\releases\GTASource\rage\GTAV Source\src\dev_ng\` — `game/` (gameplay),
`rage/base/src/physics/` (engine), `rage/naturalmotion/` (Euphoria/NM, binary-only).

### 1.1 The bone body: articulated constraint graph (not free capsules)

* The ped's ragdoll is a **fragment instance**: `class fragInstNMGta : fragInstNM`
  (`game/physics/gtaInst.h:265-266`), the ped-specific `fragInst` type
  (`gtaInst.h:265` comment). Ragdoll tick entry: `ProcessRagdoll(float fTimeStep)`
  (`gtaInst.h:337`). Ejection entry: `SetRagdollEjectedFromVehicle(CVehicle*)`
  (`gtaInst.h:377`). Stiffness ramps: `ProcessTwoStageRamp(eRampType, eBodyPartGroup,
  startStiff, midStiff, …)` (`gtaInst.h:394`). "Time since last bullet hit this rage
  ragdoll" is a first-class field (`gtaInst.h:417`).
* The actual bodies live behind `phArticulatedCollider::GetBody() → phArticulatedBody`.
  Joints are typed `phJoint::JNT_1DOF` / `JNT_3DOF` and carry **muscle** state:
  `SetMuscleTargetAngle/Speed`, `Get/SetMuscleAngleStrength`, `SetDriveState
  (phJoint::DRIVE_STATE_FREE)` — see the joint walk in
  `task/Physics/TaskRageRagdoll.cpp:328-388` (`ProcessJointRelaxation`). This is an
  **Featherstone-style articulated chain with driven joints**: joints can be *driven*
  toward muscle targets (active pose maintenance) or freed (limp collapse).
* Global stiffness knob set on ragdoll entry: human `0.5`, animal `0.25`, water `0.1`
  (`TaskRageRagdoll.cpp:199-203`).
* **Missing from the leak:** the `phArticulatedBody` / `phJoint` class *declarations*
  (rage framework headers not present — only call sites leak) and the Euphoria/NM runtime
  (`rage/naturalmotion/lib/` contains only `artrockstar.idb` debug info). The
  skeleton→joint builder (ART asset → ph joints) is not inspectable. Everything above is
  from game-side call sites.

### 1.2 Core engine = Bullet-derived

`rage/base/src/physics/` ships Bullet-derived code verbatim: `btAxisSweep3.h:1-2`
("Bullet Continuous Collision Detection … Copyright (c) 2003-2006 Erwin Coumans"),
`btAxisSweep1`, `btNxN`, `btImmediateModeBroadphase`, `btOverlappingPairCache`,
`btSpatialHash`, `btBroadphaseProxy.h` — all re-namespaced into `rage::`. Constraint
family: `constraint{fixed,spherical,hinge,prismatic,rotation,distance,cylindrical,
halfspace,fixedrotation}.cpp` + `constraintmgr`, `contact`, `manifold`,
`simulator.{h,cpp}` (`phSimulator` at `simulator.h:133` orchestrates levels, contact
and constraint tasks). **Conclusion: RAGE's physics is an evolved Bullet 1.x/2.x —
Bullet 2.87 in our tree is the correct free stand-in, and our `ragdoll_death.cpp`
cone-twist/hinge setup is already structurally RAGE-like.**

### 1.3 Task layer: FSM, pools, LOD, timeout

* `CTaskRageRagdoll` is the **non-Euphoria** ragdoll ("Task to handle non-natural motion
  ragdolls", `task/Physics/TaskRageRagdoll.h:2`); Euphoria behaviours are the sibling
  `CTaskNM*` family (`TaskNMBehaviour : CTaskFSMClone`, `TaskNM.h:458`). FSM:
  `State_Ragdoll → State_AnimatedFallback → State_Finish` (`TaskRageRagdoll.h:100-105`).
* Entry (`Ragdoll_OnEnter`, `TaskRageRagdoll.cpp:149-226`): `pPed->SwitchToRagdoll(...)`,
  `SetBlockNMActivation` bracketing, `SwitchFromNMToRageRagdoll(...)`, add to ragdoll
  pool, extra allowed penetration on the collider, default corpse friction.
* Update (`Ragdoll_OnUpdate`, `:229-326`): **3-tier LOD accounting**
  (`RAGDOLL_LOD_HIGH/MEDIUM/LOW`, `:234-255`), writhe (animals), joint-stiffness ramp
  (MP), `ProcessHitByVehicle`, `CorrectUnstablePoses`, drive-state free after drive
  timer, **3.5 s default max runtime** then hand back to NM control (`:293-305`).
* **Budget = priority-scored pools.** `CTaskNMBehaviour::CRagdollPool`
  (`TaskNM.h:775-801`): 3 pools (Gameplay / RageRagdoll / LocalPlayer,
  `TaskNM.h:764-772`), `GetMaxRagdolls`/`GetFreeSlots`; on overflow the *lowest-scoring*
  active ragdoll is aborted to admit a higher-priority one
  (`CalcRagdollScore/EventScore/MultiplierScore`, `TaskNM.h:803-826`).

### 1.4 Hit reactions & vehicle ejection

* Per-shot impulse housekeeping tunables: `m_fImpulseReductionPerShot`,
  `m_fImpulseRecoveryPerSecond`, `m_fMax/MinImpulseModifier`, `m_fCounterImpulseRatio`,
  `m_fTempInitialStiffnessWhenShot`, initial-hit multiplier
  (`TaskRageRagdoll.h:56-71`) — rapid fire on a corpse is deliberately de-ganged.
* Vehicle interactions: `ProcessHitByVehicle(pPed)` per frame
  (`TaskRageRagdoll.cpp:270`); ejection behaviours are dedicated NM tasks:
  `CTaskNMThroughWindscreen` (`TaskNMThroughWindscreen.h:59+` — tunables for gravity
  scale, bike knock-off forward/up components, min/max speeds, eject impact depth and
  friction; `HandleRagdollImpact`; `ShouldContinueAfterDeath()`), and
  `CTaskNMJumpRollFromRoadVehicle`. Ejected state stamped via
  `fragInstNMGta::SetRagdollEjectedFromVehicle` (`gtaInst.h:377`).
* Per-vehicle-model activation tuning: `CVehicleModelInfoRagdollActivation`
  (`modelinfo/VehicleModelInfoExtensions.h:120`).

### 1.5 Blend with animation

* **Ownership swap, not slerp-into-keyframe.** `CPed::GetUsingRagdoll()` is true iff the
  ped's current physics inst *is* the ragdoll inst (`Peds/ped.h:1017`); while active the
  ragdoll inst owns the skeleton, and animation is blocked/filtered per bone
  (`animation/MovePed.h:59 RagdollFilter : crFrameFilterBoneBasic`,
  `animation/EventTags.h:373 CBlockRagdollTag`).
* `CPed::UpdateRagdollMatrix(bool bWarp)` (`Peds/Ped.cpp:4259-4297`) is the
  **placement hand-off** (entity matrix → ragdoll inst matrix while *inactive*), guarded
  by asserts against being called on an active ragdoll — the physics→skeleton per-bone
  write lives inside the binary NM/ART layer (not inspectable).
* Blend-out to animation: `CTaskBlendFromNM` + `BlendFromNmData` with named blend-out
  pose sets (`NMBS_STANDING`, `NMBS_STANDING_AIMING_RIFLE_HURT`, …,
  `task/Physics/BlendFromNmData.h:44-60`) — pose *matching* against the live ragdoll.
* Constraint extras while ragdolled: `CRagdollConstraintComponent`
  (`physics/RagdollConstraints.h:14-51`) — handcuffs / bound ankles / meathook-to-world
  via `phConstraintHandle`s inserted into the same constraint system
  (`InsertEnabledConstraints`, `RemoveRagdollConstraints`).
* Cloth is a **separate** particle system (`game/cloth/`: `ClothParticles`,
  `ClothConstraintGraph`, `ClothSprings`, `ClothUpdater`) — vertex-level, not bone-level.
  Not needed for our phases 1-3; noted so nobody conflates the two.

---

## 2. SA shipped vs RAGE (delta summary — SA detail lives in [RE-doc])

| Axis | SA (shipped/cut, [RE-doc §3]) | RAGE (V) | Takeaway for us |
|---|---|---|---|
| Solver | Cut velocity→axis→quat integrator (`Ragdoll.cpp` ResolveSystem); shipped BoneNode euler clamps only | Constraint-solved `phArticulatedBody`, driven joints with muscle targets | Bullet backend ≈ RAGE solver; native backend ≈ SA prototype. Keep both behind one INI |
| Anim hand-off | `BlendKeyframe` slerp into `AnimBlendFrameData` interp frame (bits 1/2) | Inst-ownership swap + per-bone anim filter + pose-matched blend-out | SA idiom is the only one feasible for us (we cannot swap the game's phInst); death path can *emulate ownership* via capture bits — [RE-doc G7] |
| Budget | none (24 static slots, distance tiers only) | 3 priority pools + overflow eviction + 3-tier LOD + 3.5 s timeout | Port the *idea*: score + evict + LOD + settle timeout (we already detect settle: `ragdoll_death.cpp:1113-1137`) |
| Hit reaction | raw impulse into `m_vel` / Bullet body | impulse de-ganging per shot, counter-impulse, temp stiffness, writhe ramp | Cheap win: port `ImpulseReductionPerShot`/recovery family into INI-tuned damping on repeated hits |
| Vehicle | none (flat plane, [RE-doc G8]) | windscreen/bike-eject tasks, per-model activation, `SetRagdollEjectedFromVehicle` | Phase 4+: trigger-only (spawn eject impulse at crash) — full NM behaviours out of scope |
| Ground | flat plane at ped-Z | full world collision in phSimulator | keep [RE-doc G8] mitigation: game LOS/ground-Z queries feeding Bullet planes |

---

## 3. Bullet integration plan for `src/entities/softbody.cpp`

### 3.1 Backend ownership split

| Concern | Owner after this plan |
|---|---|
| Lifecycle (Init/Update/PreRender/Shutdown/LoadConfig) | `softbody.cpp` (already wired: `main.cpp:3918/5408/7760/8500`) |
| Native integrator (P1: velocity→parent-quat→BlendKeyframe) | `softbody.cpp` — **stays the `native` backend**, unchanged behavior |
| Shared Bullet world (`btDiscreteDynamicsWorld`) | **moves to `softbody.cpp`** (today it lives in `ragdoll_death.cpp:52` as `g_bulletWorld`, stepped *inside* the per-instance loop — [RE-doc G9]) |
| Death-ragdoll instance mgmt (11 capsules, cone/hinge, settle, hit map) | stays `ragdoll_death.cpp` as the **client** of the softbody-owned world (absorb later, not in this phase) |
| Gibs (dismember) | stays `sftbdyphsx.cpp`, keeps using the same world via one accessor |

Rule: **one world, one step per game frame, in `SoftbodyUpdate()`**, before instance
clients read transforms. `Ragdoll_Simulate` (hook `main.cpp:5399→5408`) keeps calling
instance logic but no longer calls `stepSimulation`.

### 3.2 INI design

```ini
[SkyGfx]
softbodyBackend = off | native | bullet   ; default off
softbodyNative  = 0|1                     ; legacy: 1 => backend=native if backend unset
softbodyMaxRagdolls    = 4                ; Bullet death-ragdoll budget (RAGE pool idea)
softbodyMaxOverlays    = 12               ; native living-overlay budget
softbodyStepHz         = 60               ; fixed step; stepSimulation(dt,2,1/60) kept
softbodyHitAttenuation = 1                ; per-shot impulse reduction factor (RAGE §1.4)
softbodyTimeoutSec     = 3.5              ; settle→release timeout (RAGE §1.3)
```

`softbody.cpp` reads once at `SoftbodyInit()` (same `GetPrivateProfileIntA` self-read
precedent as today, `softbody.cpp:539-559`; extend to a small string read for backend).
`softbodyNative=1` keeps working (maps to `native`). Backend switch is **activation-time**:
existing instances finish on their backend; no mid-flight migration (avoids [RE-doc R7]).

### 3.3 Minimal Bullet class set (already proven in-tree)

World (singleton, new home in `softbody.cpp`): `btDefaultCollisionConfiguration`,
`btCollisionDispatcher`, `btDbvtBroadphase`, `btSequentialImpulseConstraintSolver`,
`btDiscreteDynamicsWorld` — exactly the set `ragdoll_death.cpp:8-17` already uses.
Per death-ragdoll: `btCapsuleShape` ×11, `btRigidBody` ×11,
`btConeTwistConstraint` + `btHingeConstraint` (INI-tuned spans, `ragdoll_death.cpp:98-140`),
`btStaticPlaneShape` for ground (upgraded per [RE-doc G8] to game-queried planes).
Per gib: `btBoxShape`/`btCapsuleShape` + `btRigidBody` (dismember).
Optional experiment (not committed): `btPoint2PointConstraint` chain for a cheap
12-body "SA-prototype-like" mode; P2P lacks angular limits — only useful for soft jiggle,
not death collapse.

**Not used:** btSoftBody (heavy, unnecessary — our softbody name means "soft *character*",
and flesh is bone-driven per [RE-doc §5.3]), btMultiBody (overkill; articulated body
would be RAGE-authentic but Bullet 2.87's btMultiBody API is fussy and our capsule
constraints already match the current tuned look).

### 3.4 Ship form & Win32 constraints

* **Static-compiled into the DLL** (status quo): `premake5.lua:282`
  `files { "third_party/bullet/src/**.cpp" }` + include `third_party/bullet/src`
  (`:314`). 115 .cpp, MSVC v143, Win32 — already building and shipping. No header-only
  subset (Bullet is not header-only; a trimmed static build is a later binary-size task,
  see risk B1).
* No DLL redistribution, no delay-load, no extra deploy target — Bullet dies with our ASI.
* Keep `__try/__except` discipline: Bullet calls are ours (stable); SEH stays wrapped
  only around *game* calls ([RE-doc R1]).

### 3.5 Rockstar leftover logic patched *around* Bullet (the "free" part)

These are the game-intel hooks Bullet cannot know, already half-present in our code:

| Game logic | Source of truth | Where it plugs in |
|---|---|---|
| Bone tags + parent links + euler limits | `ms_boneInfos` @ 0x8D26D0, 32 tags ([RE-doc §3.2], `sftbdyphsx.cpp:403`) | map `ePedBone`→Bullet body (exists: `ragdoll_death.cpp:765-801`); limits from table where cone spans unset |
| Ped pool walk | `CPool` @ 0xB74490, stride 0x7C4 (`sftbdyphsx.cpp:479-559`) | overlay assignment + death poll in one walk (unify: today two walks) |
| Death state poll | `CPed+0x530 == {65,66}` (`ragdoll_death.cpp:1006-1007`) | activation trigger for Bullet backend |
| Hit source | `m_nBodypartToRemove` @ CPed+0x754, `m_vecMoveSpeed` (`ragdoll_death.cpp:738-852`) | impulse entry — add RAGE-style per-shot attenuation (§3.2) |
| Capture/release bits 1/2 | fixed `AnimBlendFrameData` 0x18 layout ([RE-doc G1]) | on activation capture 12 bones; on settle/timeout release → anim resume |
| Ground/building query | `FindGroundZForCoord` 0x569660 / `ProcessLineOfSight` 0x56BA00 ([RE-doc §2.4]) | per-bone plane refresh for Bullet ground (fix G8) |
| Write-back | `BlendKeyframe` idiom ([RE-doc G2]) | Bullet world transform → **local** bone quat → interp frame (fix G6: `inv(parent)*child`) |

### 3.6 RAGE ideas worth porting (cheap → expensive)

1. **Single world step + settle timeout → release** (timeout INI, §3.2). *Trivial.*
2. **Ragdoll budget with score eviction** (`softbodyMaxRagdolls`; score = hit recency +
   distance + player proximity — mirrors `CalcRagdollScore`, `TaskNM.h:826`). *Small.*
3. **3-tier LOD**: HIGH = full Bullet; MED = 30 Hz step, fewer constraints (neck free);
   LOW = frozen last pose / disable collision ([RE-doc §1.3 LOD + our `SetUsingLowLodPhysics`
   precedent `Ped.cpp:4299`). *Medium.*
4. **Hit impulse de-ganging** (§1.4 tunables → our INI). *Small.*
5. **Stiffness ramp on death** (two-stage ramp idiom, `gtaInst.h:394`): joints start
   stiff (retain pose) then relax — maps to gradually reducing cone/hinge motor strength
   over `softbodyTimeoutSec`. *Medium — biggest visual-quality win.*
6. Vehicle-eject *trigger* only (phase 4): on crash, impulse from vehicle velocity at
   ped position; no NM behaviours. *Later.*

---

## 4. License / compliance

* **Bullet:** zlib license (Copyright © 2003-2007 Erwin Coumans et al.). Permits
  commercial closed-source use, modification, redistribution in binary form; requires
  the copyright notice in source/binary distributions. **Gaps today:**
  `third_party/bullet/` vendors only `src/` + `reference/` — **no LICENSE.txt/COPYING
  file**; `DEPENDENCIES.md` has **no Bullet entry**. Actions: (1) drop Bullet 2.87's
  `LICENSE.txt` into `third_party/bullet/`; (2) add DEPENDENCIES.md section; (3) ship the
  notice in the release zip (`installer.py`/`release` staging). Do not strip headers.
* **GTA V RAGE source:** leaked proprietary Rockstar code. **Reference-only.** We may
  *learn* architecture (articulated joints, pools, tunables structure) and write our own
  clean-room equivalents; we may **not** copy code, constants tables, or asset formats
  into our tree. Our code must not link anything from `naturalmotion/` or rage libs.
* **SA leaked source (`gtasa_src-main`):** same rule — reference-only. Our existing plan
  already ports *formulas/idioms* (InitLimits mixing, BlendKeyframe) written in our own
  code against public RW SDK + game addresses; that precedent stands. Do not paste
  Rockstar source lines into shipping .cpp files.
* **Our tree:** `ragdoll_death.cpp`/`softbody.cpp`/`dismember` are original work using
  public Bullet APIs + game addresses — keep it that way; comments may *cite* file:line
  of the leaks for provenance (we already do), never embed their code.

---

## 5. Phased plan, integration points, risks

### 5.1 Integration points (exact)

| Site | Today | Change |
|---|---|---|
| `main.cpp:7760` `SoftbodyInit()` | reads `softbodyNative`, pool zero | + backend resolve, Bullet world create (if backend=bullet), LoadConfig |
| `main.cpp:5408` `SoftbodyUpdate()` | native overlay integrator | + **single** `stepSimulation` (moved from `ragdoll_death.cpp:1086`), then death-instance update, settle→release |
| `main.cpp:3918` `SoftbodyPreRender()` | BlendKeyframe write-back | + death write-back in **local space** (fix G6) via capture bits |
| `main.cpp:8500` `SoftbodyShutdown()` | overlay release | + world teardown, remove all bodies/constraints before delete |
| `main.cpp:3913/5399/7747` `Ragdoll_*` slots | death-ragdoll entry points | become thin forwards into softbody lane (keep names for hook stability) |
| `ragdoll_death.cpp:1086` | `stepSimulation` per instance | **delete** — world stepped by softbody (fix G9) |

### 5.2 Phases & effort

```
P-a  Unify Bullet ownership + backend INI + move step + LICENSE/DEPENDENCIES   ~0.5 day
     Exit: softbodyBackend=bullet behaves identically to today's death ragdoll;
     =off / native unchanged; one stepSimulation per frame (log-verified).
P-b  Fixes G6/G7/G8 (local write-back, capture/release lifecycle, game-Z planes) ~1-1.5 days
     Exit: corpse on turning/teleported ped no longer double-transforms; anim
     resumes after release without pop (matrix-dump check per [RE-doc 1.5 exit]).
P-c  RAGE-lite polish: budget+eviction, hit attenuation, settle timeout, 2-stage
     stiffness ramp, LOD tiers (INI-gated, defaults = current behavior)          ~1-1.5 days
     Exit: 6 simultaneous deaths stay within budget; rapid-fire no longer
     explodes corpses; timeout release matches current settle detect visually.
P-d  (optional, later) absorb ragdoll_death.cpp into softbody.cpp as
     SoftRagdollDeath_c per [RE-doc §5.2; 0.5-1 day
P-e  (phase 4 idea spike, separate task) vehicle-eject impulse trigger          ~0.5 day
```

Total for P-a..P-c: **~3-4.5 focused days.** Each phase is INI-revertible.

### 5.3 Risk register (delta to [RE-doc §6])

| ID | Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| B1 | Binary size / build time: 115 Bullet .cpp already cost ~; trimming (exclude btSoftBody/GIMPACT/etc.) regresses builds | Med | Med | Defer trimming; if done, glob-exclude list in premake with a build-count assert (115 today, `premake5.lua:16`) |
| B2 | Win32/v143 warnings-as-errors on Bullet TUs | Med | Low | already green; pin: don't upgrade Bullet beyond 2.87 (3.x API break) |
| B3 | Double-write race between BlendKeyframe (PreRender) and death matrix poke while migrating write-back semantics | Med | High (transition) | feature flag `writeback=world|local|blendkeyframe` ([RE-doc R7]); default local after parity |
| B4 | Single-step move changes timing (instances used to step N×/frame) → corpses fall slower/faster | Med | Med | step with accumulated dt at fixed 1/60 substeps (`maxSubSteps=2` kept); A/B video at 4 ragdolls |
| B5 | Capture bits on revived/scripted peds ([RE-doc R8]) | Med | Low | capture only at death activation; release in settle/timeout/Shutdown; never on living overlays |
| B6 | Per-frame budget regression with Bullet+overlays both on | Med | Med | keep ≤0.2 ms budget ([RE-doc R3]); budget guard: if frame time > budget, drop LOD tier (§3.6-3) before dropping instances |
| B7 | Compliance miss (LICENSE absent) | High (legal) | Certain today | P-a mandatory: LICENSE.txt + DEPENDENCIES.md + release staging |
| B8 | SEH surface: more Bullet objects alive during game-call write-backs | Med | Low | SEH stays only around game calls; NaN guard on every quat→matrix before skinning ([RE-doc R1]) |

### 5.4 Explicit non-goals

* No Euphoria/NM reimplementation (muscle behaviors beyond stiffness ramp).
* No btSoftBody vertex flesh (virtual bones only, [RE-doc §5.3]).
* No Bullet world-collision meshes of the game world (game queries feed planes).
* No copying from either leak tree — clean-room structure only (§4).

---

## 6. Citation index (this doc)

**RAGE V tree** (`E:\dev(dave)\releases\GTASource\rage\GTAV Source\src\dev_ng\`):
`game/physics/gtaInst.h:219-225,265-266,337,377,394,417` (fragInstNMGta);
`game/physics/RagdollConstraints.h:14-51`; `game/task/Physics/TaskRageRagdoll.h:2,56-71,100-105`;
`game/task/Physics/TaskRageRagdoll.cpp:149-226,229-326,328-388`;
`game/task/Physics/TaskNM.h:458,764-772,775-826`;
`game/task/Physics/TaskNMThroughWindscreen.h:59-120`;
`game/task/Physics/BlendFromNmData.h:44-60`; `game/animation/MovePed.h:59`;
`game/animation/EventTags.h:373`; `game/Peds/ped.h:622,1003-1017`;
`game/Peds/Ped.cpp:3771-3816,4259-4297,4299-4348`;
`game/modelinfo/VehicleModelInfoExtensions.h:120`;
`rage/base/src/physics/btAxisSweep3.h:1-2` + constraint*/simulator files §1.2;
`rage/naturalmotion/lib/` = binary-only (idb). Missing: phArticulatedBody/phJoint decls,
ART joint builder, physics→skeleton write (inside NM runtime).

**Ours:** `src/entities/softbody.{h,cpp}` (lifecycle `softbody.h:30-38`,
INI `softbody.cpp:539-559`, functions `:564/:571/:633/:640`);
`src/extras/ragdoll_death.{h,cpp}` (`g_bulletWorld` :52, joints :98-140, step :1086,
death poll :1006, hit map :765-801, settle :1113-1137); `src/entities/sftbdyphsx.cpp`
(pool walk :479-559, ms_boneInfos :403); `src/core/main.cpp:3913,3918,5399,5408,7747,7760,8500`;
`premake5.lua:16,282,314`; `third_party/bullet/src/LinearMath/btScalar.h:28` (BT_BULLET_VERSION 287).
**SA/RW detail:** see [RE-doc] sections as cited inline.
