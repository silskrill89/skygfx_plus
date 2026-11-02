# Perf Check — build 10 run, 2026-11-02 (READ-ONLY forensics)

Sources: `E:\games\gtasa_skygfx_plus\skygfx_dbg.log` (81,755 lines / 11.4 MB, session
15:12:46→15:29:41 ≈ 1,014 s), 6 newest ShareX screenshots 2026-11 (`gta_sa_*`),
`docs/plans/2026-10-31-source-inventory.md`, landed-fix verification in source.
No source edited, no build.

## Run snapshot

- **Displayed FPS ≈ 170–180** (two independent estimates): CUT-reset lines carry
  `posCut=9.00 = 2.5/dtScale` → dtScale=0.278 → fTimeStep≈5.6 ms ≈ 180 FPS
  (postfx_motionblur.cpp:237, MotionBlurDtScale = fTimeStep×50). Log frame-id tick rate
  516/s (e.g. `#1032458`@15:29:35.308 → `#1034575`@15:29:39.415) ÷ ~3 camera
  updates/frame (main + IBL + mirror/envmap) ≈ 172 FPS.
- **Screenshot↔log correlation (F/C overlay)**: `gta_sa_uMLeIJ6nhB.png` (15:29:25.8)
  shows `F 1027749 | C 0xFAEA5`; `gta_sa_zWCLRk1sSg.png` (15:29:38.2) shows
  `F 1033351 | C 0xFC67B` — both land inside the log's `#N` range at matching
  timestamps. Overlay = debugmenu_ui.cpp:4449/4505 (`"F %u | C 0x%04X"`).
- Config state (skygfx.ini): `diagLevel=0`, `debugOverlay=1`, `ssaoEnable=1`
  (16 samples, 2 blur passes, temporal OFF), `forwardPlusEnable=1`,
  `velocityBufferEnable=1`, `motionBlurStrength=0.028`, `waterQuality=3`,
  `profilerEnable=0`.
- Note (visual, not perf): two newest screenshots (`zWCLRk1sSg`, `uMLeIJ6nhB`) are
  nearly white/washed-out — separate exposure lane, flagged only because it recurs.

---

## Ranked remaining frame-time costs

### R1 — Log pipeline: spammer alive + per-line file open/close (hygiene contract broken)

**Estimated cost**: ~80 lines/s average × ~25 µs/line (CreateFileA+WriteFile+CloseHandle
+ GetLocalTime + 2× snprintf + ring put) ≈ 0.2 % CPU average **on the render thread**,
with burst micro-stalls; 11.4 MB disk per 17-min session. Modest average, but the
"quiet at diagLevel=0" contract is simply not implemented.

**Evidence**:
- **The spammer: `PBRBuilding: frame` = 25,886 lines (31 % of log)**, untagged so it
  evaded tag-based counting. Site: buildingPipe.cpp:2707-2708 —
  `if(++pbrFrameCount % 300 == 1) dbglog(...)` counts **atomic callback invocations**,
  not frames (~26 lines/s all session). This is the exact bug the vehicle lane already
  fixed: vehiclePipe.cpp:2953-2957 replaced its `%3600` draw-counter gate (7,178
  lines/6 min) with `dbglog_throttle("vehpbr_env")`; the building lane never got the
  same fix.
- Throttle receipts still emit at diagLevel=0: `[PostFX]` 16,356 + `[PF]` 13,104 +
  `[VehiclePBR]` 4,920 + `[Pipeline]`/`[ForwardPlus]`/`[FX]`/`[CF]`… ≈ 55 k lines of
  2 s-cadence receipts (~30 lines per window). skygfx.h:126 silences them only at
  `g_diagLevel >= 2`; INI has 0.
- `g_diagLevel` is **never checked** in the log writer — debug_handl.cpp:48 declares it,
  dbglog_internal (:260-332) never reads it.
- Per-line I/O: debug_handl.cpp:314-331 — `CreateFileA` + `WriteFile` + `CloseHandle`
  for **every line**. The 1/s FlushFileBuffers throttle (:325-330) landed; the
  open/close per line did not.

**Owner files**: `src/render/buildingPipe.cpp` (spam site), `src/core/debug_handl.cpp`
(writer), `src/skygfx.h` (throttle), `skygfx.ini` (diagLevel).

**One-line fixes**: (a) INI `diagLevel=2` → silences every throttle-gated receipt, zero
code; (b) buildingPipe.cpp:2708 → `dbglog_throttle("pbrbld")` (copy the vehicle-lane
precedent); (c) debug_handl.cpp: keep one persistent append HANDLE from `diag_init`,
close at exit — kills the per-line CreateFileA/CloseHandle.

### R2 — Debug correlation overlay: a full ImGui frame every game frame (default ON)

**Estimated cost**: ~0.15–0.4 ms CPU/frame (Win32+DX9 NewFrame, ImGui::NewFrame,
9× AddText, Render, RenderDrawData). At ~180 FPS the frame budget is ~5.5 ms →
**3–7 % of per-frame CPU**, for a diagnostic readout.

**Evidence**: main.cpp:8929 calls `DebugOverlayDraw(d3d9device)` unconditionally at the
DrawFinalEffects hook tail; debugmenu_ui.cpp:4490-4552 runs the complete ImGui frame
whenever the menu is closed ("running it is the overlay's whole purpose"); INI
`debugOverlay=1` (default 1, debugmenu_ui.cpp:4429). Screenshots confirm it live
(F/C readout top-left in every newest capture).

**Owner file**: `src/core/debugmenu_ui.cpp` (+ `skygfx.ini`).

**One-line fix**: ship `debugOverlay=0` (F9 still toggles it live); optionally throttle
the overlay's ImGui re-render to ~10 Hz keeping the last draw data between updates.

### R3 — SSAO chain on: 4 passes + camera/RT swap cycles per frame

**Estimated cost**: ~1.5–3 ms GPU/frame at 1080p (quarter-res 16-sample AO + 2×
quarter-res blur + full-res upsample + half-res 960×540 normal buffer) plus 4–6
render-target switches — each SSAO pass does `RwCameraEndUpdate → SetRaster →
RwCameraBeginUpdate` (postfx_ssao.cpp:1010-1012), which flushes D3D state, and each
pass snapshots 7 sampler stages × 5 fields (:1025-1031). Largest GPU line-item found.

**Evidence**: INI `ssaoEnable=1`; `[PostFX] DrawSSAO_Overhaul ENTER/completed/composite`
258 throttle windows each; `[PF] NormalBuffer bind … out=960x540` every window;
`pf_gate` skip-bits show `ssao=0` (= not skipped → running; the pf_gate fields are
*skip* flags, postfx.cpp:1914 `pfSkip |= PFG_DEPTHHIST`).

**Owner file**: `src/render/postfx_ssao.cpp` (+ `skygfx.ini`).

**One-line fix** (if a tuning pass is acceptable): `ssaoSampleCount 16→8` +
`ssaoBlurPasses 2→1` ≈ 40 % SSAO cost cut; otherwise accept — it is a chosen visual
feature. (Also verify: Pass 1 is named SSAO_Temporal and runs with
`ssaoTemporalEnable=0` — the blend is 0 via c4 (postfx_ssao.cpp:1079) but confirm the
history plumbing is skipped, not just the accumulation.)

### R4 — Velocity cut detector: systematic false cuts (~11.9 k/session)

**Estimated cost**: per false cut: 1920×1080 velocity `Clear()` + skipped pass + one
unthrottled log line; ~15–25 cuts/min all session. GPU cost modest, but motion-blur
history is reset every few seconds (visual continuity) and the log I/O is real.

**Evidence**: 11,677 + 189 `camera CUT reset` lines spread across **every** minute of
the run (histogram: 1,510 @15:14 … 1,384 @15:26). posDelta values cluster at
**recurring near-identical values** — 19.5×53, 6.0×63, 5.3×49, 4.3×13 — fixed
camera-position jumps, not organic motion (fwdDot≈1.0 in all samples: pure translation
jumps). Detector: postfx_motionblur.cpp:240-245 (`posDelta > 2.5/dtScale` →
`ClearVelocityToZero` + re-latch + `dbglog` + return). Recurring fixed deltas strongly
suggest a secondary camera's view matrix (IBL capture / mirror / envmap) leaking into
the D3D view matrix the detector reads (:191-201 derives pose from the raw D3D view
matrix).

**Owner file**: `src/render/postfx_motionblur.cpp`.

**One-line fix**: validate the cut against the game camera (`Scene.camera` position)
instead of the raw D3D view matrix — or suppress the detector for one frame after any
secondary capture (IBL/mirror/envmap) ran.

### R5 — font_flush pin: ~30 state ops per glyph batch, no fast path

**Estimated cost**: ~0.05–0.15 ms/frame with HUD text visible (several CFont batches per
frame; each batch = 3× GetRenderState + 3× RwD3D9GetRenderState + 2× RwRenderStateGet +
4 park-and-set RwRenderStateSet + 3× RwD3D9SetRenderState + 3× dev->SetRenderState + 9
restore writes). GetRenderState is a driver round-trip.

**Evidence**: `[font_flush] gate pinned=` 380 throttle windows (fires continuously);
main.cpp:4130-4178 — the pin deliberately park-and-sets "so every L1 write is a real
transition" (:4147-4159), i.e. it pays full transition cost even when the entry state
already equals the canonical SRCALPHA/INVSRCALPHA (the common case).

**Owner file**: `src/core/main.cpp` (glyph-flush hook 0x7273D0).

**One-line fix**: fast-path compare — if the captured entry block already equals
canonical, skip capture + park-and-set and just call the original.

### R6 — Forward+ enabled, contributes zero, full rebuild runs anyway

**Estimated cost**: <0.05 ms CPU/frame (empty loops, PF_SCOPE pairs, stats, throttle
checks) — small, but 100 % dead work all session.

**Evidence**: `[ForwardPlus] fp_fallback: tile list empty (collected=0 added=0
rawGame=0 tilesLit=0 inst=0)` in 361/368 throttle windows; `path: pushed=0 drainRaw=0
dedup=0 collected=0 gpu=0 fallback=1 feedRaw=0 rawGame=0`. forwardplus.cpp:1401-1457 —
CollectLights + sort + dedup + merge + view-transform + CullLightsToTiles all execute
**before** the `tilesLit==0` fallback decision (:1451).

**Owner file**: `src/render/forwardplus.cpp`.

**One-line fix**: early-out before the rebuild when the drain buffer, registry snapshot
and raw game feed are all empty → set `g_fpFallbackFrame` directly and skip
rebuild/cull. (Verify at night first: rawGame=0 may be a daytime-scene artifact; if FP
never contributes at night either, `forwardPlusEnable=0` is the free config fix.)

### R7 — Water RT startup flip-flop: 239 recreate cycles in one minute (one-time)

**Estimated cost**: one-time startup stutter — ~1,434 CreateTexture/Release ops
(3 RTs × 239 cycles) at 1920×1080 D3DPOOL_DEFAULT; **zero steady-state cost** (all 239
`release … (recreate …)` lines fall in minute 15:13; stable afterwards).

**Evidence**: log shows `release scene (recreate 1920x1080 -> 2048x1024)` at 15:13:20.175
and `release scene (recreate 2048x1024 -> 1920x1080)` at 15:13:20.182 — **7 ms apart**.
waterPipe.cpp:1759 sizes all three Ensure* calls from the live viewport; during
menu/load the viewport alternates between camera dims (1920×1080) and 2048×1024, and
EnsureSceneTexture recreates on any dims change (:1102-1119).

**Owner file**: `src/render/waterPipe.cpp`.

**One-line fix**: canonical dims — clamp the Ensure* size to the camera resolution
(ignore front-buffer-sized viewports), so the menu/load viewport bounce can't
recreate.

---

## Verified NON-costs (landed fixes working — do not re-litigate)

| Landed fix | Verification |
|---|---|
| Elision verify readbacks | Gated `g_diagLevel >= 1` (buildingPipe.cpp:1266; vehiclePipe.cpp:881-884 comment + site) — **zero readbacks at diagLevel=0**. Elision itself working: `[BldElide] writes skipped…` 363, `[VehElide] … elided=N writes` 226 receipts. |
| RSMon 1-in-30 | `rs_mon_report` 157 lines; `samples=1208 agree=0 diverge=0` — throttled, cheap. |
| Sprite-shield gating | `[sprite_flush]` 336 + `[postfx_flush]` 114 throttle receipts; gates are compare-only per flush event (`shielded=… skipped=…`). |
| Log flush throttle | `FlushFileBuffers` 1/s max (debug_handl.cpp:325-330) — landed. |
| VehOpacity spam | 20 lines total (one early burst, then quiet) — not a spammer. |
| ImGui menu | Gated `config->debugMenuOpen` (postfx.cpp:2674) — zero cost when closed. "Larger window" only matters while open. |
| VehiclePBR heartbeat | `dbglog_throttle("vehpbr_env")` (vehiclePipe.cpp:2958) — the old %3600 draw-counter bug fixed there; 4,920 lines are 2 s-cadence receipts (silenced by diagLevel=2). |

## Stable-config keys costing perf for no/low visual gain

| Key | Cost | Gain | Action |
|---|---|---|---|
| `debugOverlay=1` | Full ImGui frame per game frame (R2, 3–7 % CPU) | Diagnostic F/C readout | **Set 0** (F9 toggles) |
| `diagLevel=0` | ~55 k throttle receipts/session (R1) | None at 0 vs 2 | **Set 2** — silences receipt class, zero visual impact |
| `forwardPlusEnable=1` | Per-frame rebuild + fallback (R6) | Zero contribution all session (rawGame=0) | Verify at night; else **set 0** |
| `velocityBufferEnable=1` + `motionBlurStrength=0.028` | Velocity RT + prev-depth full-res R32F copy (postfx.cpp:1908-1911, gated live) + cut detector + per-frame pass | Barely visible: strength 0.028 in 169 windows, **0.000 in 106** | Candidate for 0 if the subtle blur isn't wanted |

## Fix-order recommendation

1. **Free config wins** (no build): `diagLevel=2`, `debugOverlay=0` → removes R1
   receipts + R2 entirely; log should drop from 81.7 k to ~1–2 k lines/session.
2. **One-line code fixes**: PBRBuilding throttle (R1b), persistent log HANDLE (R1c),
   FP early-out (R6), font-pin fast path (R5), cut-detector camera-source fix (R4),
   water canonical dims (R7).
3. **Optional tuning**: SSAO sample/blur reduction (R3) — only if the user wants more
   GPU headroom; it is a chosen feature.
