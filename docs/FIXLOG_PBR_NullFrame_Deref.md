# FIXLOG: PBR render crash — NULL frame / NULL light deref in custom pipelines

## Symptom
Game crashes to a Windows "Application Error" dialog during gameplay (render path),
after init now completes cleanly:

```
The instruction at 0x000000000007F39FB referenced memory at 0x0000000000000008.
The memory could not be read.
```

`0x7F39FB` = `RwFrameGetLTM()` (game/RW build). `0x8` = `NULL + 8` = the
`RwMatrix matrix` member of `RwFrame` (offset 8). So the call is
`RwFrameGetLTM(NULL)` -> `&(NULL)->matrix` -> deref of address `0x8`.

## Root cause (rwsdk contract)
RenderWare custom-pipeline render callbacks are written on the assumption that
every atomic has a **valid parent frame** and that a **current camera / direct
light** exists. Our PBR building callback violated that for two cases:

1. `CustomBuildingEnvMapPipeline__SetupEnv(atomic, NULL, &envmat)`
   - fallback `envframe = RwCameraGetFrame(RWSRCGLOBAL(curCamera))` is NULL when
     there is no current camera;
   - `frame = RpAtomicGetClump(atomic) ? RpClumpGetFrame(clump) : RpAtomicGetFrame(atomic)`
     is NULL for **standalone atomics (not attached to a clump / no parent frame)**.
   - both feed `RwFrameGetLTM(NULL)` -> crash.

2. `setWindParams(atomic, frame)` where `frame = atomic->object.object.parent`
   is NULL (WIND-shader buildings whose atomic has no parent).

3. `pipeUploadLightColorPS(pDirect, ...)` / `pipeUploadLightDirectionPS(pDirect, ...)`
   where `pDirect` (game direct-light global `@0xC886EC`) is NULL during passes
   with no active direct light. `pipeUploadLightColorPS` derefs `light->color`
   at offset 8 -> crash.

### RW SDK references (UserGuideVol1-3.txt)
- Vol1 S2.10.3 Frames: "The LTM ... can be retrieved using `RwFrameGetLTM()`.
  ... The LTM describes the total transformation from Object Space to World Space."
  -> `RwFrameGetLTM()` requires a valid `RwFrame`.
- Vol2 S13.2.2 (custom node callbacks): `LTM = RwFrameGetLTM(node->frame);`
  -> the SDK examples always assume the frame is non-NULL. There is no
  "NULL frame" convention in rwsdk -- guarding is our responsibility.

## Fix (applied in `src/buildingPipe.cpp`)
- `CustomBuildingEnvMapPipeline__SetupEnv`: early-out to identity `envmat` when
  `envframe` is still NULL after the camera fallback; identity `lastmat` when the
  resolved `frame` is NULL (standalone atomic).
- `setWindParams`: `if(!frame) return;` before `RwFrameGetLTM(frame)`.
- `CustomPipeRenderCB_PBR` (and `CustomPipeRenderCB_Xbox`): wrap the
  `pDirect` uploads in `if(pDirect){ ... } else { pipeUploadZeroPS(REG_directCol);
  pipeUploadZeroPS(REG_directDir); }`.

The camera/`eyePos` path was already guarded (`if(curCam){ if(camFrame){ ... } }`),
and the Xbox callback already guards `if(atomicFrame)` for `atomicLTM`. Same
pattern extended everywhere a frame/light can be NULL.

## Verification
- Build with `python tools/fix_build.py`; deploy.
- If a new `CRASH:` appears at a *different* address, repeat: the deref is a
  different NULL (frame/light/material) -- guard that site the same way.
- The VEH handler (`src/core/diagnostics.cpp`) is hardened (re-entrancy guard +
  SEH wrap) so a faulty stack-walk / `MiniDumpWriteDump` can no longer re-enter
  and freeze the process; it now logs a clean stack and lets the process exit.
