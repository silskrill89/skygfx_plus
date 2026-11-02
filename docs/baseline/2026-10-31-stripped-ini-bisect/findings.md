# Stripped-INI bisect run findings — 2026-10-31 00:57–00:58

**Config**: stripped bisect INI (Xbox pipes bp=1/vp=2, all features OFF, ragdoll ON, `depthHookEnable=1` unblocked after the 0x7F9A3C Reset crash). Log: `skygfx_dbg.log` 00:57:12→00:58:17, frames 0→57441, clean DLL_DETACH (no crash — Reset fix verified).

**Shots (5)**: `gta_sa_3VM1j7srLm.png` (F7140, interior 10:11), `gta_sa_y8rbeOrZ5Z.jpg` (F34792, 10:27), `gta_sa_qu9uhTwshi.jpg` (F37271, 10:31), `gta_sa_GkcA1o1bkA.jpg` (F41023, 10:35), `gta_sa_4ntQTCdVmL.png` (F56382, pause menu).

## Bisect verdict

With ALL skygfx state-repair features OFF, the core diseases RETURN. The stripped INI removed the medicine, not the disease:

- `rs_mon FIRST_DIVERGE site=scene_start` (persistent, n=343..1325/frame): device = `SRCBLEND=2(ONE) DESTBLEND=1(ZERO) ZWRITE=0 ZTEST=0 ZFUNC=8` while rw+drv caches hold the world baseline (`5/6`, ZWRITE=1, ZTEST=1, ZFUNC=4). Nothing re-syncs the device at frame start with `stateBaselineEnable=0`.
- `rs_mon DIVERGE site=frame_end`: `ALPHABLEND rw=0 drv=0 dev=1` — device left blending ON across the frame boundary.
- `corona_blend`: `dev blend=1 src=5 dst=6 | drv blend=0` — drv≠dev split persists.

## Symptoms on stripped INI (all pre-existing core families)

| # | Symptom | Shots | Family |
|---|---------|-------|--------|
| S1 | Bright RED untextured cylinder floating mid-air (same world spot, 3 shots) | GkcA1o1bkA, qu9uhTwshi, y8rbeOrZ5Z | marker/prop texture-bind or corona-mesh — core pipe |
| S2 | Line of small dark/black squares across scene at eye level | GkcA1o1bkA, qu9uhTwshi, y8rbeOrZ5Z | corona/pointlight quads REPLACE (§4.7) — shields were masking |
| S3 | Black blotch decals on road | GkcA1o1bkA, qu9uhTwshi, y8rbeOrZ5Z | F9 decal family — texAlphaClassify OFF removed the classifier |
| S4 | Bush drawn over peds' upper bodies | GkcA1o1bkA, qu9uhTwshi | §4.1 vegetation draw-order — Z-contract repair gated off |
| S5 | CJ bandana renders as green face mask (interior) | 3VM1j7srLm | bandana-backwards family (§2) |
| S6 | Pure-black plant silhouettes in interior | 3VM1j7srLm | F6 family |
| S7 | Pause-menu backdrop near-fully black | 4ntQTCdVmL | verify vs vanilla pause dim |

## Verified-good receipts (do not regress)

- `[VehTex-FIRST] branch=xboxPaintNull tex='(null)' null=1 want=102621B8 dev0=102621B8 ok` — 3-layer vehicle bind fix confirmed reaching the device.
- `[Ped] pAmbient override` non-black after startup (0.040,0.023,0.003); black override only at #765 (loading, savedAmb black — guard passed through correctly).
- `[CoronaPool]` negative counter repaired to 0 every occurrence (cosmetic, working as designed).
- `[splashfoam]` white-smoke blend pin firing; no crash; clean shutdown.
- `[water] flatpath -> FFP fallback (pipe unavailable)` — expected: water pipe gated in bisect INI; water renders FFP-flat by design there.

## Conclusion

The root cause is the **core frame-boundary device-state split** (b7396c4 class): post-processing tails leave the device in post-state (blend ONE/ZERO, Z off), rw cache lies, all downstream `RwRenderStateSet` no-op. The disabled features (stateBaselineEnable, corona shields, texAlphaClassify, grass Z-contract) are the REPAIRS. Next test must run the FULL INI (backup: `skygfx.ini.bak-20261031-full`) with the crash fix + menu-click fix build.
