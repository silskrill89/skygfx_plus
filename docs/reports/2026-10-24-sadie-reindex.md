# Sadie re-index — 2026-10-24

**Scope:** `.sadie/` only (scripts/config + this report). No source, `.serena`, or
`.opencode` files were modified.

## Why

`.sadie/sadie.db` was 21,884,928 B, last written **Oct 15** — 9 days behind the
repo. Root causes found and fixed:

1. `sadie.py init` only indexed `.serena/`, `.mimocode/skills`, `.opencode/commands`
   and `.sadie/*.md`. **No** `docs/`, `memory-bank/`, `docs/plans/`,
   `docs/research/`, science pillars, wiki, or source symbols.
2. `INSERT OR REPLACE` had no uniqueness constraint to conflict on → every
   `init` re-run appended duplicate rows (640 dup rows / 151 groups).
3. Canonical `shader_registers` and `bindings` were empty (0 rows) while the
   `*_legacy` tables held 526/437 rows. `init` never populated the canonical
   tables; `ask` still queried legacy column names.
4. Vault sync stored only the **first 4,000 chars** of each note, so terms deep
   in large docs (e.g. `texalpha` in `Raw/RenderWare User Guide Vol1.md` at
   offset ~271k) were unfindable.
5. The GTAMods wiki path (`H:/wikis/gtamods/rendering`) is on an **offline
   drive** (H: does not exist on this machine). No `.sadie` script referenced it
   before, but the project docs did.
6. `sadie_extract.scan_bindings` crashed on computed sampler slots
   (`dev->SetTexture(i, ...)` → `int('i')`), aborting a full rebuild.
7. No FTS index existed; search was a full-table `LIKE` scan.

## Paths fixed

| Source | Status before | Action |
|--------|---------------|--------|
| `H:/wikis/gtamods/rendering` | **MISSING** (H: drive absent) | primary kept in config; automatic fallback to `<RE vault>/GTAMods Wiki` (59 notes). `verify` reports the offline primary as a WARN, not a failure. No fake dirs created. |
| `E:/dev(dave)/SKYGFXPLUS_DOCS` | present (15,279 md) | synced, full bodies |
| `E:/dev(dave)/GTA SA Reverse Engineering Documentation` | present (156 md) | synced; 59 wiki-mirror notes moved to the `wiki` layer to avoid double indexing |
| `docs/research/youtube/` | present (1 transcript) | indexed as `research` |
| `memory-bank/.cache/science-pillars/` | present (4 md) | indexed as `science-pillars` |
| `docs/plans/` | present (15 md, growing) | indexed as `plans` |
| `memory-bank/`, `.opencode/memory-bank/` | present | indexed as `memory-bank` |
| `.serena/memories/` | present (25 md) | indexed as `serena` |
| `src/**/*.{cpp,h,hpp,c,inl}` | not indexed | indexed as `code` (112 files) so symbol queries resolve |

All path constants now live in **`.sadie/sadie_config.py`** (single source of
truth). `tools/skytool.py` / `tools/memory.py` still hard-code
`H:/wikis/gtamods/rendering`, but they are outside the allowed edit scope; they
will keep reporting the wiki as missing while H: is offline.

The stale `locations` registry (OSA/O3DE paths that do not exist in this repo)
was replaced with the real project, game, vault and wiki paths (18 entries).

## Schema / idempotency fixes

- Normalised every `source_file` to forward slashes and removed **640 duplicate
  rows** (151 groups) left by previous `init` runs.
- Added `UNIQUE INDEX ux_items_source_section ON items(source_file, COALESCE(section,''))`
  and an explicit upsert helper → re-running `init` is now idempotent (verified:
  second run produced identical counts, 0 duplicate groups).
- Added FTS5 external-content table **`items_fts`** (title/content/tags/source_file)
  with `AFTER INSERT/UPDATE/DELETE` triggers, plus a rebuild-on-create guard
  (an empty external-content index + trigger delete raises
  `database disk image is malformed`; rebuild fixes it).
- `init` now populates the canonical extractor tables: **779 shader_registers**
  and **759 bindings**; added canonical indexes
  `idx_sr_canon_file`, `idx_sr_canon_reg`, `idx_bind_canon_reg`, `idx_bind_canon_file`.
- Rewrote the `ask` verb to the canonical columns (`reg`, `entry`, `symbol`,
  `value`, …) — it previously queried legacy names and would have crashed.
- Vault sync: content hash now includes a format version, full bodies stored
  (cap 400k), deleted/renamed notes pruned via `sync_state`, and the wiki mirror
  excluded from the RE vault walk.
- `clear_project_items()` wipes only the derived project layers before re-index
  (vaults are handled by `sync_state`), so stale docs disappear.

## Before / after

| Layer | Before (Oct 15 DB) | After |
|-------|-------------------:|------:|
| `vault/re` | 155 | 97 |
| `vault/skygfx` | 15,290 | 15,279 |
| `serena` | 682 (with dupes) | 201 |
| `memory-bank` | 0 | 141 |
| `science-pillars` | 0 | 42 |
| `research` | 0 | 2 |
| `plans` | 0 | 179 |
| `docs` | 0 | 850 |
| `wiki` | 0 | 318 |
| `code` | 0 | 112 |
| `meta` | 137 (with dupes) | 36 |
| **items total** | **16,264** | **17,257** |
| duplicates | 640 rows / 151 groups | **0** |
| FTS rows | — (no FTS) | 17,257 |
| shader_registers | 0 (526 in `_legacy`) | 779 |
| bindings | 0 (437 in `_legacy`) | 759 |
| DB size | 21,884,928 B | 63,934,464 B (high-water mark; VACUUM compacts) |

The `vault/re` drop is intentional: 59 GTAMods-mirror notes now live in the
`wiki` layer (155 − 59 + 1 new note = 97). SkyGFX dropped 11 notes that were
deleted/renamed on disk. DB growth is full note bodies + FTS + 112 code files.

## Acceptance queries

`python .sadie/sadie.py search "<q>"` (FTS5, token/prefix matching):

| Query | Resolves to |
|-------|-------------|
| `texalpha` | `code:src/render/texalpha.h` (+ `vault/skygfx:Raw/RenderWare User Guide Vol1.md`, term at offset 271k — previously truncated away) |
| `CoronaWindow_Close` | `.opencode/memory-bank/progress.md` (+ `code:src/render/depthhook.cpp`) |
| `pipeApplyWorldBaseline` | `docs/plans/2026-10-24-imgui-settings-coverage.md` |
| `Winamp` | `docs/plans/2026-10-24-imgui-aero-theme-research.md` (created concurrently by another agent today) |
| `alpha reversal` | `docs/plans/2026-10-21-alpha-reversal-findings.md` (`Alpha-Reversal` hyphen form) |
| `forward plus fallback` | `docs/obsidian-vault/SkyGFX-Pipeline/Roadmap.md` |

`gotchas`: 717 raw rows, 0 empty/broken entries after filtering (was printing
831; wiki reference notes no longer auto-flag as critical). Default output is
capped at 50; `--all` shows every row.

## Verify command

Added `python .sadie/sadie.py verify` (also `python .sadie/verify.py`). Checks:
DB exists, required tables, non-zero row count per required layer, FTS row parity,
duplicate groups, `sync_state` orphans, the 6 sanity queries, and every configured
source path. Exit 0 = pass, 1 = fail.

```
=== sadie verify ===
  [PASS] database: E:\dev(dave)\skygfx_plus_expIV\.sadie\sadie.db (63934464 bytes)
  [PASS] tables: all present
  [PASS] layer vault/re: 97 rows
  [PASS] layer vault/skygfx: 15279 rows
  [PASS] layer serena: 201 rows
  [PASS] layer memory-bank: 141 rows
  [PASS] layer science-pillars: 42 rows
  [PASS] layer research: 2 rows
  [PASS] layer plans: 179 rows
  [PASS] layer docs: 850 rows
  [PASS] layer wiki: 318 rows
  [PASS] fts index: 17257/17257 rows
  [PASS] duplicates: 0 groups
  [PASS] sync_state: 0 orphans
  [PASS] query 'texalpha': code:src/render/texalpha.h
  [PASS] query 'CoronaWindow_Close': .opencode/memory-bank/progress.md
  [PASS] query 'pipeApplyWorldBaseline': docs/plans/2026-10-24-imgui-settings-coverage.md
  [PASS] query 'Winamp': docs/plans/2026-10-24-imgui-aero-theme-research.md
  [PASS] query 'alpha reversal': docs/plans/2026-10-21-alpha-reversal-findings.md
  [PASS] query 'forward plus fallback': docs/obsidian-vault/SkyGFX-Pipeline/Roadmap.md
  [WARN] path wiki (primary): missing: H:\wikis\gtamods\rendering
RESULT: PASS (0 failed, 1 warnings)
```

`verify` ignores `docs/reports/*` when resolving the sanity queries so this
report cannot satisfy its own checks; before this report existed,
`CoronaWindow_Close` resolved to `.opencode/memory-bank/progress.md` and
`code:src/render/depthhook.cpp`.

## Re-run after future doc writes

```bash
python .sadie/sadie.py init
python .sadie/sadie_vault_sync.py
python .sadie/sadie.py verify
python .sadie/sadie.py stats
```

`init` is safe to re-run (idempotent upserts; project layers rebuilt). The vault
sync skips unchanged notes by hash and prunes deleted ones. No VACUUM is run, so
the file keeps its high-water mark after repeated rebuilds.

## Concurrency note

Other agents were writing `docs/plans/*` throughout this work (the `Winamp`
query only resolved because a plan landed mid-run). A final
`init` + `sadie_vault_sync.py` pass was executed **after** this report was
written; its output is appended below.

### Final end-of-work pass

Executed after this report was written (so the report itself and any
concurrent `docs/plans/*` edits are included):

```
$ python .sadie/sadie.py init
  cleared 1861 stale project rows
  [wiki] primary offline, using mirror: E:\dev(dave)\GTA SA Reverse Engineering Documentation\GTAMods Wiki
Indexed 17257 items:
  ...
  docs                 850
  plans                179
  shader_registers:    779   bindings: 759
Database: .sadie\sadie.db (63934464 bytes)

$ python .sadie/sadie_vault_sync.py
wiki primary offline (H:\wikis\gtamods\rendering); mirror indexed separately: ...\GTAMods Wiki
Syncing vault: re (...)
  +0 added, ~0 updated, =97 unchanged, -0 pruned
Syncing vault: skygfx (...)
  +0 added, ~0 updated, =15279 unchanged, -0 pruned
vault sync complete: +0 added, ~0 updated, =15376 unchanged, -0 pruned; vault items=15376, total items=17257

$ python .sadie/sadie.py stats
  items: 17257 rows
  items_fts: 17257 rows
  vault/re 97 | vault/skygfx 15279 | serena 201 | memory-bank 141
  science-pillars 42 | research 2 | plans 179 | docs 850 | wiki 318
  code 112 | meta 36 | duplicate groups: 0

$ python .sadie/sadie.py verify
RESULT: PASS (0 failed, 1 warnings)   # warning = offline H: wiki primary
$ echo $?
0
```

All six acceptance queries return a real path; all required layers are non-zero;
duplicate groups = 0; `sync_state` orphans = 0. Counts drift upward as
concurrent agents add docs (`plans` moved 168 → 179 during this task).
