#pragma once

// SA CMirrors composite pass — mirror distortion fix (src/render/mirrorPass.cpp).
// Intercepts CMirrors::RenderMirrorBuffer's single call site (0x53EABA, CMain::
// Render) and ALWAYS calls the game's function first (full parity: 8-track Im3D
// screens, trailing stadium quad, state baseline). When SkyGfx/mirrorDistortionFix
// is on AND the game took its plain fullscreen else-branch (CMirrors::TypeOfMirror
// != MIRROR_TYPE_NONE — first consumer of that envmap.cpp:554 global — no
// STAIRS/bFudgeNow, camera outside the (1003,-42,216)+50 m stadium-quad block),
// it OVERDRAWS that quad through Mirror_Composite.ps: per-pixel projective fetch
// of CMirrors::pBuffer from the fragment NDC (FusionFix mirrorPS1.asm port).
// Everything else — fix off, no mirror, stadium screens — falls through to the
// game's own output untouched.
void mirrorPass_init(void);
