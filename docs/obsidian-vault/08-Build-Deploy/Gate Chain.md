---
tags: [build, gate, release, binary-identity, deploy]
created: 2026-11-10
updated: 2026-11-10
---

# Build Gate Chain

> [!info] Authoritative run-logs
> `docs/plans/2026-11-07-build-gate-checklist.md` is the source of truth for every gate run.
> This note is the vault-side ledger of identities and protocol.

## Identity ledger

| Gate | Date (2026) | Bytes | md5 | Carries |
|---|---|---:|---|---|
| W0 | 11-09 | 2,602,496 | `5854020218cf222b32670c08aaca4541` | N-registry wave 2 + main.cpp campaign |
| W0.1 | 11-09 04:54 | 2,602,496 | `58b2190be48c7e5de298bc7e3ccc5c0c` | Sprite/font flush-state perfection; CloudRegionFlush fast path |
| W0.2 | 11-09 14:11 | 2,608,640 | `80ebc9a9972910fb7e0c78907d31acaa` | 8-writer pre-build wave; frame-end `StateBlockEnforceCanonical`; texdefs cross-dict sweep |
| W0.3 | 11-09 | 2,614,784 | `e463506abd4fe2e5d81479fa02d21366` | noise-based variation + motion_fx/wind + main.cpp campaign |
| W0.4 | 11-09 | 2,616,320 | `516d904c3baac849f7bf298967fe98f8` | L1–L5 wave: lever floor, CoronaWindow depth net, texdefs fail-closed, VCS chokepoint, crash §8 |
| W0.5 | 11-09 late | 2,616,832 | `bb5d1230aba68348cb7de28f0201f7a3` | `config.cpp` + `render_callbacks.cpp` extraction; R1–R9 redundancy fixes |
| **W0.6** | — | — | — | **PENDING** — blocked on the address-hygiene sweep |

Every gate deploy target is `E:\games\gtasa_skygfx_plus\skygfx.asi`. The build's target directory
**is** the game directory, so `--no-deploy` skips only a redundant copy; the produced artifact is
the deployed artifact. Each gate keeps a md5-verified backup at `.cache/skygfx.w0Ngate.asi`.

## Protocol

1. **Pre-build census** — `git status --porcelain`; confirm the never-commit set is ignored and
   nothing is staged: `n1`, `qc`, `skygfx_plus_expIV.7z`, `_backup/`, `PLAN.md`,
   `tools/*tabshell*.ps1`, `tools/*cloudstore*.ps1`.
2. **One integrated build** — orchestrator only; lanes never build:
   `python tools/fast_build.py --no-deploy`.
   Shader validation must be `113/113`; MSBuild may show the pre-existing C4005/C4805 warnings,
   but must be **0 errors**.
3. **Binary string-verify** — token census against the deployed ASI. The pattern script lives in
   `%TEMP%` / `.cache` (for example `.cache/w03_verify.py`), never in the repo. Token counts so
   far: W0 18, W0.2 29, W0.3 35, W0.4 39, W0.5 43.
4. **Deploy record** — bytes, md5, mtime, backup path.
5. **Runtime receipts + user retest** — watch-lists live in the wave records and
   [[Status 2026-11-10]].
6. **Commit only after retest passes**, never the census list.

## W0.6 additions

Verify these new tokens once W0.6 is built:

`[texflush]` · `cloud_split_restore` · `AUTOEXP_DEGENERATE` · `[PF] VCS RT parity` ·
`radvcs_srcmismatch` · `blurvcs_srcmismatch` · `[Softbody] stale-write skip` ·
`[ragdoll] gib retire` · `n9_ped_remap` · `n9_pedcb_remap` · `bld_zw_guard` ·
`bld_opaque_nopark` · `far_fog` one-shot.

## Gate-fix lessons

- **W0.2:** a missing `..\src\core\shadows.cpp` `ClCompile` entry produced `LNK2019
  shadows_installHooks`. Premake globs do not update the pre-generated vcxproj.
- **W0.3:** `motion_fx.cpp:91` called `NoiseMaterialVariation` unqualified while it lives in
  `namespace SkyNoise`. Cross-lane pinned APIs must pin the namespace too.
- **W0.5:** the `dbglog_once("cloud_texraster")` token was split into
  `cloud_texraster_passthru` / `cloud_texraster_normal`; log recipes must match by suffix now.

## Related

- [[Status 2026-11-10]] — current wave content and watch-list
- [[Build System]] — toolchain and `fast_build.py` / `fix_build.py`
- [[Current Architecture (Wave 20)]]
- `docs/plans/2026-11-07-build-gate-checklist.md`
