# 2026-10-29 - Recurring object-render corruption (giant opaque teal/grey quads) - diagnosis #2 (m0962)

Status: READ-ONLY diagnosis, one doc, no source edited.
Method: ShareX mtime sweep + Pillow RGB sampling + visual read of all 8 frames + log correlation
+ **disassembly of the DEPLOYED ASI** (pefile/capstone) to test patch presence at code level.

Evidence base (relative paths only):
- Screenshots: C:/Users/aaaaaaaaa/Documents/ShareX/Screenshots/2026-10/ (ShareX wall-clock = log wall-clock)
- Log: E:/games/gtasa_skygfx_plus/skygfx_dbg.log (single run, 13,878 lines, 00:40:50.659 - 00:44:00.642)
- Binary under test: E:/games/gtasa_skygfx_plus/skygfx.asi - size 2,451,456, mtime 2026-10-29 00:39:57,
  ImageBase 0x10000000 (identical size/mtime to the logged [BUILD] line -> it IS the run's binary)
- Source (working tree, as-read): src/render/waterPipe.cpp (1782 lines, mtime 03:32:03),
  src/core/main.cpp (8489 lines, mtime 03:40:30), src/skygfx.h, docs/plans/2026-10-29-teal-quads-diagnosis.md

---

## 0. Verdict (one paragraph)

The frames in the 00:42-00:43 run were captured from a binary that **does NOT contain the teal-quad fixes** -
not because of what the log says, but because of what is inside the ASI: disassembly of the deployed
skygfx.asi shows the sanity guard still compiled as nIdx <= nVerts*3 (Patch A absent) and shows BOTH
vanilla water fall-through paths calling CWaterLevel::RenderAndEmptyRenderBuffer with **no VS/PS
neutralisation** (Patch B absent), and the post-fix marker strings ("water_vanilla_fallthrough",
"first hit", "count=%u") do not exist in the binary at all. Source mtimes (03:32/03:40) are ~3 hours
AFTER the run, no build/Release artifact exists, and the edits are uncommitted - the patches were written
into src but never compiled or deployed. Therefore the recurring slabs are still exactly the exp-1 defect:
the shared-vertex ocean-grid flush (live sample 621v/3432i = ratio 5.53, the ONLY ratio>3 flush in the whole
run) fails the *3 guard, main.cpp hands the buffer to the vanilla Im3D draw, and the stale pipe VS/PS still
device-bound projects the water buffer through the last object's WVP -> giant screen-covering opaque quads,
teal (water body colour through wrong constants) or grey (different stale PS), drawn over foreground geometry
because the garbage depth passes Z. The screenshots do NOT falsify Patches A/B - they are the pre-patch
baseline. The single actionable fix is: build + deploy the already-written Patch A/B (plus a binary string
gate so this never recurs), see section 6.

---

## 1. Evidence inventory (incl. the post-00:44 check)

Newest-file sweep of the ShareX 2026-10 folder (by mtime):
- gta_sa_* newest = the 8 files 00:42:22 - 00:43:58 on 2026-10-29. **Nothing after 00:44 is gta_sa_***
  -> no newer game captures exist; per instructions the 8 files from 00:42-00:43 are the evidence set.
- After 00:44 there are 6 vesktop_* files (Discord active window) 01:19:18 - 01:20:21. Checked all 6:
  non-1920x1080 crops (1647x601, 1643x479, 1228x681, 1910x525, 1565x666, 1522x960), no HUD in any,
  content = zoomed crops of already-known frames (pier railing/water, sky gradient). The log proves no game
  run after 00:44 (file mtime 00:44:00, ends "DLL_DETACH complete", single [BUILD] line at line 29).
  -> vesktop_* = Discord re-shares/zooms of the 00:42-00:43 evidence, NOT a new run. No new signal.
- Log [BUILD] line (line 29): asipath=.../skygfx.asi size=2451456 mtime=2026-10-29 00:39:57 built=Oct 29 00:39:10.
  The size heuristic given for this task (2451456 = teal fixes in) is **falsified by the binary itself** - see
  section 3 for byte-level proof. Use the string/disassembly gate in section 6 instead of a size table.

---

## 2. Per-image table (all 8 visually read; RGB from Pillow samples)

Slab colour cluster across 7 of 8 frames: (72,120,120) / (80,128,128) family (median per frame below).
Coverage = fraction of sampled pixels in the teal family (stride 6). "Grey variant" is a separate family.
Nearest log receipts: [water] entry receipts (2 s-throttled, skygfx.h:89-101) bracket each capture.

| # | File (mtime) | Clock | Artifact as seen | Slab RGB samples | Teal cov | Nearest log receipts |
|---|---|---|---|---|---|---|
| 1 | gta_sa_Fa3HyQjE78.png 00:42:22.063 | 11:19 | Giant flat GREY slab band across mid-frame behind/over the road, hard horizontal edges; water above it textured/correct | grey (114,121,129) at (320,540) and (1600,540); teal-median (61,68,80); water (132,177,218) at (1600,180) | 0.4% | 00:42:20.817 v=2044/3066; 00:42:22.820 v=40/60 |
| 2 | gta_sa_fwOki5g45c.jpg 00:42:34.953 | 11:32 | Flat TEAL slab left + bottom-left ~15%, razor edge through the pier; bay water correctly parallax-shaded | median (81,131,133), mode (80,128,128); water (113,158,189) at (320,180) | 14.7% | 00:42:32.816 v=312/468; 00:42:34.808 v=344/516 |
| 3 | gta_sa_33FAwmesLO.jpg 00:42:39.942 | 11:37 | Teal slab clipped to left frame edge only (mostly off-screen); rest of bay correct | median (80,134,142), mode (80,136,144), centroid (687,346) | 3.1% | 00:42:38.812 v=344/516; 00:42:40.815 v=344/516 |
| 4 | gta_sa_8nn00HyfHt.jpg 00:42:49.928 | 11:47 | Flat teal polygon over the bay, hard polygon edges overlapping the correctly rendered water behind it | median (80,127,134), mode (80,128,128); point (80,131,135) at (320,540) | 15.6% | 00:42:48.812 v=120/180; 00:42:50.810 v=120 |
| 5 | gta_sa_l56CmrMkQc.png 00:43:19.111 | 12:16 | Teal slab covers bottom ~45% INCLUDING the boardwalk/bench drawn in FRONT of it (garbage Z passes) | median (74,126,128), mode (72,128,128); points (70,122,123),(72,126,128) | 29.4% (max) | 00:43:18.825 v=248/372; 00:43:20.842 v=248/372 |
| 6 | gta_sa_qSqaV8zUs5.png 00:43:24.857 | 12:22 | Grey slab over the road bottom-left + teal slab right; car renders normally on top of both | median (72,124,126), mode (64,120,120); grey (104,115,124) at (960,900) | 15.9% | 00:43:22.841 v=56/84; 00:43:24.841 v=56/84 |
| 7 | gta_sa_Lk80k43oMu.png 00:43:50.898 | 12:48 | Axis-aligned dark teal rectangle right ~35% (mis-projection signature) + white blowout bottom half | median (72,126,126), mode (72,120,120), centroid (1257,428); white (255,255,255) at (960,900)/(1600,900) | 22.8% | 00:43:50.840 v=8/12 (local body via pipe) |
| 8 | gta_sa_Bn1MVuQikY.png 00:43:58.259 | 12:56 | Teal band with hard diagonal clip edge at the waterline + white blowout below | median (68,125,128), mode (64,120,120), centroid (1403,415); white (255,255,255) | 11.4% | 00:43:56.844 v=8/12 |
| - | (between captures) | - | The one proven UNSANE flush of the run: **00:43:12.808 entry verts=621 indices=3432 (ratio 5.53 > 3)** | - | - | log line 11853 -> guaranteed bail under the *3 guard (section 3) |

Shared signature in all 8: flat, hard-edged, opaque, sits exactly where water planes are, drawn OVER foreground
geometry (bench, car, road, railing), while the same frames render the sane-flush water correctly (textured,
specular, shore fade). Captures 7/8 white blowout = separate defect (section 5, R6).

---

## 3. Patch status: src vs the DEPLOYED binary (the decisive section)

Asked: "are Patch A + Patch B present in src" - YES both are (grep below). Asked: "did the screenshots come
from a run AFTER those patches" - **NO. The run's binary does not contain them.** Proof by disassembly of
E:/games/gtasa_skygfx_plus/skygfx.asi (capstone x86, VAs = 0x10000000 + RVA):

| Patch | In src (working tree) | In the run's ASI (00:39:57) | Disassembly / string proof |
|---|---|---|---|
| A: WbBufferCountsSane *3 -> *6 | YES - src/render/waterPipe.cpp:518-522 (`nIdx <= nVerts * 6`, 4096 cap kept) | **NO - still *3** | waterPipe_setRenderState sanity block at 0x1007FCC6-0x1007FD10: `lea eax,[edx-1]; cmp eax,0xFFF; ja 0x1008192E` (cap 4096), `cmp edx,ecx; ja` (verts<=idx), **`lea eax,[edx+edx*2]; cmp ecx,eax; ja 0x1008192E` = if(nIdx > nVerts*3) bail**, `mul 0xAAAAAAAB ... sub ecx,eax; jne` (idx%3). *6 would emit `lea eax,[edx+edx*2] / add eax,eax` - absent. |
| B: VS/PS neutralised before EVERY vanilla water fall-through | YES - src/core/main.cpp:5621-5628 (gate site) and :5773-5780 (mid-setup site): `SetVertexShader(NULL)`, `SetPixelShader(NULL)`, `*(void**)0x8E2448 = NULL`, `*(void**)0x8E244C = NULL` | **NO - neither site unbinds** | Gate function 0x100191B0: gate checks at 0x100191DA/0x100191E2/0x100191EF jump to 0x10019524 -> `dbglog_throttle("water_fallthru")` -> "[water] gate failed..." dbglog (0x10019535-0x10019557) -> **0x1001955A: `mov eax,[0x1026DE68]; test eax,eax; je; call eax` = vanilla RenderAndEmptyRenderBuffer, 10 bytes, no vtable 0x170/0x1AC Set*Shader calls, no `mov dword[0x8E2448/0x8E244C],0`**. Mid-setup bail: `cmp byte[0x1033E540],0; je 0x100194FB` (g_waterParallaxActive false) -> release prevVS/prevPS/prevDecl -> `jmp 0x1001955A` -> same vanilla call, no unbind. The NULL-then-bind dance at 0x10019287-0x100192AE exists only on the SUCCESS path. |
| Diagnostic: unconditional first-hit receipt + count | YES - src/render/waterPipe.cpp:1062-1087 ("bad buffer first hit...", "... -> vanilla draw (count=%u)") | **NO** | ASI contains exactly "[water] bad buffer v=%u i=%u -> vanilla draw" (2 pushes) and "-> classify disabled"; the strings `first hit`, `count=%u`, `ratio=` are absent. |
| Receipt tag `water_vanilla_fallthrough` | YES - src/core/main.cpp:5629 and :5781 | **NO (0 occurrences in the ASI)** | Along with `neutralised` = 0, `mid_setup` = 0. Present: `water_badbuf`, `water_fallthru`, `gate failed`, `water_nort`, `water_lost`. |

Timeline that explains the mismatch:
1. 00:39:10/00:39:57 - ASI built and deployed (size 2,451,456). Pre-Patch-A/B per the table above.
2. 00:40:50 - 00:44:00 - game run; 8 captures 00:42:22 - 00:43:58; log written.
3. ~03:32 (waterPipe.cpp) and ~03:40 (main.cpp) - Patches A/B + diagnostic written into the working tree.
4. Never rebuilt: `build/` has no Release/ output (only obj/, .vcxproj), game ASI still mtime 00:39:57.
   The edits are also uncommitted (HEAD 898e894, 2026-10-25; `git diff HEAD` shows them as working-tree changes).

Consequence: the size-based rule "2451456 = fixes in the run" is unreliable for this build series; the size
moved for other reasons. The correct gate is a string check on the ASI (section 6).

---

## 4. Log correlation (run = line 1 .. 13,878, 00:40:50.659 - 00:44:00.642)

Water flush receipts (21 distinct shapes sampled): every shape has indices/verts = exactly 1.5 EXCEPT one -
`00:43:12.808 verts=621 indices=3432` (ratio 5.53, log line 11853). Under the deployed *3 guard that flush
MUST take the bail at waterPipe.cpp:1062 (3432 > 621*3 = 1863), yet:
- "[water] bad buffer" = **0 lines**, "[water] gate failed" = 0, "vanilla fall-through" = 0,
  water_nort = 0, water_lost = 0, [WARN]=0, [ERROR]=0.
- Explanation: dbglog_throttle hashes the tag into only 256 shared buckets (skygfx.h:89-101) for ~276 tags,
  so "water_badbuf" was bucket-starved by a colliding tag; the bail is provable arithmetically instead of
  from a log line. (The new first-hit receipt in src removes this blind spot on the next run.)

Candidate lanes (the ones asked for), with receipts in THIS run:
- [water]: 204 lines (67 "restore", 67 "chain", entry receipts every ~2 s). Water pipe path itself is
  healthy: chains report sceneRT/depthRT ok, router=IDR_WATER_PARALLAX, VS/PS handles, cull/fog/z restored.
- water_badbuf / water_vanilla_fallthrough: 0 / 0 (see above - starved / not compiled in).
- CoronaDraw: 80 lines. pre-force blend=0 only at 00:41:14.515 and .524 (2 events, ~28 s before the first
  capture), each immediately shielded: [CloudShield] flush batch=sprites forced ONE/ONE at 00:41:14.517 and
  [FlushShield] restore ... (captured REPLACE -> canonical normal-alpha) at 00:41:14.519. All other
  CoronaDraw receipts are pre-force blend=1. Aggregate: blend=1 308 lines, blend=0 312 lines (the blend=0
  ones are the "was" side of CoronaShield receipts: 85 CoronaShield lines, 44 with was blend=0 -> forced to
  additive ONE/ONE, 41 already blend=1). CloudShield: 2 (sprites, clouds).
- MirrorPass: 85 lines, ALL "gate: mirrorDistortionFix off ... no composite overdraw"; TypeOfMirror=0 on
  the sampled frames; [CullResync] mirror exit rw=1 (85). No mirror composite ran at all.
- Shadows: only 6 lines total - all install-time ([shadow_install] hooks 0x53E0BE / 0x708300 / 0x706AB0)
  plus one Tonemap "shadow=193" scalar. No per-frame shadow-draw receipt exists in this build.
- EnvMap: [EnvMap] RenderSphereReflections targets a 256x256 RT (00:43:10.044), and [BUILDING] cb notes
  "camRas=04C311C8 256x256 != screen 1920x1080 - non-screen raster LIVE" -> off-screen pass, not a
  screen-space overdraw.
- Sprite-shield ordering: [PedOpaque] contract repaired 86 times (per-frame state repair, normal),
  rs_mon_report agree=18620 diverge=0 bad=0 (render-state monitor clean).
- Underwater: 3 [UW] lines (water.dat 307 planes, Water_Underwater PS res=276 compiled twice at 00:41:10/11).
  WATERDROPS overlay: 0 lines.

---

## 5. Ranked root causes (confidence + exact file:line + minimal fix)

### R1 - PRIMARY, 78%: ocean-grid water flush fails the *3 sanity guard -> vanilla Im3D draw with stale pipe VS/PS
Mechanism: SA emits water as (a) non-shared quads (4v/6i, ratio 1.5 - every sane sample in this run) and
(b) the shared-vertex ocean grid (621v/3432i = 23x27 verts, 22x26 quads). The deployed guard rejects (b),
g_waterParallaxActive stays false, and main.cpp hands the buffer to the vanilla CWaterLevel draw, which is
fixed-function and sets FVF/decl through RW but never unbinds shaders; the last pipe's VS/PS (and its last
object WVP in c0-c3) stays device-bound, so world-space water vertices (+/-3000) project to giant
screen-covering quads with garbage depth (hence drawn over the boardwalk/car/road) and the water texture is
shaded by the wrong constants -> flat teal (72,120,120)/(80,128,128) or grey (114,121,129) depending on which
stale PS is live. The code names this failure class verbatim at src/core/main.cpp:5604-5612.
Anchors: src/render/waterPipe.cpp:518-522 (guard, ASI still *3 at VA 0x1007FCF7),
src/render/waterPipe.cpp:1062-1087 (bail -> vanilla draw, ASI VA 0x1008192E-0x10081963),
src/core/main.cpp:5613-5635 (gate fall-through, ASI VA 0x10019524-0x10019563, no unbind),
src/core/main.cpp:5772-5785 (mid-setup fall-through, ASI VA 0x100194FB, no unbind),
hook sites src/core/main.cpp:7849-7852 (4 call sites 0x6E8790/0x6E8EF1/0x6E91E4/0x6E9963).
Log evidence: the only ratio>3 flush of the run is 621/3432 at 00:43:12.808; artifact is continuous across
all 8 captures spanning 96 s; sane flushes in the SAME frames render correct water.
**Minimal fix: deploy what is already in src (Patch A at waterPipe.cpp:521, Patch B at main.cpp:5621-5628 and
:5773-5780) = build + deploy only; keep the first-hit/count receipt at waterPipe.cpp:1062-1087.**
No new design needed. Optionally raise the cap 4096 -> 8192 as exp-1 specified (not required for 621 verts).

### R2 - SECONDARY, 6%: every OTHER mid-setup bail has the same stale-shader hole
waterPipe_setRenderState early-outs after entry: device-lost (water_lost), refraction/RT missing
(water_nort/water_nodepthrt/water_noreflrt), shader missing - all leave g_waterParallaxActive=false and reach
the same else-branch vanilla call. Anchors: src/render/waterPipe.cpp:691, :727, :764, :1113, :1141 and the
mid-setup else-branch src/core/main.cpp:5757-5785. Log evidence: 0 receipts for water_lost/water_nort in this
run (bucket starvation cannot be excluded, but nothing points at them). **Minimal fix: already covered by
Patch B's else-branch unbind - one patch closes R1 and R2 together.**

### R3 - CLEARED as primary, 5%: sprite/corona flush blend=0 (REPLACE) leaking past the shield
Evidence against: 85 [CoronaShield] receipts (44 had was blend=0, all forced additive ONE/ONE), the only
two pre-force blend=0 CoronaDraw events (00:41:14.515/.524) were shielded within 4 ms ([CloudShield] +
[FlushShield] captured REPLACE -> canonical normal-alpha), and [CoronaDraw] is blend=1 for the whole capture
window. Colour/geometry mismatch too: this lane emits glare/cloud sprites (white/warm), not water-teal
slabs that hug the waterline. Anchors if it ever regresses: src/core/main.cpp coronaDrawRawShield / FlushShield
sites, sprite flush at 0x70CF20. **Minimal fix if it recurs: extend the shield to any remaining Im2D flush
batch and log the batch id on every force (one line per frame).**

### R4 - CLEARED, 2%: mirror / env raster composite overdraw
Evidence: all 85 [MirrorPass] receipts say "gate: mirrorDistortionFix off ... no composite overdraw";
[CullResync] mirror exit rw=1; [EnvMap] RenderSphereReflections writes a 256x256 RT, not the backbuffer.
Anchors: src/render/mirrorPass.cpp (hook at 0x53EABA), src/render/envmap.cpp RenderSphereReflections.
**Minimal fix: none. Keep the per-frame gate receipt so this stays provable.**

### R5 - CLEARED, 1%: shadow quads
Evidence: only install-time [shadow_install] receipts (0x53E0BE, 0x708300, 0x706AB0); no per-frame shadow
draw logging in this build; SA static shadows are ground-projected dark grey, colour and shape mismatch the
captured slabs. Anchors: CShadows hooks, src/render/buildingPipe.cpp shadow contract repair.
**Minimal fix if R1 fails: add a one-shot per-frame shadow-draw receipt (tag "shadow_draw") before blaming.**

### R6 - SEPARATE DEFECT (not the teal emitter): underwater white blowout
Captures 7/8: bottom ~40% blown to pure white (255,255,255) with a hard curved edge. Correlates with the
[UW] chain (Water_Underwater PS res=276, water.dat 307 planes) and/or depthRT-based translucency below the
surface. Anchors: src/render/postfx_underwater.cpp, [UW] receipts at 00:41:10.113-.118. **Minimal fix:
track separately - clamp the underwater composite to [0,1] and verify depthRT validity below the surface;
do not couple it to the R1 fix.**

---

## 6. Verification plan (so the next run is decisive)

1. Build + deploy (python tools/fast_build.py), then BEFORE launching, string-gate the deployed binary:
   `grep -a -c water_vanilla_fallthrough <asi>` must be >= 1 and `grep -a -c "first hit" <asi>` >= 1.
   Also `grep -a -c "count=%u" <asi>` >= 1. This, not the file size, is the patch-presence test.
2. Run the same pier/underwater route. Expect: no grid bail at all (Patch A lets 621v/3432i through the pipe),
   i.e. [water] entry receipts with ratio>3 keep rendering through IDR_WATER_PARALLAX; "[water] bad buffer"
   only appears if a genuinely foreign flush shows up.
3. Acceptance: teal/grey slabs gone while sane-flush water is unchanged -> R1 confirmed and closed.
   If slabs persist while `bad buffer` count stays 0 -> R1 falsified; next move is a per-flush vertex-bbox
   dump (compare against water.dat bbox -3000..3000) and re-open R3/R6 with per-batch receipts.

---

## 7. Caveats

- Water receipts are 2 s-throttled per 256 shared FNV buckets (skygfx.h:89-101): only ONE flush shape per
  ~2 s window is sampled, so per-frame bail counts are inferred, not measured. The single 621/3432 sample is
  treated as representative because the artifact is continuous across all 8 captures.
- The identity of the stale shader at the vanilla-draw instant is still not captured (no bind dump on the
  bail path in this binary); the new first-hit receipt in src covers the count, and a GetVS/GetPS handle
  dump in the same bail path would close the identity gap.
- Patch-presence claims are byte-level (capstone disassembly + string table) on the exact ASI named by the
  logged [BUILD] line (size/mtime match), so they are not inference from behaviour.
- Line numbers refer to the working tree as read at diagnosis time: src/render/waterPipe.cpp 1782 lines,
  src/core/main.cpp 8489 lines; Patch A/B are uncommitted working-tree changes (HEAD 898e894, 2026-10-25).
- The task's size heuristic (2451456 = teal fixes present) is contradicted by this binary; the size table
  should not be used again without the string gate from section 6.
