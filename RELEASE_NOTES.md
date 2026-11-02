
## Wave 12 — Whiteout bounds, scene repair, wind sea, GPU particles (2026-11-02)

- **GodRays additive bound**: worst-case energy cap (grWorst ≤ 0.30, uniform scale-down, shape preserved); blend SET now 3-layer park-and-set; NightVision unbounded `while(n--)` loop capped at 64; InfraredVision radius sanitized; underwater composite audited clean (opaque REPLACE, not additive).
- **Scene-start repair**: new `pipeRepairSceneStartSync()` runs every frame in `RenderScene_before` — compares device vs rw cache on the blend/Z family and repairs to cache truth BEFORE the first draw. Kills the standing see-through divergence (device entering the scene blend-on/depth-off).
- **Height fog**: `heightFogDensity=0` is now a true no-op skip (was force-substituting 0.0015 + skyBot tint = the grey veil).
- **Motion blur**: velocity-cut detector now gated on camera identity (`PoseIsMainCamera`) — leaked secondary-camera matrices (mirror/vehicle cam) no longer false-fire cuts (rotor smear fixed).
- **Ped/marker contracts**: marker NULL-pipe entry contract now sets ALPHAFUNC=GREATEREQUAL@1; always-on cheap NULL-shader safety net under the defaultCbShaderHygiene lever; ped contract skip path resets to canonical GE@1 (leaked alpharef 140 can no longer hole ped skins).
- **Clouds**: weather-driven coverage wired (`CWeather__CloudCoverage` → noise constants); PBRBuilding frame spam throttled (was 31% of the log).
- **GPU particles module (new, experimental)**: `particles_gpu.{cpp,h}` + WakeParticlePS.hlsl — effectsPC.txd texture harvest, wind-scaled wake particles spawned from the live wake flushes, water-plane billboards, full state capture/restore. INI `gpuParticlesEnable` (default 0 = inert).
- **Perf**: font pin entry==canonical fast path; Forward+ skips full rebuild when zero tiles lit; debug overlay default OFF.
