# 2026-11-10 September Reference Gap Analysis

## Goal Line
Match `C:\Users\aaaaaaaaa\Documents\ShareX\Screenshots\2026-09` (Sep-23 ~07:09 batch, Sep-30 06:21-08:56) — zero on-screen glitches vs W0.5 build (2026-11-09 23:12-23:13).

## Reference Set (Sept 2026-09)
- **Sep-23 batch** (~07:09): gta_sa_Mtjbnldaze.jpg + 07:11-07:12 sequence + 07:57-07:59 + 09:49-09:56 + 10:13
- **Sep-30 batch** (06:21-08:56): additional outdoor/vehicle/landscape shots
- User note: even Sept shots had some visible issues — cataloged below.

## Current Set (W0.5, 2026-11-09)
| Shot ID | Frame | Visual defects |
|---|---|---|
| ECtpXKCWWK | 86127 | Black streak-bars + red square + olive band + pavement blotches |
| HTmdFzxoxn | 50123 | CJ kitchen: grey box at head + yellow marker cone at head height + dark floor slab + floating specks |
| Wo3DZ2MDJ8 | 66540 | Black floor blotches + grey slabs |
| hd9HXqites | 156602 | Whole-scene rainbow RGB channel-shred |

(Check for newer 2026-11 shots — none found beyond these four.)

## Per-Family Gap Table

| # | Family / Shot | Reference (Sept) | Current (Nov W0.5) | Suspected subsystem | Fixed on disk (unbuilt) | Remains after unbuilt |
|---|---|---|---|---|---|---|
| 1 | Rainbow / hd9HXqites | Clean color channels, no channel shred | Full-scene rainbow RGB channel shred | CA contract (ChromaticAberration c0/c1/c2 crosstalk) | YES — CA contract unified c0=(intensity,1,2.5/max(w,h),w/h); dead c1/c2 uploads removed; 0.10→0.25px edge, 2.5px hard cap; OFF bit-identical | None — should be GONE |
| 2 | Black streak-bars / ECtpXKCWWK | Clean pavement, no bars | Black vertical streak-bars + red square + olive band | Black-quad blend-0 hole + far/fog re-update race | YES — (a) black-quad white-shield: rw_fixes.cpp PostFxFlush_701FE9_hook blend==0 → gpWhiteTexture 3L bind + shield; (b) far/fog re-update restored with begun-camera gate | Olive band (FxAlpha/CClouds split) remains as #6 |
| 3 | Grey box head + marker cone / HTmdFzxoxn | Clean ped head, no marker cone | Grey box at head + yellow marker cone at head height + dark floor + floating specks | Family-C softbody TOCTOU (stale quat/clump writes) + gib lifetime | YES — SoftRagdoll_c::PreRender/BlendKeyframe gated at write time; Ragdoll_c::m_clump captured at Init; Dismember_Update/Render moved before A/B gate; receipts [Softbody] stale-write skip / [ragdoll] stale-write skip / [ragdoll] gib retire | Dark floor slab / floating specks (sentinel / depth) — see #5 |
| 4 | Black floor blotches + grey slabs / Wo3DZ2MDJ8 | Clean floor | Black blotches + grey slabs | Sentinel miss-policy: sprite sentinel erases geometry; world sentinel is dim-grey | YES — sentinel honors at 3 sites: buildingPipe.cpp bldPolicyTexture, skin.cpp, render_callbacks.cpp; sprite→grey remap, NULL→white unchanged; receipts n9_ped_remap/n9_pedcb_remap | Grey slabs **expected to remain** (bounded + receipt-named); verify visual bounds acceptable |
| 5 | Olive/milky band / ECtpXKCWWK | Normal atmosphere/sky | Olive/milky color cast band | [FxAlpha] drift + CClouds split mixed-capture | YES — CClouds single-source rw-capture through pipeDeviceStateSet (12 writes → 1); receipt cloud_split_restore; [FxAlpha] trajectory → ~0 | Verify olive band fully resolved; if not, LuminanceReduce bottom-17% under-sample |
| 6 | Auto-exposure wash / general | Stable adaptation | Exposure adapts but hits 1e-4 attractor (maxExp=1.0 clamp) | Luminance cache-desync: raw SetTexture(0,NULL) desync RW cache | YES — postfx_luminance.cpp:273-308 fix: NULL→flush→sceneRaster, commit NULL then measTex; AUTOEXP_DEGENERATE receipt | W0.5 used maxExp=1.0 (Modern maxExp=2.512) — retest with Modern profile; bottom-17% under-sample |
| 7 | Scene repair / frame repair / SpriteDepthFix / PedZW | Falling token counts | Tokens falling but still present: [BldZW] 77, [PipeAlpha] 69, [texdefs] 42, SpriteDepthFix 33, zfix2 30, PedRS 30, zfix 18, PedZW 7 | Legacy shim receipts | Partially — each fix wave reduces; W0.6 adds [texflush], bld_zw_guard, bld_opaque_nopark | Monitor until zero; not blockers if no visual artifact |
| 8 | Sept-30 outdoor/vehicle shots | Reference has some issues | Not re-captured on W0.5 | Unknown — need fresh capture | N/A | Retest needed post-W0.6 |

## Summary
- **P0 (blocking visual glitches):** All six visible defects (#1-6) have fixes **on disk, unbuilt** (wave-20 10 lanes + FxPass/CA scope gate).
- **P1 (residual visual):** Grey slabs (#4) expected to remain bounded; verify W0.6 retest. Olive band (#5) should be ~0. Exposure (#6) needs Modern maxExp=2.512.
- **P2 (noise tokens):** Scene/frame repair family falling; monitor to zero.

**Next step:** W0.6 build (after address-hygiene + FxPass lands) → user retest vs Sept references.

## Open Items from Sept Shots (User-Noted Issues)
- [ ] Document any visible defects present in the Sept 2026-09 reference shots themselves (user stated some existed) — add to backlog for honesty.