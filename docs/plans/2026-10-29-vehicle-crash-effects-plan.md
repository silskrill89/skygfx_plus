# Vehicle Crash Effects Plan — Wheel Detach / Occupant Ejection / Burnout Smoke / Tire Pops (m0964)

Date: 2026-10-29. Status: research complete, ready for review.
Scope: INI-gated (default OFF) crash-enhancement features:
(a) occupant ejection through windshield + wheel detach on hard crash,
(b) enhanced particles — denser burnout smoke, dramatic tire pops.
Sources: leaked GTASource SA (`E:\dev(dave)\releases\GTASource\gtasa_src-main`), GTAV dev_ng source, SADestruction
inventory (`docs/plans/2026-10-29-destruction-mod-inventory.md`), our src/ tree. **SADestruction has NO LICENSE —
reference only, no code copying.**

---

## 1. Shipped-SA inventory (what vanilla already does)

Source citations are `file:line` in `E:\dev(dave)\releases\GTASource\gtasa_src-main`. Game addresses are
plugin-sdk/community-documented and marked *(verify)* — the leak contains source only, no addresses.

| # | Behaviour | Vanilla? | Evidence |
|---|-----------|----------|----------|
| 1 | **Tyre burst** (gunfire / collision / sharp object) | YES | `CAutomobile::BurstTyre` `Automobile.cpp:9326-9395`: sets `DT_WHEEL_BURST` (:9353-9354), stats `TYRES_POPPED` (:9362), audio `AE_CAR_WHEEL_BURST` (:9368), random `ApplyMoveForce`/`ApplyTurnForce` yaw jolt (:9373-9376, `fBurstForceMult=0.03` :9325) |
| 2 | **Wheel status plumbing** (`INTACT/BURST/MISSING`) | YES (read) | `CDamageManager` via `Damage.Get/SetWheelStatus` — BURST set at `Automobile.cpp:3876` (commented-out `SetTotalDamage` wheel block :3869-3878), 9354; `DT_WHEEL_MISSING` is **only ever read** (`Automobile.cpp:11459-11461` — "simulate missing wheel by forcing a skip over the wheels processing code"), **never written by any game code in the leak** ⇒ no vanilla wheel-detach |
| 3 | **Burst-wheel handling feel** | YES | Burst wheels degrade adhesive limit: `Damage.fWheelDamageEffect` multiplier in `ProcessWheel` calls `Automobile.cpp:11878-11881, 11929-11932`; wobble/bump ratio `Automobile.cpp:11461-11469` |
| 4 | **Wheel smoke / skid particles (incl. burnout)** | YES | `CAutomobile::ProcessControl` → `AddSingleWheelParticles(...)` per wheel `Automobile.cpp:7415` (+ fixed-wheel variants :7433, :7443). Front-wheel skid smoke gated behind rear skid (`bDoFrontSkidSmoke` :7390-7393); flags `WHEEL_PARTICLE_NOSKIDSMOKE/BLOODY/EXTRASKIDS` :7398-7408. Function itself lives in `CVehicle` (not in leak subset) — handles wheelspin smoke, skidmarks, bloody wheels |
| 5 | **Crash impact particles** | YES | `CAutomobile::dmgDrawCarCollidingParticles` `Automobile.cpp:2417-2490`: sparks `g_fx.AddSparks` (:2437), smoke `g_fx.m_fxSysSmokeHuge` trail along move vector (:2464), paint-colour debris sprites (:2470+) |
| 6 | **Panel spring-damper flap** (bumper/boot/exhaust) | YES | `CBouncingPanels` — `SetPanel(CAR_MISC_B/CAR_BOOT/CAR_EXHAUST, ...)` `Automobile.cpp:332-347`; assigned on damage :3519-3524; applied to wheel-node matrix :7926-7927. Class def in `Vehicle.h` (not in leak) |
| 7 | **Panel/door/hood removal on destruction** | YES | `SetPanelDamage`/`SetDoorDamage`/`SetBumperDamage` `Automobile.cpp:3486-3707`: `DT_PANEL_MISSING`/`DT_DOOR_BASHED` + `SetComponentVisibility(..., VEHICLE_ATOMIC_NONE)` (:3535, :3606, :3689) — panels **disappear atomically**, no gib physics |
| 8 | **Occupant ejection from cars** | **NO** | No eject code anywhere in leak; peds stay seated in `STATUS_DRIVEBY/DRIVING`. Only bike ejection exists: `CBike::DamageKnockOffRider` (`Automobile.cpp:2782`), `CEventKnockOffBike` (:4319, 4364), `Ped.cpp:7556 KnockOffRider` |
| 9 | **Wheel-off-on-explosion** | **NO** | `SetTotalDamage` wheel-removal block is **commented out** `Automobile.cpp:3868-3878`; explosion just hides the whole wreck |
| 10 | **Tire pop drama** (debris, rim sparks, light) | PARTIAL | Burst = status flip + audio + force jolt (#1); no rubber shrapnel, no dedicated FX beyond shared smoke |

**Vanilla smoke limitation relevant to the wishlist**: burnout smoke exists but is sparse, single-texture, and the
front-wheel gate (#4) suppresses it in common scenarios. There is no rim-throw spark, no rubber-debris particle, no
"pop" burst at the instant of puncture.

## 2. Gap list vs user wishlist

| Wishlist item | Vanilla | Gap |
|---|---|---|
| Fly through front windshield on hard crash | none | Full feature: detach ped from vehicle → launch trajectory through front glass → ragdoll landing; glass-shatter FX |
| Wheel detaches on crash | `DT_WHEEL_MISSING` read path exists but is dead | Spawn rolling/bouncing wheel debris, hide wheel atomic, degrade handling (reuse MISSING read path!) |
| Burnout smoke enhanced | sparse `AddSingleWheelParticles` smoke | Denser/larger smoke, rim sparks when wheel is burst, ground darkening |
| Tires pop dramatically | status flip + audio | Instant rubber-debris burst + puff + camera-distance-scaled pop |

## 3. Design A — Wheel detach on crash

**Trigger** (INI-gated): hard collision impulse into/near a wheel well. Hook `CAutomobile::VehicleDamage`
(plugin-sdk 0x6A75E0 *(verify)*) or poll `Damage.GetWheelStatus` transitions in a per-frame scan; accept when
`fImpulse > CAR_DAMAGE_THRESHOLD * wheelMult` AND damage position within `wheelRadius * 1.5` of a wheel node
(`m_aCarNodes[CAR_WHEEL_*]`, frames already used at `Automobile.cpp:9300-9310`).

**Detach sequence** (per wheel):
1. `Damage.SetWheelStatus(wheel, DT_WHEEL_MISSING)` — vanilla read path at `Automobile.cpp:11459` then forces the
   wheel-simulation skip ⇒ handling "limps" for free; `ProcessWheel` never called for it.
2. Hide wheel atomic: `SetComponentVisibility(m_aCarNodes[idx], VEHICLE_ATOMIC_NONE)` — exact pattern of panel
   removal `Automobile.cpp:3535`.
3. Spawn a **detached-wheel gib**: load the wheel model (same `CVehicleModelInfo` wheel component, index via
   `CDamageManager::GetCarNodeIndexFromPanel`-style table) as a temporary `CObject` using the SADestruction
   `topple()` recipe — inventory §4(a)1: mass/turn-mass/friction/elasticity/CoM writes, `CWorld::Add`, linear
   velocity = car velocity + random outward kick, **height-scaled angular velocity** (0.12/height trick) so the
   wheel tumbles instead of whipping, auto-removal timer 120 s. *(Reference pattern only — reimplement, do not copy:
   no license.)*
4. Physics realism options: SA moving objects collide via **spheres only** — synthesize a sphere chain along the
   wheel axis using the trunk-collider idea (inventory §4(a)2, 2-3 spheres, radius = wheel radius).
5. Audio: reuse `AE_CAR_WHEEL_BURST` (already played by `BurstTyre` :9368) + a clatter one-shot on ground hits.
6. Optional geometric carve (Phase-3+, expensive): SADestruction `gm_crater` as a small fender-dent operator
   (inventory §4(c)1) — only with per-instance geometry clone discipline (inventory §4(c)4: never pool-sweep,
   vehicles share geometry).

**Restoration**: on repair/garage, `SetWheelStatus(INTACT)` + `VEHICLE_ATOMIC_DAMAGED/NORMAL` visibility; the
detached `CObject` simply expires. No geometry snapshots needed for Phase 1-2.

## 4. Design B — Occupant ejection through windshield

**Trigger**: player/NPC in `STATUS_DRIVEBY/DRIVING`, front-facing impulse (`DotProduct(damageNormal, forward) < -0.6`),
impulse/mass > speed threshold (INI `ejectSpeedThreshold`, default ~0.55 m/frame ≈ 130 km/h relative closing), seat
belt analog OFF (SA has none — every qualifying crash is eligible), ped health > 0, vehicle not mission-critical.

**Sequence**:
1. Detach ped: existing ragdoll infra — `ragdoll_death.cpp` death-ragdoll + `Dismember_*` (`sftbdyphsx.h:236-247`)
   already owns ped clump manipulation. Set ped ragdoll, remove from vehicle seating (`CPed` seat pointers — use
   same pattern as bike `KnockOffRider` event handling `Automobile.cpp:4319` which throws a ped off with velocity).
2. Launch: ped center-of-mass velocity = car velocity × 1.1 + forward bias through the front glass plane
   (`m_aCarNodes[CAR_WINDSCREEN]` node position), slight upward + random yaw spin. GTAV reference for the *feel*:
   `Ped.cpp:15650-15724` — `SetRagdollEjectedFromVehicle(pVehicle)`, contact-depth scaling vs chassis, friction
   boost; ejection task family `CTaskNMThroughWindscreen` (referenced `Ped.cpp:15716`). We do NOT port Euphoria —
   plain ballistic + our Bullet/native ragdoll integrator (`SoftbodyUpdate`/`Ragdoll_Simulate` slot).
3. Glass FX: spawn SA's own broken-glass particles (`CGlass` family already rendered via `RenderEffects` hook
   `main.cpp:4336`) at the windscreen node; optionally hide a windscreen component (`VEHICLE_ATOMIC_NONE`) as in
   panel removal.
4. Safety: only eject if a clear forward path exists (cheap `CWorld::ProcessLineOfSight` from ped chest through
   glass plane — SADestruction uses the same API, inventory §3). If blocked (truck cab), fall to in-cab ragdoll.

**Player special-case**: after ejection, camera switches to ped (game does this automatically once ped leaves
vehicle seat); player death on hard ground impact is vanilla fall-damage behavior — acceptable, INI-gated.

## 5. Design C — Enhanced burnout smoke

Vanilla already spawns skid/wheelspin smoke via `AddSingleWheelParticles` (`Automobile.cpp:7415`). Enhancement =
**amplifier, not replacement** (keeps SilentPatch/audio mods happy):
- Detect wheelspin: `m_aWheelState[i] == WS_SKIDDING` (gate seen at :7390-7393) or high `m_aRatioHistory` slip while
  thrust applied — read from `CAutomobile` members in a per-frame scan hooked at our game-frame driver.
- Spawn additional `g_fx` smoke particles (same `FxPrtMult_c` recipe as `dmgDrawCarCollidingParticles`
  `Automobile.cpp:2450`: grey mult, `fxSysSmokeHuge`) with INI-scaled rate `burnoutSmokeRate` and duration
  multiplier; offset at wheel contact point (`m_aWheelColPoints[i]` used :7415).
- Rim sparks when wheel status == BURST: `g_fx.AddSparks` recipe from `Automobile.cpp:2437`, rate scaled by speed.
- All CPU-side particle spawns into the **game's own** particle system — zero shader/CSO changes (§8).

## 6. Design D — Tire pop effects

Hook `CAutomobile::BurstTyre` (plugin-sdk 0x6A3830 *(verify)*; source `Automobile.cpp:9326`) or the
`SetWheelStatus(..., DT_WHEEL_BURST)` write sites (:9353-9354). On the INTACT→BURST transition, in addition to
vanilla (audio/force/stat already there):
- Instant radial burst of 6-10 rubber-debris particles (dark, small, fast, gravity) from wheel position —
  `g_fx` particle recipe; plus one grey smoke puff (`fxSysSmokeHuge` single spawn, `Automobile.cpp:2464` pattern).
- Optional 1-frame small spark `AddSparks` for gunfire pops (weaponType path at :2428-2437 distinguishes unarmed).
- GTAV behavior reference: `CWheel::ProcessImpactPopTires` `wheel.cpp:2238-2302` — chance-based pop (0.15/0.3), 3 s
  per-wheel cooldown, only-one-flat-tire-until-`GetNumFlatTires` reset; we copy the *cooldown/chance gating idea*
  (not code) to avoid pop-spam on scrapes.

## 7. Reuse mapping summary

| Need | Reuse | Source |
|---|---|---|
| Detached wheel as rolling gib | Temporary `CObject` spawn recipe (mass/vel/angular-vel/removal timer) | SADestruction `topple()` — inventory §4(a)1 *(reference only)* |
| Sphere-chain collider for wheel gib | Trunk/limb collider synthesis | inventory §4(a)2 *(reference only)* |
| Crash smoke/sparks/debris particles | `g_fx.AddSparks`, `fxSysSmokeHuge`, `FxPrtMult_c` | `Automobile.cpp:2417-2490` |
| Wheel contact positions / states | `m_aWheelColPoints`, `m_aWheelState`, `Damage` | `Automobile.cpp:7415`, :7390 |
| Missing-wheel handling degradation | vanilla `DT_WHEEL_MISSING` read path | `Automobile.cpp:11459` |
| Ped launch + ragdoll | `Ragdoll_Simulate` / `SoftbodyUpdate` / `Dismember_*` | `main.cpp:5404-5408`, `sftbdyphsx.h:236-247` |
| Panel-hide pattern for wheel atomic | `SetComponentVisibility(..., VEHICLE_ATOMIC_NONE)` | `Automobile.cpp:3535,3606,3689` |
| Frame update driver for scans | chained `CGame::Process` hook pattern (call-site 0x53E981, target 0x53BEE0) | SADestruction — inventory §3 *(reference only; we chain, never replace)* |
| Optional fender dent (phase 3) | `gm_crater` dent-operator idea | inventory §4(c)1 *(reference only)* |
| Windscreen glass FX | game `CGlass` (rendered in our hooked `RenderEffects` window) | `main.cpp:4336` |

## 8. INI key design — `[CrashFX]` (all default OFF)

```ini
[CrashFX]
crashFXEnable = 0            ; master switch — every feature below no-ops unless 1
wheelDetachEnable = 0        ; Design A
wheelDetachImpulse = 50.0    ; impulse·mass threshold multiplier
ejectEnable = 0              ; Design B
ejectSpeedThreshold = 0.55   ; relative closing speed (m/frame) to trigger ejection
burnoutSmokeEnable = 0       ; Design C
burnoutSmokeRate = 2.0       ; multiplier over vanilla smoke spawn rate
tirePopFXEnable = 0          ; Design D
tirePopCooldownMs = 800      ; per-vehicle pop FX cooldown (GTAV-derived gating idea)
```

Follows existing config pipeline: parse in `readIniFile` (main.cpp), cached like `softbodyNative`
(`softbody.h:38`), row in debug-menu/INI round-trip. Default OFF = zero behavior change for existing users;
enables A/B independently (C/D are pure-FX and can ship earlier/lower risk).

## 9. Integration points

| What | Where | Notes |
|---|---|---|
| INI parse + cached flags | `readIniFile` in `src/core/main.cpp` | same pattern as `ragdollEnable` (`skygfx.default.ini:204`) |
| Per-frame scans (wheel states, crash events, smoke spawns) | chained `CGame::Process` hook (install like SADestruction `install_hook` at call-site 0x53E981, chain previous target — inventory §3) | game-logic phase, before render; do NOT put spawns in render callbacks |
| Crash detection (preferred precise hook) | `CAutomobile::VehicleDamage` — 0x6A75E0 *(verify via disasm before hooking)* | trampoline hook, inspect `fImpulse`, `pVecDamagePos` |
| Tire-pop hook | `CAutomobile::BurstTyre` — 0x6A3830 *(verify)* | post-call hook; vanilla already does audio/force, we only add FX |
| Wheel-atomic hide | `SetComponentVisibility` call (vanilla fn, pattern `Automobile.cpp:3535`) | save/restore render state N/A — atomic visibility is not D3D state |
| Gib rendering | game's own `CObject` render path | **no** changes to our pipe callbacks (`vehiclePipe.cpp:3817` 0x5D9FE9 hook untouched) |
| Smoke/spark rendering | game `g_fx` system (rendered inside hooked `RenderEffects` 0x53E170 window `main.cpp:4241`) | we only *spawn*; game draws |
| Existing render-frame slot (fallback driver) | `RenderScene_after` `main.cpp:5394-5408` (already hosts `Ragdoll_Simulate`, `SoftbodyUpdate`) | acceptable for FX spawns; NOT for ped-seat state mutation (use game-frame slot) |

## 10. SM3.0 / performance impact

- **Zero shader changes**: all four features are CPU particle spawns + entity-state writes into the game's existing
  draw paths. No new CSO, no `Resource.rc` edits, no `pipeUploadPBR` involvement.
- Cost model: burns/tire pops bounded by INI rate multipliers + cooldowns (§8); wheel-detach is a one-shot event
  (≤4 per vehicle); ejection is rare. Particle budget: cap concurrent extra spawns per frame (e.g. 16) — same
  discipline as SADestruction's 2-blasts/frame queue (inventory §2.3).
- Risk is **logic-phase CPU**, not GPU: keep per-frame scans O(active vehicles) with early-out when
  `crashFXEnable == 0` (single cached bool check, same as `SoftbodyGetEnabled()` fast-no-op pattern
  `softbody.h:33-36`).

## 11. Effort estimates

| Phase | Feature | Est. | Depends |
|---|---|---|---|
| 1 | Tire pop FX (D) — BurstTyre post-hook + g_fx spawns | 0.5-1 d | address verify |
| 2 | Burnout smoke amplifier (C) — per-frame scan + g_fx spawns | 1-1.5 d | game-frame hook |
| 3 | Wheel detach (A) — status flip + atomic hide + CObject gib | 2-3 d | phase 2 driver |
| 4 | Occupant ejection (B) — trigger + ragdoll launch + glass FX | 3-5 d | ragdoll integration review, line-of-sight gate |
| 5 | (optional) fender dent via gm_crater idea | 5+ d | per-instance geometry clone work — likely separate project |

## 12. Risks

| Risk | Mitigation |
|---|---|
| **Crash safety** — hooking vehicle-logic fns can corrupt ped/vehicle pools | trampoline hooks with original-chaining; validate every pointer (`m_aCarNodes[i]`, seat ptrs) before write; feature no-ops rather than asserts; addresses marked *(verify)* must be disasm-confirmed on 1.0 US before first run |
| **SilentPatch interaction** — SP patches vehicle/ped behavior + defers corona batches (`main.cpp:2825, 3248-3293`) | chain-never-replace hook install (SADestruction pattern, inventory §3); our hooks are additive post-calls, no byte-patching of SP-owned sites; test with SP present (it is in the game dir) |
| **VehDeform.SA.asi conflict** — external deformation ASI in game dir (`E:\games\gtasa_skygfx_plus\VehDeform.SA.asi`) mutates vehicle geometry | our detach only flips status + visibility (no geometry writes) in phases 1-4; geometry-carving phase must query-load-order and disable itself if VehDeform is active |
| **Mission scripts** — scripts force wheel status / teleport peds | respect `bCanBeDamaged` gate (vanilla checks it first, `Automobile.cpp:2748`) and `STATUS_MISSION_VEHICLE`; never eject from cutscene vehicles |
| **Ejection exploits/oddity** (player eject at 1 km/h, ped stuck in glass) | threshold + line-of-sight gate; cooldown per vehicle; eject only once per crash event |
| **Softbody/ragdoll interplay** | ejection reuses the same ragdoll integrator — cap concurrent ejected peds (e.g. 4) to bound Bullet/native cost |
| **Address drift on non-1.0 exes** | same exe-signature check discipline as SADestruction (`0x401000` ∈ {0x53EC8B55, 0x16197BE9}, inventory §3); refuse to install on unknown exe |

## 13. Phased order (recommended)

1. **Phase 1 — Tire pop FX** (D): smallest surface, immediate visible payoff, exercises the BurstTyre hook path.
2. **Phase 2 — Burnout smoke** (C): establishes the per-frame scan driver + INI plumbing used by everything after.
3. **Phase 3 — Wheel detach** (A): reuses driver + g_fx; vanilla MISSING path does the handling half for us.
4. **Phase 4 — Ejection** (B): highest risk (ped state), lands last with all safety gates from §12.
5. **Phase 5 — (parked)** geometric dents/fender carve — only if phases 1-4 prove stable; likely needs its own plan
   (per-instance geometry discipline, §7).

Each phase ships behind `crashFXEnable=0` default; enable independently; document in `skygfx.default.ini`.

## 14. Address verification checklist (before Phase 1)

- [ ] `CAutomobile::VehicleDamage` — plugin-sdk claims 0x6A75E0 — confirm prologue bytes + call sites in game exe
- [ ] `CAutomobile::BurstTyre` — plugin-sdk claims 0x6A3830 — confirm
- [ ] `CAutomobile::ProcessControl` (0x6B1890 claimed) — only if per-vehicle hook preferred over pool scan
- [ ] `CGame::Process` call-site 0x53E981 / target 0x53BEE0 — already documented by SADestruction (inventory §3), confirm `byte[0x53E981]==0xE8` at runtime (it logs HOOK FAILED otherwise)
- [ ] `SetComponentVisibility` address (vanilla helper used at `Automobile.cpp:3535`) — confirm before direct call
- [ ] `g_fx` object address + `AddSparks` / `fxSysSmokeHuge` member offsets — confirm against leak structs / disasm
