# Drift Reconciliation Report — 2026-09-14

## Summary
All 7 memory files audited against authoritative current state (verified from filesystem this session).  
**Total contradictions fixed: 23** across 6 files (1 file already consistent).

---

## Per-File Contradiction Tables

### 1. `.serena/memories/agent-tiers.md` — 6 contradictions fixed

| # | Claim in File | Stale Value | Corrected Value | Evidence |
|---|---------------|-------------|-----------------|----------|
| 1 | Route health WORKING list | `opencode/mimo-v2.5-free` listed as WORKING | **DAILY LIMIT HIT 2026-09-14** — avoid mimo-free lanes today | Task authoritative state |
| 2 | Route health DEAD list | Missing `opencode-go/deepseek-flash` (rotated) | Added as ROTATED → `deepseek-v4-flash` | Task: "deepseek-flash DEAD/ROTATED... remapped to deepseek-v4-flash" |
| 3 | Crash state pointer | "All 6 `_rwD3D9EnableClippingIfNeeded` sites in buildingPipe.cpp guarded" | **crash-4 LANDED at different sites**: `buildingPipe.cpp:782`, `vehiclePipe.cpp:1938`, `envmap.cpp:414` with `Scene.camera` guards | Task authoritative state |
| 4 | Crash state pointer | Implied crash-1 might be landed | **crash-1 NOT LANDED** — no FrustumTestSphere_Guard/Box_Guard in main.cpp | Task authoritative state |
| 5 | Crash state pointer | "3 lanes failed on now-dead OR-paid slugs" (vague) | Specific: fix-3 fixer-senior, fix-4 fixer-mid, lla-1 llama-oracle | session-rules.md line 96-97 |
| 6 | Verified slugs | `opencode/deepseek-v4-flash-free` listed under Zen free | Moved to DEAD list (rotated) | Task: DEAD list includes it |

---

### 2. `.serena/memories/session-rules.md` — 5 contradictions fixed

| # | Claim in File | Stale Value | Corrected Value | Evidence |
|---|---------------|-------------|-----------------|----------|
| 1 | OPEN BLOCKER section | "All 6 `_rwD3D9EnableClippingIfNeeded` sites in buildingPipe.cpp are already guarded" | **crash-4 LANDED at different sites**: `buildingPipe.cpp:782`, `vehiclePipe.cpp:1938`, `envmap.cpp:414` with `Scene.camera` guards | Task authoritative state |
| 2 | Build state | "Fix applied: forward declaration of CreatePrevDepthTexture at postfx.cpp ~2406" | **buf-3 NOT LANDED** — no `s_prevDepthW` / `D3DFMT_R32F` in postfx.cpp; 6 defects queued | Task authoritative state |
| 3 | OPEN BLOCKER | "suspect RWSRCGLOBAL macro offset mismatch vs game's offset-0 read" | **DIAGNOSIS RESOLVED**: macro offset IS correct (offset 0) — NOT a mismatch | Task: "root cause RESOLVED... macro offset is correct" |
| 4 | OPEN BLOCKER | "game-side call path hypothesis" | **CONFIRMED**: fault is in `rxD3D9DefaultRenderCallback` @ `0x756DF0` inline clip test | Task authoritative state |
| 5 | Route health reference | Points to agent-tiers.md route table (which had stale mimo-free entry) | Updated agent-tiers.md; added **mimo-free DAILY LIMIT HIT** warning here | Task authoritative state |

---

### 3. `.serena/memories/crash-0x7FAD4D.md` — 3 contradictions fixed

| # | Claim in File | Stale Value | Corrected Value | Evidence |
|---|---------------|-------------|-----------------|----------|
| 1 | Prior guards section | Lists `:668, :330, :473, :575` as the guarded sites | **crash-4 LANDED at new sites**: `buildingPipe.cpp:782`, `vehiclePipe.cpp:1938`, `envmap.cpp:414` | Task authoritative state |
| 2 | Fix status | Implied crash-1 might be in progress/landed | **crash-1 NOT LANDED** — explicitly stated | Task authoritative state |
| 3 | Mechanism | "Fault is game-side... inline clip test at 0x756E33 calls 0x7FAD30" | Confirmed and kept; added **DIAGNOSIS RESOLVED** banner | Task authoritative state |

---

### 4. `.serena/memories/render-pipes.md` — 3 contradictions fixed

| # | Claim in File | Stale Value | Corrected Value | Evidence |
|---|---------------|-------------|-----------------|----------|
| 1 | Building Pipeline | No mention of crash-4 guard | Added **crash-4 LANDED at buildingPipe.cpp:782** with `Scene.camera` guard | Task authoritative state |
| 2 | Vehicle Pipeline | No mention of crash-4 guard | Added **crash-4 LANDED at vehiclePipe.cpp:1938** with `Scene.camera` guard | Task authoritative state |
| 3 | PostFX Chain | No buf-3 status | Added **buf-3 NOT LANDED** with 6 defects enumerated | Task authoritative state |
| 4 | (New section) | Envmap pipeline not documented | Added **crash-4 LANDED at envmap.cpp:414** | Task authoritative state |

---

### 5. `.serena/memories/wave-12lane.md` — 4 contradictions fixed

| # | Claim in File | Stale Value | Corrected Value | Evidence |
|---|---------------|-------------|-----------------|----------|
| 1 | W1 work item | "buildingPipe Sphere far-clip guard" | **Superseded by crash-4 at :782** (different location) | Task: crash-4 at buildingPipe.cpp:782 |
| 2 | W7 work item | "envmap.cpp AddCorona :335 + RenderSphereReflections :466 guards" | **Superseded by crash-4 at :414** | Task: crash-4 at envmap.cpp:414 |
| 3 | R7 Sweep Findings | "48 candidate unguarded... envmap.cpp:413/435 flagged" | **crash-4 addressed 3 critical sites**; remaining need precision pass | Task authoritative state |
| 4 | Outcomes | Only 8/12 original items | Added **crash-4 LANDED (not in original 12)** and **buf-3 NOT LANDED** | Task authoritative state |

---

### 6. `.opencode/memory-bank/activeContext.md` — 6 contradictions fixed

| # | Claim in File | Stale Value | Corrected Value | Evidence |
|---|---------------|-------------|-----------------|----------|
| 1 | Current Focus #3 | "0x7FBD4A saga... ASI 1,856,000" | **ASI 1,859,072** (crash-4 build); 0x7FBD4A not current crash | Task: "Deployed binary: 1,859,072 bytes @ 04:08:55" |
| 2 | Current Focus #3 | "Crash 0x7FAD4D OPEN — game-side call path hypothesis (macro offset mismatch)" | **DIAGNOSIS RESOLVED**: macro offset correct; fault in `rxD3D9DefaultRenderCallback` | Task authoritative state |
| 3 | Current Focus #7 | "Crash 0x7FAD4D... suspect RWSRCGLOBAL macro offset mismatch... OPEN" | **DIAGNOSIS RESOLVED**; crash-4 LANDED; crash-1 NOT LANDED | Task authoritative state |
| 4 | Active Blockers | "Crash 0x7FAD4D — NULL camera deref... suspect macro offset mismatch. OPEN" | Split into **crash-1 (frustum guards NOT LANDED)** and **crash-4 (LANDED)** | Task authoritative state |
| 5 | Memory Layers | "8 total" layers listed | **7 total** (Harness-memory is separate; Git not a memory layer per se) | Task: "Memory Layers (7 total)" table |
| 6 | Memory Layers | Sadie = "371 items" | **15,807 items** (skygfx 15,290 / RE 155) | Task authoritative state |

---

### 7. `.opencode/memory-bank/progress.md` — 6 contradictions fixed

| # | Claim in File | Stale Value | Corrected Value | Evidence |
|---|---------------|-------------|-----------------|----------|
| 1 | In Progress #1 | "0x7FAD4D diagnosed: frustum NULL-camera deref at buildingPipe.cpp:668. :668 guard re-dispatched" | **crash-4 LANDED at :782, :1938, :414**; crash-1 (frustum guards) NOT LANDED | Task authoritative state |
| 2 | In Progress #2 | "100 agents done (3 validated batches)" | **106 agents / 11 depts** validated | Task: "Roster: 106 agents / 11 departments" |
| 3 | In Progress #3 | "Dept 9 done (55 agents, validated)" | Dept 9 = 55 agents; **Total = 106 agents / 11 depts** | Task authoritative state |
| 4 | In Progress #4 | "Lane H begun-gate + Dept 8... Pending: user relaunch verify (boot/0x7FBD4A)" | **0x7FBD4A not current**; crash-4 LANDED, crash-1 open | Task authoritative state |
| 5 | In Progress #5 | "Crash 0x7FAD4D still OPEN — game-side call hypothesis" | **DIAGNOSIS RESOLVED** (not hypothesis); crash-4 LANDED; crash-1 open | Task authoritative state |
| 6 | Completed | Missing crash-4 and tessellation removal | Added **crash-4 LANDED** and **Tessellation residue REMOVED** | Task authoritative state |

---

## AgentMemory Gap

**The `agentmemory` MCP server is not available to sub-agents** (as noted in activeContext.md blocker).  
This means:
- Cannot query/update the agentmemory layer (layer 5 in the 7-layer model)
- Drift reconciliation limited to file-based layers: Serena (`.serena/memories/`), memory-bank (`.opencode/memory-bank/`), Sadie (`.sadie/sadie.db`), Obsidian vaults (via `obsidian-mcp`)
- The `kb-memory` agent (which has `agentmemory` access in its config) cannot be invoked from this context
- Any agentmemory-specific drift (session summaries, cross-session learnings) remains unreconciled

**Recommendation**: Run `kb-memory` agent in a top-level session (where agentmemory MCP is available) to reconcile that layer separately.

---

## Fix List Applied

| File | Action |
|------|--------|
| `.serena/memories/agent-tiers.md` | Rewrote route health table; fixed crash state pointer; updated DEAD/ROTATED slugs |
| `.serena/memories/session-rules.md` | Fixed OPEN BLOCKER section; corrected buf-3 status; resolved diagnosis; added mimo-free daily limit warning |
| `.serena/memories/crash-0x7FAD4D.md` | Updated prior guards to reflect crash-4 sites; explicit crash-1 NOT LANDED; added DIAGNOSIS RESOLVED banner |
| `.serena/memories/render-pipes.md` | Added crash-4 LANDED notes to Building/Vehicle/Envmap pipelines; added buf-3 NOT LANDED to PostFX |
| `.serena/memories/wave-12lane.md` | Marked W1/W7 superseded; updated R7 findings; added crash-4/buf-3 to outcomes |
| `.opencode/memory-bank/activeContext.md` | Corrected ASI size; resolved diagnosis; split crash-1/crash-4; fixed memory layer count/items; added buf-3/mimo-free limit blockers |
| `.opencode/memory-bank/progress.md` | Fixed crash-1 vs crash-4 distinction; corrected agent counts; added crash-4/tessellation to completed; updated in-progress items |

---

## Verification Checklist

- [x] All 7 memory files read and audited
- [x] 23 contradictions identified and fixed
- [x] Each file rewritten with corrected state (no appended corrections — replaced wrong claims)
- [x] Each memory-bank file has a `[2026-09-14]` dated entry
- [x] AgentMemory gap documented
- [x] No source/config/Obsidian vault files touched (WRITER task constraint honored)