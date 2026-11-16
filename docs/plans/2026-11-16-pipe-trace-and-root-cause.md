# 2026-11-16 — Full pipe trace + root cause + AAP clean-port plan

Directive (m2077): "trace the whole pipe and properly analyse the whole thing so we can
figure how to properly clean aaps function in our mod and then build from there."

## 1. Verdict up front

The building/vehicle PIPE is not the defect. Traced end-to-end against AAP
(E:\SDKs\skygfx_original), the pipe matches AAP where it matters, and the runtime log +
screenshots prove the real failure: **world textures never resolve, so every building/
road/prop binds the dim-grey 1x1 policy sentinel (0x60,0x60,0x60) and renders as grey ×
ambient — pale-blue slabs by day, near-black pyramids at dawn.** The pipe then draws them
correctly, untextured.

## 2. Pipe trace (verified, file:line)

Hook install (both install; log `[Hook] hookBuildingPipe()` at DllMain):
- `hookBuildingPipe` buildingPipe.cpp:4285 = AAP buildingPipe.cpp:805 byte-for-byte:
  InterceptCall(0x53C15E → CustomBuildingPipeline__Update), InjectHook(0x5D7100,
  0x5D7D90 → CreateCustomObjPipe_PS2), Patch(0x5D7200, 0xC3). texdefs.cpp:1698 owns the
  0x5D7F40 (IsCBPCPipelineAttached) hook with a prologue probe (skip on mismatch).
- RVA error of 2026-11-15 DISPROVEN: correct PE-section mapping shows 0x53C15E = E8
  (CALL), 0x5D5B10 = E9 (JMP) — all hooks valid, identical bytes in both installs.
- Pipe creation: our CreateCustomObjPipe_PS2 (buildingPipe.cpp:4206/4237) is IDENTICAL
  to AAP (buildingPipe.cpp:723/754): RxPipeline + DNInstance_PS2 + reinstance +
  CustomPipeRenderCB_Switch + CreateShaders. Log confirms CreateShaders ran
  (03:53:22.694, ps=55 vs=17 compiled, 0 failed).
- Switch cb (buildingPipe.cpp:3805) = AAP Switch (buildingPipe.cpp:470) + our state
  prologue/epilogue (cull/alpha/ZWRITE-3L/ZTEST guards, camRas probe, pipeForceAlphaBlock,
  decal park, blend-leak probe). Dispatch: Xbox → _CB_Xbox (:2203).
- Xbox cb (buildingPipe.cpp:2203) = AAP Xbox (buildingPipe.cpp:281) structurally: transform
  constants, setDnParams, VS select (xboxBuildingVS / WIND variant), mesh loop with
  ambient/matCol/surfProps/env/detail shader select, D3D9RenderDual (:2632). Extra: state
  save/restore, elide, Forward+ gate, decal park — all additive, not draw-changing.
- CustomBuildingPipeline__Update (buildingPipe.cpp:163): orig + buildingAmbient =
  GetTimecycleAmbientPBR() (returns 0.100 per [PBR-Ambient] receipt; only zeroed by
  lightning override). No zeroing path.

Runtime (03:53 session, clean ed7a74e binary 2,612,224 B = git HEAD):
- Postfx chain completes every frame: CF-SRC → UpdateFrontBuffer → ColourFilter_Modern →
  Tonemap identGrade → Lens bypass → pf_gate → DrawFinalEffects → "camera raster = final
  displayed surface". Healthy.
- Zero building-cb receipts — EXPECTED: all are throttled/diagLevel-gated (P4).
- [PedRS] first-frame transient (dev src=10 dst=1) self-repairs to ok by frame 9950.
- `[texdefs] miss total=32 distinct=32` during world load; only ~2 recovered by
  cross-dict sweep (`dictfallback`), 30 → `miss-policy class=world action=sentinel`.

## 3. Root cause (screenshots prove it)

- gta_sa_jKA2NRNZn7.png (10:30): buildings = giant flat light-blue untextured slabs
  (grey sentinel 0x60 × bright bluish daytime ambient), ground flat, grass textured OK
  (grass_pc recovered), peds textured OK (in-dict lookups).
- gta_sa_fXMztMH5Yk.png (06:33): same geometry near-black (grey sentinel × 0.100 ambient).
- Mechanism: TexDefsFindCB (texdefs.cpp:699) — gameGetTexture (0x731720) parent-walks
  dict->parent; that link is NULL/broken in this ModLoader/ROSA install, so full-detail
  textures looked up while a LOD dict is current FAIL. TexDefsDictSweep recovers only
  when the target dict is ALREADY loaded (streaming order: LOD DFFs load before their
  parent full-detail TXDs). Failure → shared sentinel `skygfx_missgrey` (name overwrites
  the requested name — the original name is LOST) → material->texture poisoned FOREVER
  (game never re-requests) → every later frame binds grey.
- AAP has no answer for this: AAP has no texdefs layer at all (it assumes the vanilla
  find works). This is an install-compatibility defect, not an AAP-parity defect.

## 4. AAP clean-port position

KEEP (AAP-identical or AAP-superset with documented failure-class rationale):
pipe creation, hook set, Switch/Xbox cb draw mechanics, CreateShaders, Update ambient.
The state-prologue guards (pipeEnterAlphaMode, ZWRITE/ZTEST 3L, pipeForceAlphaBlock,
decal park) are ours — load-bearing per wave records (N1 master-fault kill, draw-over-CJ
class); strip-back is a LATER phase after the world renders correctly (can't A/B visuals
against a broken baseline).
FIX NOW (root cause): draw-time texture resolution — see §5.

## 5. Fix implemented in this build

1. Per-name world/sprite sentinels: TexDefsFindCB now returns a per-name clone sharing
   the singleton's 1x1 raster, NAMED with the requested name (original name preserved).
2. TexDefsMissTextureKind → raster-identity (recognizes clones + singleton; still never
   allocates).
3. TexDefsResolveAtDraw(name-carrying sentinel): gameGetTexture retry → case/extension
   variant → CTxdStore pool sweep; per-name cache (slot revalidation via byteMap, 1 s
   retry throttle for misses, O(1) hot path for hits); throttled receipt
   `[texdefs] resolve ...`.
4. pipeSetTexture (skygfx.h:1351) routes every pipe material bind through
   TexDefsResolveAtDraw — by the time geometry streams in, parent TXDs are loaded, so
   the retry succeeds and the REAL texture binds.
5. Sprite→world remap sites (render_callbacks, skin, buildingPipe, vehiclePipe) preserve
   the name via TexDefsRemapToWorld so those stay resolvable too.

## 6. Retest criteria

- `[texdefs] resolve '<name>' -> hit` receipts appear; miss-policy world lines stop
  accumulating for recovered names.
- Buildings/roads textured at day AND dawn (no pale-blue slabs, no black pyramids).
- If resolve stays `miss`: parent TXDs never load → next phase = force/preload parent
  TXDs at storeTxdName, or repair dict->parent links at TXD load (deeper RE of
  0x731720 parent logic).

## 7. Residual / follow-ups (unchanged backlog)

Corona pool empty (`[CoronaFeed] pool=0 active=0` — investigate), [PedRS] first-frame
transient, FxPass raw-SetTexture fixed (postfx_newfx), LuminanceReduce under-sample,
C14/E4/E5/R10-R13, state-guard strip-back phase (after visual baseline restored).
