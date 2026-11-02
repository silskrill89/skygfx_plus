# MD Reconciliation — 2026-10-31

Purpose: make the doc set consistent with `docs/FEATURE_LIST.md` (source of truth, authored
2026-11-01). The FEATURE_LIST author flagged 6 conflicts between it and older MDs. This doc
records what was changed where, and the two contradictions that required documentation instead of
an edit (source files off-limits).

---

## 1. Edits applied

### 1.1 FUTURE_FEATURES.md (root)
Was claiming ImGui menu + normal-map SDK were "excluded from build" — **WRONG**:

| Entry | Claim before | Now |
|---|---|---|
| ImGui Debug Menu | "Code exists in debugmenu_ui.cpp but excluded from build" | **LANDED.** `src/core/debugmenu_ui.cpp` is a compiled TU (~4,300 LOC; `premake5.lua:233`), plus `debugmenu_theme.cpp`. Live: All Settings tab (235 rows, search/reset/copy-as-INI), diagnostics, Atmospheric header (13 sliders), mode chips + MODE pill rows (Wave 8). Open parser/save/clamp defects tracked in `docs/plans/2026-10-24-imgui-settings-coverage.md`. |
| Normal Map Plugin Integration | "Code exists in normalmap.cpp/normalmap_plugin.cpp but excluded from build" | **GROUNDWORK LANDED; `_n` wiring deferred-last.** `src/texdefs.cpp` (texdb+normalmap+plugin registry merge; normalmap TUs no longer exist separately), `src/normmap_stubs.cpp` landed, `rpnormmap.lib` LINKED (`premake5.lua:353`). Final txd `_n` wiring into shaders = LAST feature per TODO.md golden rule §C. |

Verified against: `premake5.lua` (lines 16-134 commentary + 233/353), TODO.md §C, FEATURE_LIST §11.

### 1.2 PLAN.md (P1.3, ~line 38)
"`P1.3` Game-specific preset framework (DEFER to 64-bit build) — Not started" was **STALE**:
presets.cpp + 8-chip picker LANDED in Wave 8.

Rewritten to: SA/IV portion LANDED (`src/core/presets.cpp` whole-Config `ApplyPreset` with
mode-latch desync guard; 8-chip ImGui picker PS2/Xbox/PC/Mobile/GTA IV/GTA V/Modern/Custom in
`debugmenu_ui.cpp`; `gamePreset` INI key). REMAINING scope — full GTA III/VC/LCS/VCS/VI preset
sets + menu frameworks — still deferred to the 64-bit build (consistent with FEATURE_LIST §2 and §11).

### 1.3 CODEBASE_DOCUMENTATION.md
Described the old flat `src/*.cpp` layout ("src/main.cpp 2266 lines"), `build\build.bat`,
and the 421 KB `bin\Release\skygfx.dll`. Not rewritten (kept as historical record); a prominent
**SUPERSEDED banner** was added at the top pointing to docs/FEATURE_LIST.md + AGENTS.md +
docs/plans/2026-10-31-source-inventory.md, and listing the changed facts:
`src/core|render|entities|peds|extras|rw` layout (86 files, ~84.6k LOC), `tools/fast_build.py`
build, `build\Release\skygfx.dll` → deployed `skygfx.asi` (~2.5 MB).

### 1.4 docs/FEATURE_LIST.md — conflict rows updated
- §3.7 Corona z-test row: now cites the exact live lines (below) + points to this doc.
- §3.3 pedAmbientFloor row: resolution recorded (below), row rewritten from CONFLICT to resolved.

### 1.5 docs/plans/README.md (plan index)
Added/updated entries for all 2026-10-30/31 docs not yet indexed:
- `2026-10-30-water-overhaul-spec.md` (ACTIVE) — + note it supersedes water sections of the
  2026-10-24 water diagnosis docs.
- `2026-10-30-sep3-correlation/` folder — indexed with **actual count = 6 files** (see §4).
- `2026-10-31-source-inventory.md` (ACTIVE).
- `2026-10-31-peds-pipe-design.md` (ACTIVE, design only).
- `docs/FEATURE_LIST.md` (ACTIVE, source of truth) — added as the first new row.
- Test/baseline findings: `../baseline/2026-10-30-test2-0438/findings.md` (ARCHIVE forensics),
  `../baseline/2026-10-31-stripped-ini-bisect/findings.md` (ACTIVE — proves shields ARE the repairs),
  `../baseline/2026-10-31-test4/findings.md` (ACTIVE — user-INI test, `pipeChainEnable=0` kills water core).
- `2026-10-31-md-reconciliation.md` (this doc) — added.
- "Last updated" bumped; crash-resume read order step 4 now starts with FEATURE_LIST + the 10-31 docs.

---

## 2. coronaZtest contradiction — DOCUMENTED, NOT FIXED (source owned by another lane)

**Contradiction:** `src/core/main.cpp:374-380` comment says coronaZtest is PARSE-ONLY/retired
("SilentPatchSA owns the corona z-test at 0x6FB17C... no bytes written") — but the CCoronas::Render
shield code actively reads it.

**Live-on-disk reading sites (line drift note):** source-inventory D11 cites main.cpp:4755/:4829.
Current on-disk reads are at:
- `main.cpp:4876` — `int ztTarget = (config && config->coronaZtest == 0) ? FALSE : TRUE;`
  (ZTEST target derived from config in the corona blend/ztest shield)
- `main.cpp:4950-4955` — `[CoronaZ] forced ztest=... (coronaZtest=%d)` dbglog using
  `config->coronaZtest`

(+~121 lines of drift between the inventory and the current file; "main.cpp:4755/:4829" are now
inside the explanatory comment block at ~4783-4790 describing the same shield.)

**Resolution path for the owning lane:** do NOT trust the :374-380 "retired" comment. Either the
comment is stale (shield superseded SilentPatch ownership and piracy/PARALLEL parse-only mode no
longer holds) or the shield accidentally reads retired config. Verify generation and INI path
(`SkyGfx.coronaZtest` clamp [-1..1] in readIni) before touching either block; keep SilentPatch
compatibility in mind (no raw byte writes — shield is render-state level only).

**Status:** flagged. FEATURE_LIST §3.7 row updated to cite :4876/:4950 and this doc.

---

## 3. pedAmbientFloor conflict — RESOLVED: current truth = DEAD-BUT-PARSED

**Conflict:** `.opencode/memory-bank/activeContext.md` (Wave 5) said landed; source-inventory
§2-D4 said dead-but-parsed.

**Current truth (verified in source):** the Wave 5 feature was the ped-ambient
`max(timecycle)` lift; the separate `pedAmbientFloor` hue-preserving lift was removed in the W3
revert. `pedAmbientFloor` now has ZERO render consumers:
- Parsed+clamped: `main.cpp:7102-7104` (comment at :7284 explicitly says "the W3 revert — the
  debug menu still references config->pedAmbientFloor, so [field kept]")
- Saved/EMIT round-trip: `main.cpp:7613`, `:8243`
- Menu references: `debugmenu_ui.cpp:750` (live slider rFn) and `:1506` (default table 0.20)
- Hue-preserving lift deliberately NOT used at the pAmbient override site: `main.cpp:2185-2225`
  (source-inventory P7 confirms "pedAmbientFloor intentionally NOT used here (D4)")

**Disposition:** field/key/slider kept as-is because `debugmenu_ui.cpp` references it; the
field is dead by design until the owner either re-wires the lift or executes the source-inventory
D4 delete verdict (config field + INI key + slider + EMIT). NO source edit made here.

**Effect:** activeContext's "pedAmbientFloor landed" claim is stale; FEATURE_LIST §3.3 row now
reads: ped ambient max() ✅ landed (Wave 5), pedAmbientFloor dead-but-parsed, resolved here.

---

## 4. sep3-correlation file count — actual: 6 files (NOT 7)

The inter-lane prompt said 7 files; `docs/plans/2026-10-30-sep3-correlation/` contains **6**:

1. `git-render-pipes.md`
2. `git-alpha-corona.md`
3. `git-chars-lighting.md`
4. `git-texdefs-ground.md`
5. `ini-diff-exposure.md`
6. `shader-diff.md`

FEATURE_LIST §12 already said "6 files on disk, NOT 7" — verified correct. If a 7th sep-3
artifact (likely a missing `git-postfx.md` or similar) was intended, it was never written;
count all downstream references by content, not by the original prompt.

---

## 5. Remaining contradictions / open flags

| # | Contradiction | Where | Owner / action |
|---|---|---|---|
| R1 | coronaZtest "parse-only" comment vs active shield reads | main.cpp:374-380 vs :4876/:4950 | Owning corona lane — verify generation; FEATURE_LIST §3.7 carries the flag (§2 above) |
| R2 | activeContext "pedAmbientFloor landed" wave-5 record | memory-bank | No edit made (bank owned by its lane); FEATURE_LIST §3.3 is authoritative now |
| R3 | WORKFLOW_PLAN.md:359/377 still says III/VC menu "blocked by P1.3 preset framework" — P1.3 SA/IV part landed | WORKFLOW_PLAN | Untouched this pass (out of the 6-item scope); treat FEATURE_LIST §10 row as truth |
| R4 | Older 2026-10-25..10-29 plan docs not yet rows in docs/plans/README.md (indexed via FEATURE_LIST §12 instead) | plan index | Accepted: this pass only indexed 10-30/31 + FEATURE_LIST per scope |
| R5 | Feature-list line-note: `pipeChainEnable=0` in test-4 INI kills the water pipe core (FFP fallback) — user-config landmine, not a doc bug | docs/baseline/2026-10-31-test4 | For the water lane's "correct core without shields" wave |

No source code, build, or commit changes were made in this pass.
