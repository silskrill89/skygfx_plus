# main.cpp Water Route Hunks (fix-1 output, apply-by-checklist)

Source lane: fix-1 (ses_e7625b8f8ffenlRFCcAfCtqQ86) — waterPipe.cpp:1785-1902
`waterPipe_directFlushRoute()` + Water_VS c11 swell ALREADY IN TREE.
Only these main.cpp hunks are outstanding. Companion: `ffp-binding-spec.md`
(gen-9) adds the W1/W5 unpark fix — apply BOTH in the same pass.

## HUNK 1 — src/core/main.cpp:5649-5658 (replace the "Fix: force FFP …" paragraph)

OLD (lines 5649-5658):
```
// Fix: force FFP (raw NULL VS/PS + RW cache seeds — same park-and-set idiom
// as the vanilla fall-through in CWaterLevel__RenderAndEmptyRenderBuffer_hook)
// and re-bind the vanilla water raster (waterclear256Raster, 0xC228A8) for
// the draw. Vanilla Im3D is fixed-function, so FFP + waterclear256 + the
// Im3D vertex colour reproduces the vanilla look; RW's Im3D transform (the
// call immediately before each site) owns decl/FVF/matrices, untouched here.
// The raster bind is deliberately NOT restored: it goes through the RW
// setter (cache + device stay in sync), vanilla leaves it bound across
// these draws too, and the next RW consumer (water state block 0x6EFB78 /
// RenderBoatWakes' wake raster 0x6ED9AA) re-binds its own.
```

NEW:
```
// Fix (W1 seam wave, 2026-10-30): route through the water pipe FIRST via
// waterPipe_directFlushRoute() (waterPipe.cpp) — full Water_VS + style PS
// bind (WVP/time, c10 gate, c14/c15 body-foam contract, s0-s3, Z pin,
// classify) + the raw pinned-stage draw, so these flushes get the same
// parallax/caustics/SHORE-FOAM band as the hooked flushes (the band lives
// in the parallax PS; FFP water has none — that was the missing coast foam
// and the hard vertical seam). FFP + waterclear256Raster (0xC228A8) below
// is now only the fail-open fallback when the pipe cannot run. RW's Im3D
// transform (the call immediately before each site) owns decl/FVF/matrices;
// the raster bind is deliberately NOT restored (RW setter keeps cache+device
// in sync; vanilla leaves it bound too — water state block 0x6EFB78 /
// RenderBoatWakes' wake raster 0x6ED9AA re-bind their own).
```

## HUNK 2 — src/core/main.cpp:5687-5693 (inside WaterDirectFlush_Draw)

OLD (lines 5687-5693):
```
	if(dbglog_throttle("water_flatpath"))
		dbglog("[water] flatpath site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p s3=%p (count=%u) -> FFP+vanilla raster",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0, (void*)staleS3,
		       s_flatCount);
	if(staleS0) staleS0->Release();
	if(staleS3) staleS3->Release();
```

NEW:
```
	if(staleS0) staleS0->Release();
	if(staleS3) staleS3->Release();

	// ---- W1 seam fix: pipe route FIRST, FFP only as fail-open ----
	// waterPipe_directFlushRoute() binds Water_VS + the style parallax PS +
	// the full pipe constant set (WVP, time, c14/c15 foam contract, s0-s3)
	// and draws this SAME TempBuffer through the pipe; TRUE = consumed.
	// FALSE = pipe unavailable (disabled/lost/insane counts) — the FFP
	// fallback below keeps the old fail-open path. The route restores the
	// pre-route stage internally, so on TRUE we only release our capture.
	extern bool waterPipe_directFlushRoute(void);	// waterPipe.cpp (function-local extern idiom)
	bool routed = waterPipe_directFlushRoute();
	if(dbglog_throttle("water_flatpath"))
		dbglog("[water] flatpath site=%d v=%u i=%u staleVS=%p stalePS=%p s0=%p s3=%p (count=%u) -> %s",
		       site, (unsigned)TempBufferVerticesStored, (unsigned)TempBufferIndicesStored,
		       (void*)prevVS, (void*)prevPS, (void*)staleS0, (void*)staleS3,
		       s_flatCount,
		       routed ? "pipe route (Water_VS+parallax PS, foam live)"
			      : "FFP fallback (pipe unavailable)");
	if(routed){
		if(prevVS) prevVS->Release();
		if(prevPS) prevPS->Release();
		return TRUE;
	}
```

Everything from `// Force FFP` at :5695 through `return res;` at :5722 stays as the
fallback path. Installs at :8058-8059 and the wake W5 hooks are unaffected.

## Acceptance (next run)
- `water_flatpath` must log `-> pipe route (Water_VS+parallax PS, foam live)`
- one-shot `[water] flatroute active` receipt must appear
- coast shots must show shore foam + no vertical seam + no flat milky planes flooding land

## Uncertainties (from fix-1)
- Swell amplitude 0.16 m total is hardcoded (waterPipe.cpp:1312 c11 upload); c11.x=0 = exact flat rewrite; INI key later if wanted.
- Foam needs tier>=2 + waterShoreFoam=1 + valid depth copy — if absent on routed water, check `[water] chain ... depthRT=ok` receipts.
- Site 1 (outside-world sheet) through parallax PS uses vanilla UVs — if far sheet tiling is odd, gate site 1 to reduced tier.
- Wake flushes (0x6EA2E8/0x6EDD7C) stay FFP by design (own raster).
