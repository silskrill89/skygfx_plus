# Cleanup pass — 2026-11-03

Four-item sanity scan of `src/`. No render-state logic, shader contracts, or hook
installs touched. No build, no commit. `/Zs` syntax checks PASSED (ZS_EXIT=0) on
all six edited files.

| # | Item | Outcome |
|---|------|---------|
| 1 | dbglog_throttle FNV bucket collisions | **REPORT** (108 colliding buckets, 286/367 tags) |
| 2 | INI keys with zero `config->` readers | **REPORT** (18 behaviorally-dead keys; 0 unambiguous deletions) |
| 3 | Dead functions, zero callers | **2 DELETED**, ~25 reported |
| 4 | Stale comments (recent-wave claims) | **14 comment fixes** |

Edited files: `buildingPipe.cpp`, `vehiclePipe.cpp`, `main.cpp`,
`debugmenu_ui.cpp`, `ragdoll_death.cpp`, `physicscfg.cpp` (all comment-only
except the two function deletions in the last two).

---

## 1. dbglog_throttle FNV-bucket collisions

Hash (authoritative, `skygfx.h:121-155`): canonical FNV-1a 32-bit
(`h=2166136261; h^=c; h*=16777619`), bucket = `h & 255`. The 256-slot
`s_lastTick[]` is **global per bucket**, so two different tag texts in one
bucket share a throttle window: whichever tag fires first silences the other
for 2s (the `ssao_aoval`/`cf_pipeline` starvation failure mode).

Throwaway script recomputed the exact hash over every literal
`dbglog_throttle("...")` in `src/` (comments excluded):

- **367 unique tags, 429 call sites**
- **108 buckets carry ≥2 distinct tags (286 tags affected)**
- Worst buckets: **27** and **61** (6 tags each), **19** and **249** (5 tags)

Worst offenders (bucket: tags):
```
 27: defpipe_contract, fp_path, pc_refloff, pf_iv, pf_normbuf, uw_tick
 61: ped_dual_skip, pf_cfm_done, pf_dfe, smaaTemporal, water_badbuf, water_nodepthrt
 19: mirror_gate_none, pf_fxframe2, pf_fxpass, pf_mb, pf_ycbcr
249: bld_env, mode_click, pc_g1, pf_exposure2, softbody_water
 15: grass_zfix, pf_vel_bind, pipeline_uploadpbr
 16: ped_rs, pf_grainps2, postfx_report
 28: cf_expchk, grad_draw, rad_dbg, tonemap_lumfb, vehpbr_mesh
 59: pf_gate, smaa_pending, smaa_vel
 65: mirror_state_restore, pf_ibl, texp_order, water_flatpath
 76: gsetrt_null, ibl_indoor, pcfg_backend_bad, postfx_flush_id
 84: VehicleSkipped:ENV, pf_hf_gate, sky_raster_off
 85: pcfg_create_exists, pf_luma_skip, sprite_flush_gate
 92: cf_exp, cf_modern_state, cfv_white
105: SMAA-DIAG, pf_iv_run, pf_lens3, pipeline_ibl
119: VehEnvMask, corona_shield, hf_crash2, pf_vel
124: bld_decal_park, pf_ivps2, state_baseline, wroute_badbuf
133: cf_switch, mirror_nops, pcfg_create_fail, smaa_broken
147: cfg_saveas, ibl_nocam, water_flood, water_lost
178: envmap_leeds, gr_crash2, pc_g4, pf_gr_set
197: envtex, pc_badpack, ssao_ov_comp, veg_tex0
203: PedZWrite, fp, radiosity
231: cf_pipeline, envi_clamp, vcs_rad_pending   (cf_pipeline STILL contended)
240: noise_nodev, pbr_ambient, water_copyfail
248: fx_alpha, gpu_part_render, ras_unsafe, vel_no_depth
```

**Recommended fix (follow-up wave):** grow `s_lastTick` 256→1024 and mask with
`& 1023` (one-line change in `skygfx.h`; the pointer-memo fast path is
unaffected) — kills ~95% of collisions. Alternative: bulk-rename colliding tags
(churns grep-able log strings; not recommended).

---

## 2. Config keys parsed with zero readers

Method: extracted all `cfg.get(...)` + `readint2/readfloat2(...)` keys in
`readIniFile` (main.cpp:7333-8568) → 253 keys, then read/write-classified every
`->field` across `src/`.

**Strict criterion (zero `config->` references): EMPTY** — every parsed key has
at least a debug-menu row or `saveConfigTo` round-trip read. Nothing deleted.

**Behaviorally dead (value never consumed by render/gameplay code):**
`pedAmbientFloor`, `[General] NoiseQuality`, `qualityPreset`, `sssIntensity`,
`sssVegIntensity`, `sssSkinIntensity`, `sssClothIntensity`,
`sssPostProcessThreshold`, `skinWrapFactor`, `skinSpecularPower`,
`skinSpecularStrength`, `hairAnisotropicPower`, `hairAnisotropicStrength`,
`hairSSSStrength`, `hairEnhanceEnable`, `vegetationSSSStrength`,
`vegetationAmbientBoost`, `vegetationEnhanceEnable` — all kept (documented
deferrals / ModeManager-owned MV_ rows / SSS feature-contract surface). Removal
needs a coordinated multi-surface pass (parse+clamp+save+menu+EMIT+struct) —
recommend a dedicated "config surface" wave.

**Bonus:** duplicate parses (second parse is a provable no-op):
`normalBufferOffset`/`normalBufferScale` (main.cpp:7835-7836 + 8114-8115) and
`pipeChainIntensity` (7840 + 8118). One-line deletions for a follow-up.

---

## 3. Dead functions

### Deleted (2 — static, zero refs)
- `ReadIniInt` — extras/ragdoll_death.cpp (sibling `ReadIniFloat` is live)
- `backendIsNative` — core/physicscfg.cpp (softbodyNative mirror inlines it)

### Reported, NOT deleted (reason per class)
- **Orphaned hook**: `CShadows__RenderStoredShadows_hook` + `_orig` (main.cpp) —
  zero refs; the 0x70A960 entry hook was NEVER installed (comment fixed §4).
- **Hook-shaped orphans** (uninstalled experiment hooks): `mysrand`,
  `SetLightsWithTimeOfDayColour`, `CreateRoadsignTexture_RwTextureSetName` +
  `CreateRoadsignAtomicA_RpAtomicSetGeometry` pair.
- **Feature functions**: `uploadLightCol`, `BlendEnvMapTemporal`,
  `CCustomBuildingRenderer__IsCBPCPipelineAttached` (twin hook in texdefs live),
  `CCarFXRenderer__IsCCPCPipelineAttached` (CLASS-6 deliberate stub).
- **RW bridge surface** (never delete): `D3DXCompileShaderFromFileA` WRAPPER,
  ~40 `Rp*/Rw*` zero-ref WRAPPERs in normmap_stubs.cpp/gta.cpp, rw/ mirror
  headers (Pools/LinkList/gta.h/MemoryMgr.h).
- **Entangled**: texdefs light block (4 statics + 3 orphans — entangled with
  live `CreateLight`/`DestroyLight`); `PackBRDFConstants` (documents c0/c1
  layout); grasssys scan-code inlines (D1 scope); envmap CAtomicModelInfo
  accessors.

Prior waves already removed `UploadUnifiedConstants` (D3) and wheels accessors
(D10).

---

## 4. Stale comments fixed (14 edits)

Budget claims ×7 (buildingPipe/vehiclePipe: "self-budgets to 256/frame" /
"budget-skipped" — budget is telemetry-only since the texalpha stability fix);
coronaZtest D11 ("PARSE-ONLY" → shield ZTEST enforcement block DOES read it);
RK_NOISEQ tooltip (regen removed with SkyNoise); ragdoll file refs ×2
(ragdoll.cpp → sftbdyphsx.cpp); PC_GrassRenderer names (never landed —
CGrassRenderer/CGreassInstancer are the real names); shadow-install coverage
claim (main.cpp:9684 — now documents the ORPHANED hook + missing coverage).

---

## Verification

`cl /Zs` on all six edited files via temp .bat (`/I resources +
/I third_party/bullet/src` + premake include set): **ZS_EXIT=0**, only the
pre-existing C4005 macro-redefinition warning. All deletions double-verified
(bare-name + call-site grep, zero refs before removal).
