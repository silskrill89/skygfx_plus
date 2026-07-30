# Ragdoll Physics Restoration — Implementation Plan

> **Goal:** Restore GTA SA's cut ragdoll physics system using bone-node-based simulation via the RW SDK, without RAGE/NaturalMotion dependencies.

> **Architecture:** Port the commented-out Ragdoll.cpp prototype from the original SA source, extended with full-bone processing, joint limits, ground collision, and proper friction. Build on top of the already-shipping BoneNode_c and IKChain_c systems (confirmed active in game exe).

> **Tech Stack:** C++, RW SDK 3.6 (baked into game exe), RpHAnim bone hierarchy, RtQuat quaternion math, WRAPPER/EAXJMP for game function calls.

---

## Component Inventory

### AVAILABLE (OpenSA reconstruction — complete)

| Component | File | Lines | Notes |
|-----------|------|-------|-------|
| `BoneNode_c.h/.cpp` | OpenSA + gta-reversed | 65+232 | Full class, VALIDATE_SIZE(0x98) |
| `BoneNodeManager_c.h/.cpp` | OpenSA + gta-reversed | 33+48 | Pool of 128 BoneNode_c |
| `IKChain_c.h/.cpp` | OpenSA + gta-reversed | 71+297 | Full IK solver |
| `IKChainManager_c.h/.cpp` | OpenSA + gta-reversed | 143+302 | LookAt/PointArm API |
| `tBoneInfo.h` | OpenSA | 13 | {m_current, m_prev, m_Max, m_Min, m_ABC} |
| `eBoneTag.h` | OpenSA | 59 | BONE_ROOT=0..BONE_R_TOE_0=54, MAX_BONE_NUM=32 |
| `Ragdoll.cpp` | gtasa_src-main | 435 | **Commented out** — full prototype |
| `RpHAnimBlendInterpFrame.h` | gta-reversed | 15 | {RtQuat q; CVector t} size 0x1C |

### MUST CREATE

| Component | Description | Complexity |
|-----------|-------------|------------|
| `Ragdoll.h` | Class declarations for Ragdoll_c and RagdollManager_c | Easy |
| `ms_boneInfos[32]` data | Hard-coded bone hierarchy parent links + rotation limits | Medium |
| `aBONETAG_ENUM_TAB[32]` | Index-to-boneTag lookup table | Easy |
| `NUM_RAGDOLLS` constant | Pool size (suggest 8) | Trivial |
| WRAPPER declarations | HAnim + Quat functions in gta.h/gta.cpp | Easy |
| `BoneNode_c` extensions | Add m_vel + m_keyFrameQuat fields | Medium |
| Ground collision | Raycast bone positions against world | Hard |
| Integration hooks | Init/Exit/Update in main.cpp | Easy |

### ALREADY SHIPPING (g_boneNodeMan + g_ikChainMan)

The game exe already initializes and runs BoneNodeManager_c and IKChainManager_c. Our ragdoll builds ON TOP of these — we don't replace them. The ragdoll layer captures bone keyframes, applies physics, and blends back.

---

## Skeleton Reference (GTA SA Ped)

```
Root (BONE_ROOT = 0)
  └─ Pelvis (BONE_PELVIS = 1)
       ├─ Spine (BONE_SPINE = 2)
       │    └─ Spine1 (BONE_SPINE1 = 3)
       │         └─ Neck (BONE_NECK = 4)
       │              └─ Head (BONE_HEAD = 5)
       │                   ├─ L_Brow (BONE_L_BROW = 6)
       │                   ├─ R_Brow (BONE_R_BROW = 7)
       │                   └─ Jaw (BONE_JAW = 8)
       ├─ R_Clavicle (21) → R_UpperArm (22) → R_ForeArm (23) → R_Hand (24) → R_Finger (25) → R_Finger_01 (26)
       ├─ L_Clavicle (31) → L_UpperArm (32) → L_ForeArm (33) → L_Hand (34) → L_Finger (35) → L_Finger_01 (36)
       ├─ L_Thigh (41) → L_Calf (42) → L_Foot (43) → L_Toe_0 (44)
       └─ R_Thigh (51) → R_Calf (52) → R_Foot (53) → R_Toe_0 (54)
```

Bones 201 (Belly), 301/302 (Breasts) are procedural wobble bones, NOT in the physics chain.

---

## Global Constraints

- **SM3.0 only** — All shaders compile as ps_3_0 / vs_3_0
- **RW 3.6 SDK** — Baked into game exe, use WRAPPER/EAXJMP for all RW calls
- **vcxproj hand-maintained** — Source of truth for build, not premake5.lua
- **`src/Core/` = `src/core/`** — Same directory on Windows (case-insensitive)
- **Build:** `python tools/fast_build.py` → MSBuild VS2022 → `E:\games\gtasa_skygfx_plus\skygfx.asi`
- **Debug log:** `E:\games\gtasa_skygfx_plus\skygfx_dbg.log`

---

## Implementation Tasks

### Task 1: Create Ragdoll.h — Class Declarations

**Files:**
- Create: `src/entities/ragdoll.h`

**What to write:**

```cpp
#pragma once

#include <rwcore.h>
#include <rpanim.h>
#include <rtquat.h>
#include "../rw/gta.h"

// Forward declarations
class CPed;
class BoneNode_c;

// Constants
#define NUM_RAGDOLLS 8

// Bone tag lookup table (index -> eBoneTag)
extern int32 aBONETAG_ENUM_TAB[];

class Ragdoll_c : public ListItem_c<Ragdoll_c> {
public:
    Ragdoll_c();
    ~Ragdoll_c();

    bool  Init(CPed* pPed);
    void  Exit();
    void  Update(float deltaTime);
    void  SetBlend(float blend);
    void  ApplyForce(int32 boneTag, float force, RwV3d dir);

private:
    void  ResolveSystem(float deltaTime);
    void  SetupBoneHierarchy();

#ifndef FINAL
    void  RenderDebug();
#endif

    CPed*       m_pPed;
    BoneNode_c  m_boneNodes[32]; // BONETAG_MAX_NUM
    BoneNode_c* m_pRootBoneNode;
    float       m_blend;
};

class RagdollManager_c {
public:
    RagdollManager_c();
    ~RagdollManager_c();

    bool       Init();
    void       Exit();
    void       Reset();
    void       Update(float deltaTime);
    Ragdoll_c* AddRagdoll(CPed* pPed);
    void       RemoveRagdoll(Ragdoll_c* pRagdoll);

#ifndef FINAL
    void       RenderDebug();
#endif

private:
    Ragdoll_c          m_ragdolls[NUM_RAGDOLLS];
    TList_c<Ragdoll_c> m_ragdollPool;
    TList_c<Ragdoll_c> m_ragdollList;
};

extern RagdollManager_c g_ragdollMan;
```

**Key decisions:**
- `m_boneNodes[32]` — fixed array, not dynamic. MAX_BONE_NUM=32 from eBoneTag.h
- `NUM_RAGDOLLS=8` — conservative pool size for GTA SA (max ~20 peds on screen, but only death triggers ragdoll)
- Uses `ListItem_c<T>` / `TList_c<T>` — same template pattern as OpenSA's BoneNodeManager_c and IKChainManager_c

**Test:** Compiles without errors when included from a test file.

---

### Task 2: Extend BoneNode_c with Ragdoll Fields

**Files:**
- Modify: Need to create `src/entities/ragdoll_bone.h` (extended BoneNode for ragdoll use)

**Why:** The OpenSA `BoneNode_c` (0x98 bytes) has `m_Orientation` (RtQuat) but NOT `m_vel` (velocity) or `m_keyFrameQuat` (separate keyframe quaternion). The original Ragdoll.cpp writes velocity for physics and uses a separate keyframe quat for parent rotation.

**Two approaches:**

**Option A (Recommended): Create RagdollBoneNode_c wrapper**
```cpp
// src/entities/ragdoll_bone.h
// Extends BoneNode_c concept for ragdoll physics
// Does NOT modify the existing BoneNode_c class

struct RagdollBoneData {
    RwV3d  m_vel;           // velocity for physics sim
    RtQuat m_keyFrameQuat;  // separate from orientation, for parent rotation
};

// Store alongside BoneNode_c in Ragdoll_c
// Ragdoll_c maintains: BoneNode_c m_boneNodes[32] + RagdollBoneData m_physicsData[32]
```

**Option B: Modify BoneNode_c directly**
Add `m_vel` and `m_keyFrameQuat` to BoneNode_c. This changes the class layout and could affect the shipping IKChain system.

**Recommendation: Option A** — zero risk to existing IK system, clean separation of concerns.

**Files:**
- Create: `src/entities/ragdoll_bone.h`

---

### Task 3: Create ms_boneInfos Data Table

**Files:**
- Create: `src/entities/bone_data.cpp`
- Create: `src/entities/bone_data.h`

**What:** Hard-code the `BoneNodeManager_c::ms_boneInfos[32]` array with the GTA SA skeleton hierarchy. Each entry:
```cpp
struct tBoneInfo {
    int32 m_current;  // this bone's tag
    int32 m_prev;     // parent bone's tag (-1 for root)
    CVector m_Max;    // max rotation limits (degrees)
    CVector m_Min;    // min rotation limits (degrees)
    CVector m_ABC;    // limit parameters
};
```

**Source:** Reverse-engineered from game exe at 0x8D26D0, cross-referenced with OpenSA's eBoneTag.h and the ped skeleton structure documented in chars.cpp.

**Key parent links:**
```
BONE_ROOT(0)      → parent: -1 (no parent)
BONE_PELVIS(1)    → parent: BONE_ROOT
BONE_SPINE(2)     → parent: BONE_PELVIS
BONE_SPINE1(3)    → parent: BONE_SPINE
BONE_NECK(4)      → parent: BONE_SPINE1
BONE_HEAD(5)      → parent: BONE_NECK
BONE_L_BROW(6)    → parent: BONE_HEAD
BONE_R_BROW(7)    → parent: BONE_HEAD
BONE_JAW(8)       → parent: BONE_HEAD
BONE_R_CLAVICLE(21)  → parent: BONE_SPINE1
BONE_R_UPPER_ARM(22) → parent: BONE_R_CLAVICLE
BONE_R_FORE_ARM(23)  → parent: BONE_R_UPPER_ARM
BONE_R_HAND(24)      → parent: BONE_R_FORE_ARM
BONE_R_FINGER(25)    → parent: BONE_R_HAND
BONE_R_FINGER_01(26) → parent: BONE_R_FINGER
BONE_L_CLAVICLE(31)  → parent: BONE_SPINE1
BONE_L_UPPER_ARM(32) → parent: BONE_L_CLAVICLE
BONE_L_FORE_ARM(33)  → parent: BONE_L_UPPER_ARM
BONE_L_HAND(34)      → parent: BONE_L_FORE_ARM
BONE_L_FINGER(35)    → parent: BONE_L_HAND
BONE_L_FINGER_01(36) → parent: BONE_L_FINGER
BONE_L_THIGH(41)     → parent: BONE_PELVIS
BONE_L_CALF(42)      → parent: BONE_L_THIGH
BONE_L_FOOT(43)      → parent: BONE_L_CALF
BONE_L_TOE_0(44)     → parent: BONE_L_FOOT
BONE_R_THIGH(51)     → parent: BONE_PELVIS
BONE_R_CALF(52)      → parent: BONE_R_THIGH
BONE_R_FOOT(53)      → parent: BONE_R_CALF
BONE_R_TOE_0(54)     → parent: BONE_R_FOOT
```

**Also hard-code `aBONETAG_ENUM_TAB[32]`:**
Maps index 0-31 to bone tags. Order follows the iteration pattern in Ragdoll_c::Init.

---

### Task 4: Add WRAPPER Declarations for HAnim/Quat Functions

**Files:**
- Modify: `src/rw/gta.h` — add declarations
- Modify: `src/rw/gta.cpp` — add EAXJMP/WRAPPER definitions

**Functions needed (addresses from GTA SA v1.0 US RE):**

```cpp
// HAnim plugin (RpHAnim)
WRAPPER RpHAnimHierarchy* RpHAnimFrameGetHierarchy(RwFrame* frame);  // 0x7CEBF0
WRAPPER int RpHAnimIDGetIndex(RpHAnimHierarchy* h, int id);          // 0x7CE640
WRAPPER RpHAnimHierarchy* GetAnimHierarchyFromSkinClump(RpClump* c); // 0x7CE6A0
WRAPPER void RpHAnimHierarchyUpdateMatrices(RpHAnimHierarchy* h);    // 0x7CEA90

// Quaternion math (RtQuat — RW SDK baked into exe)
WRAPPER void RtQuatRotate(RtQuat* q, RwV3d* axis, float angleDeg, int combineOp);  // 0x7EB2A0
WRAPPER void RtQuatConvertFromMatrix(RtQuat* q, RwMatrix* m);                       // 0x7EB070
WRAPPER void RtQuatReciprocal(RtQuat* out, RtQuat* q);                              // 0x7EB190
WRAPPER void RtQuatTransformVectors(RwV3d* out, RwV3d* in, int count, RtQuat* q);  // 0x7EB370

// Slerp (rtslerp.h — baked into exe)
struct RtQuatSlerpCache {
    RtQuat raFrom, raTo;
    float omega;
    int nearlyZeroOm;
};
WRAPPER void RtQuatSetupSlerpCache(RtQuat* from, RtQuat* to, RtQuatSlerpCache* c);  // 0x7EB1E0
WRAPPER void RtQuatSlerp(RtQuat* out, RtQuat* from, RtQuat* to, float t, RtQuatSlerpCache* c); // 0x7EB210
```

**Note:** Some of these may already exist in the RW SDK headers included by the project. Check `#include <rtquat.h>` and `#include <rphanim.h>` first. If they're already declared as statics/inlines from the SDK, we may not need WRAPPER — but GTA SA's exe has its own copies at specific addresses.

**Test:** Build succeeds, linker resolves all symbols.

---

### Task 5: Implement Ragdoll.cpp — Core Physics

**Files:**
- Create: `src/entities/ragdoll.cpp`

**Source:** Uncomment and adapt from `gtasa_src-main/Ragdoll.cpp` (435 lines).

**Key changes from original prototype:**

1. **Process ALL bones, not just HEAD:**
```cpp
// Original (line 189): if (m_boneNodes[i].m_boneTag != BONETAG_HEAD) continue;
// Fixed: remove the continue, process all bones
for (int32 i = 0; i < BONETAG_MAX_NUM; i++) {
    // ... apply torque to ALL bones
}
```

2. **Fix friction to all 3 axes:**
```cpp
// Original (line 247): m_boneNodes[i].m_vel.x *= 0.98f;
// Fixed:
m_boneNodes[i].m_vel.x *= 0.98f;
m_boneNodes[i].m_vel.y *= 0.98f;
m_boneNodes[i].m_vel.z *= 0.98f;
```

3. **Use RagdollBoneData for velocity/keyframe:**
```cpp
// Instead of m_boneNodes[i].m_vel (doesn't exist in OpenSA BoneNode_c)
// Use: m_physicsData[i].m_vel and m_physicsData[i].m_keyFrameQuat
```

4. **Add joint limit enforcement:**
```cpp
// Uncomment the limit loop (original line 239, was commented out):
for (int32 i = 1; i < BONETAG_MAX_NUM; i++) {
    m_boneNodes[i].Limit(m_blend);
}
```

5. **Add angular velocity damping (not just linear):**
```cpp
// Original only damped linear velocity. Add angular damping too.
```

---

### Task 6: Ground Collision Response

**Files:**
- Modify: `src/entities/ragdoll.cpp`

**What:** When a bone's world position goes below ground level (typically z=0 or terrain height), push it back up and dampen velocity.

**Approach:**
```cpp
// Simple ground plane at z = groundHeight
// For each bone, check if m_boneNodes[i].m_wldMat.pos.z < groundHeight
// If so: push up, reflect velocity z-component with damping
```

**Advanced (later):** Use `CWorld::FindGroundZForCoord()` for actual terrain height. For Phase 1, a fixed ground plane is acceptable.

---

### Task 7: Integration Hooks

**Files:**
- Modify: `src/core/main.cpp`

**Init** — Add to `InjectDelayedPatches()` after pipe hooks (around line 1857):
```cpp
g_ragdollMan.Init();
```

**Shutdown** — Add to DllMain unload or game exit hook:
```cpp
g_ragdollMan.Exit();
```

**Update** — Hook into ped update cycle. Options:
- Hook `CPed::ProcessControl` or `CGame::Process` 
- Or update from `RenderScene_after` (called every frame after scene renders)

**Trigger** — Hook `CPed::SetDie()` or `CPed::KillPedWithCar()` to call `g_ragdollMan.AddRagdoll(ped)` on death.

**Blend out** — After N seconds (e.g., 3-5s), reduce `m_blend` from 1.0 to 0.0, letting animation take over. Then `RemoveRagdoll()`.

---

### Task 8: Wire Up Init/Exit in Game Lifecycle

**Files:**
- Modify: `src/core/main.cpp` — InjectDelayedPatches
- Modify: `src/core/hooks.cpp` — if needed for ped death hook

**Init sequence:**
```
DllMain
  → InjectDelayedPatches()
    → g_boneNodeMan.Init()     // already shipping
    → g_ikChainMan.Init()      // already shipping
    → g_ragdollMan.Init()      // NEW
```

**Exit sequence:**
```
Game exit
  → g_ragdollMan.Exit()        // NEW
  → g_boneNodeMan.Exit()       // already shipping
  → g_ikChainMan.Exit()        // already shipping
```

**Update sequence:**
```
CGame::Process (per frame)
  → g_ragdollMan.Update(CTimer::GetTimeStep())
```

---

### Task 9: Test and Iterate

**Verification steps:**
1. Build succeeds with no linker errors
2. Game launches without crash
3. `g_ragdollMan.Init()` logged in debug output
4. Kill a ped → ragdoll activates (bones go limp)
5. Ped settles on ground (ground collision works)
6. After 3-5 seconds, animation resumes (blend out)
7. No memory leaks (pool management correct)

**Debug visualization:**
- Use `RwDebugRenderLine()` to draw bone positions during ragdoll
- Log bone velocities and blend factor per frame (throttled)

---

## Phase Roadmap

```
Phase 1 (MVP — Tasks 1-5):
  └── Basic ragdoll: bones go limp on death, settle with gravity
      No ground collision, no joint limits, no blend-out yet

Phase 2 (Polish — Tasks 6-7):
  └── Ground collision, joint limits, angular damping
      Ped stops falling through floor

Phase 3 (Integration — Tasks 8-9):
  └── Hook into game lifecycle, blend-out, trigger on death
      Full gameplay integration

Phase 4 (Enhancement — future):
  └── Vehicle impact forces, weapon knockback,
      pose correction (from RAGE reference),
      writhe simulation, multiple death poses
```

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Bone data table wrong → bones twist incorrectly | Medium | Cross-validate with OpenSA ms_boneInfos and game exe at 0x8D26D0 |
| HAnim functions not at expected addresses | High | Verify addresses against gta-reversed-master before WRAPPER |
| Ragdoll fights with animation system | Medium | rpFRAMEROTCAPTURED flags properly capture/release bone control |
| Memory corruption from pool management | High | Use same TList_c pattern as shipping IKChainManager_c |
| Performance with 8 ragdolls active | Low | Only active on death, pool-limited, simple math |

---

## Reference Files

| File | Path | Purpose |
|------|------|---------|
| Original prototype | `gtasa_src-main/Ragdoll.cpp` | Commented-out implementation to adapt |
| BoneNode_c | `OpenSA/src/game_sa/BoneNode_c.h/.cpp` | Existing bone node class |
| IKChain_c | `OpenSA/src/game_sa/IKChain_c.h/.cpp` | Reference for bone chain pattern |
| tBoneInfo | `OpenSA/src/game_sa/tBoneInfo.h` | Bone data structure |
| eBoneTag | `OpenSA/src/game_sa/Enums/eBoneTag.h` | Bone tag enum (MAX_BONE_NUM=32) |
| RAGE ragdoll reference | `rage/GTAV Source/.../TaskRageRagdoll.cpp` | Target quality reference |
| RAGE body parts | `rage/GTAV Source/.../MaleRagdoll.xml` | 21-part capsule dimensions |
| Ped skeleton | `skygfx_plus_expIV/src/entities/chars.cpp:39-49` | SA ped bone hierarchy |
| Game hooks | `skygfx_plus_expIV/src/core/main.cpp` | Init/exit integration points |
| RW SDK rphanim | `gta-reversed-master/.../rphanim.h` | HAnim API reference |
| RW SDK rtquat | `gta-reversed-master/.../rtquat.h` | Quaternion API reference |
