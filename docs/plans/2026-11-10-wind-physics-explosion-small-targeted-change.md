# 2026-11-10 Wind / Physics / Explosion — scope reality check

## What the user proposed (voice message, paraphrased)
Connect the wind, physics and explosive-shockwave systems so they behave as ONE system: wind moves planes/vehicles/objects; explosive shockwaves propagate realistically (push vehicles on their suspension, blow out windows, move palms/people).

## What actually exists in THIS repo (verified by grep/read)
This is `skygfx_plus_expIV`, a **D3D9 renderer/graphics mod** for GTA:SA. It hooks the game's render pipeline. It does **not** own gameplay physics.

| System | Reality in repo |
|---|---|
| Wind | Real. `src/render/wind.cpp` (pure wind math: gust table, lean, clock) + `src/render/motion_fx.cpp` (parent: applies lean to tree/palm transforms, noise-band gust amp) + `src/render/treeWind.cpp` (TreeAlpha, unrelated). Wind field: `CVector2D windPos` advanced in `src/core/main.cpp:1805-1806` from `CTimer__ms_fTimeStep`; intensity `2.0 + 1.5*CWeather__Wind` (`buildingPipe.cpp:307`); shader regs `REG_windPos=21`, `REG_windIntensity=22`, `c23 windExt` (`buildingPipe.cpp:86-87`). Consumers = vegetation VS sway only. |
| Explosions | **Not exposed.** No `CExplosion`, `CWeaponDamage`, `CCarDamage`, `CPhysical::ApplyImpulse`, `CTimer::IsExplosionInFrame`, `ms_fExplosionRadius` anywhere in `src/`. Blast mechanics live inside `gta_sa.exe` and are only reachable by hooking raw game addresses. |
| Vehicle suspension | **Not exposed.** Only a comment listing `fSuspensionForceLevel` etc. in `src/extras/vehicles.cpp:254-255`. No physics read/write. |
| Ped wind/knockback | Not present beyond ragdoll visual code (`src/extras/ragdoll_death.cpp`). |

## Consequence for "Small targeted change"
A **real** wind↔physics↔explosion coupling (vehicles pushed on suspension, windows blown out, shockwaves propagating) is **not** a small change in this repo — it means hooking the game's `CPhysical`/`CExplosion` physics functions by address. That is high-risk RE work, and it fights the hard rule (named variables, no raw address call sites) because physics entry points are address-only.

The genuinely **small, safe** change available here is **visual/shader-side only**:

### Option A — visual wind gust on non-vegetation objects (small, safe)
- File: `src/render/buildingPipe.cpp` (wind constant upload block ~:305-310) and/or `src/render/motion_fx.cpp`.
- Change: derive a short-lived gust multiplier from the same `windPos`/`CWeather__Wind` already read, and upload it as an extra/extended `windIntensity` so existing wind-capable shaders (WIND VS) apply a stronger sway for one or two frames after a threshold gust. Purely cosmetic; no physics writes; no new globals; reuses the ONE motion clock.
- Risk: LOW. Behavior-identical when calm.

### Option B — hook game explosion event → broadcast a visual gust (medium, still visual-only)
- Add one hook on the game's explosion trigger (already-addressed function; needs RE to find), then set a `g_lastExplosionPos` and have the WIND shaders add a radial sway impulse near that point. Still **visual** (no real vehicle physics), but delivers the "shockwave ripple" look.
- Risk: MEDIUM (requires locating + hooking a game function; address-hygiene applies).

### Option C — true physics coupling (NOT recommended as "small")
- Would require CPhysical/CExplosion hooks and writing suspension impulse/velocity. High risk, large scope, likely to destabilize the current stabilisation campaign.

## Recommendation
Do **Option A** now (small, safe, fits "small targeted change" and the hard rule). Treat Option B as a follow-up "shockwave look" wave once W0.6 is green. Defer Option C indefinitely.

## Status
Awaiting user confirmation of which option (A recommended) before implementing.