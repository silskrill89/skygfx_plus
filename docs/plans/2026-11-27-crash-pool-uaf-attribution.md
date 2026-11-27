# 2026-11-27 — Crash 0x005D97E6 pool-UAF attribution backlog (early freer + Renewed120 correlation)

**Lane:** librarian / docs (read-only recon + this one doc write). No source edits, no builds, no
game runs, no commits.
**Scope:** the two attribution questions left open after the root-cause + FIX A/B/C landing:
(a) is there an EARLY freer of the spec pool / its slots before `DestroyPipe`, and (b) does the
`Renewed120.asi` NULLDEREF burst correlate with the fatal. Plus (c) is FIX A sufficient prevention.
**Artifacts parsed this lane (2026-11-27, read-only):**
`E:\games\gtasa_skygfx_plus\skygfx_crash_2026-11-25_120822.dmp` (390,601 B),
`…_145608.dmp` (396,020 B), `…_2026-11-26_225401.dmp` (392,733 B),
`E:\games\gtasa_skygfx_plus\skygfx_dbg.log`, `skygfx_dbg.prev.log`,
`E:\games\gtasa_skygfx_plus\gta_sa.exe` (on-disk image, imagebase `0x400000`), `src/core/crashhandler.cpp`.

---

## 1. KNOWN (every row carries a source)

| # | Fact | Source |
|---|---|---|
| K1 | Crash site `0x5D97E6` = `mov eax,[esi+4]` — the `data->Texture` load (texture is `CustomSpecMapPipeMaterialData +0x4`) inside `pluginSpecMatDestructorCB` (`0x5D97D0`) | `src/core/crashhandler.cpp:242-244`, `:265`; byte-verified on disk by this lane: `0x5D97E6 = 8B 46 04` |
| K2 | Pool `0xC02D30` = `CPool<CustomSpecMapPipeMaterialData>*`; 4096 slots × 8 B = the 32 KB block; the CB's own index math (`sar esi,3` after `sub esi,[eax]`) matches 8-byte stride | `crashhandler.cpp:247`, `:268-270`, `:867`; on-disk bytes at `0x5D97FD` = `A1 30 2D C0 00 2B 30 8B 50 04 C1 FE 03` (this lane) |
| K3 | Free site: `CCustomCarEnvMapPipeline::DestroyPipe` entry `0x5DA130` (`A1 28 2D C0 00`), reached by the 5-byte tail-jmp `E9 34 46 00 00` at `0x5D5AF7` from `CCarFXRenderer::Shutdown` (`0x5D5AD0`); NOP-patch constants match the running code exactly | `crashhandler.cpp:863-866`, `:888-891`; both byte-checked on disk this lane (`0x5D5AF7 = E9 34 46 00 00`, `0x5DA130 = A1 28 2D C0 00`, `0x5D5AD0 = 56 BE D0 2B C0`) |
| K4 | Ordering: `CGame::Shutdown` runs `CCarFXRenderer::Shutdown → DestroyPipe` FIRST (deletes + nulls the pool), THEN `RsEventHandler(rsRWTERMINATE)` destroys surviving materials whose `specData` still dangles — a quit-time UAF, not a gameplay one | `crashhandler.cpp:245-249` (prose, "Root cause (proven)"); the `0x53C900` entry address for `CGame::Shutdown` comes from prior session recon — prologue readable on disk (`56 57 B9 40 42…`) but identity NOT re-verified this lane |
| K5 | Both 11-25 minidumps: `ExceptionCode=0xC0000005`, `ExceptionAddress=0x5D97E6`, READ, `ExceptionInformation=[0, fault]` with `fault=0x771E2FC` / `0x7D3DFB4` — both ≡ 4 (mod 8), i.e. exactly a `slot+4` read; implied slot bases `0x771E2F8` / `0x7D3DFB0` are 8-aligned genuine pool-slot-shaped addresses | parsed `MINIDUMP_EXCEPTION_STREAM` of `skygfx_crash_2026-11-25_120822.dmp` / `_145608.dmp` (this lane, 2026-11-27) |
| K6 | The two 11-25 dumps are the pre-fix fatals: FIX A/B landed in commit `5816147` at 2026-11-26 21:50:05, i.e. AFTER both dumps (11-25 12:08:22 / 14:56:08) | `git log --format='%h %ci' -1 5816147` |
| K7 | Sibling quit-UAF (FIX C): dump `skygfx_crash_2026-11-26_225401.dmp` = `ExceptionAddress=0x810CB2` READ fault `0x1E650290`, logged as `CRASH[1]: READ at 0x00810CB2 … mod=gta_sa.exe+0x410CB2` — from the 22:52 session whose build stamp is `built=Nov 26 2026 22:34:01`, i.e. contains `5816147` (21:50) but NOT `15fbd64` (FIX C, 23:40:16) | parsed minidump (this lane) + `skygfx_dbg.prev.log:7684-7686` + `skygfx_dbg.prev.log:1` (build stamp) |
| K8 | FIX A = NOP exactly the 5-byte tail-jmp at `0x5D5AF7` (byte-verified both targets before writing; pattern mismatch ⇒ ONE receipt + patch SKIPPED, never blind-patched). Install: CRT static + idempotent second call from `diag_installVEH` | `crashhandler.cpp:860-966` (section), `:888-891` (constants), `:898-943` (`CrashHandler_TrySpecPoolGuard`), `:946-966` (`CrashHandler_InstallSpecPoolGuard`), `:974-977` (second call site) |
| K9 | FIX B = conjunctive VEH skip: AV only AND `site==0x5D97E6` AND (pool ptr `0xC02D30==0` OR ESI outside `[base,base+n*8)` OR byteMap bit `0x80` bEmpty set); probe faults ⇒ NO skip; recovery redirects EIP to shared CB epilogue `0x5D9820` | `crashhandler.cpp:240-270` (contract), `:275-306` (`crashSpecPoolSkipSafe`), `:536-563` (VEH dispatch), `:559-561` (redirect) |
| K10 | FIX C = conjunctive VEH skip for the `0x810CB2` MatFX data-UAF (5 conjuncts incl. return-into-dtor `[esp+0x10]==0x810C78` and a confirmed-freed `data+0x14`); recovery `Eip=0x810C9B`, `Esp+=0x18`. Its own comment says **"early freer unidentified"** for the MatFX struct — same ordering family | `crashhandler.cpp:307-345` (contract, esp. `:318`), `:351-374` (`crashMatFxSkipSafe`), `:565-593` (VEH dispatch) |
| K11 | Commit trail: `5816147` (de-merge + FIX A/B, 2026-11-26 21:50:05) → `15fbd64` (FIX C, 2026-11-26 23:40:16) → `29da737` (W0 + per-module crash budget, 2026-11-27 07:20:07) | `git log` this lane |
| K12 | Receipts live and byte-stable: `[specpool_guard] 0x5D5AF7 tail-jmp neutralized (5x NOP)…` once per session; `[specpool_skip]`, `[matfx_uaf_skip]` throttled; W0 census carries `specpool_skip` + `matfx_uaf_skip` as required tokens | `skygfx_dbg.log:2` (23:41:10.717), `skygfx_dbg.prev.log:2` (22:52:05.513); `crashhandler.cpp:960-965`, `:553-556`, `:582-585`; `docs/plans/2026-11-07-build-gate-checklist.md:140`, `:173` |
| K13 | Renewed120 burst, session 2026-11-26 23:42: `CRASH[1]…CRASH[20]` all `READ at 0x6545D41D … mod=Renewed120.asi+0xD41D NULLDEREF`, `23:42:01.589 → 23:42:01.732` (143 ms) — matches the budget-defect forensics window `2026-11-26 23:41:10 -> 23:42:04` and the `20x … inside a 143 ms burst` comment | `skygfx_dbg.log:2312-2350`; `crashhandler.cpp:378-384` |
| K14 | That same bursted session QUIT CLEAN: `texdefs shutdown complete` at 23:42:04.648 → `[STATS] … veh_catch=168 av_read=20 … guard_fault=0` → `DLL_DETACH complete` at 23:42:04.659, on a build carrying FIX A (`[specpool_guard]` receipt at session start, `built=Nov 26 2026 22:34:01`) | `skygfx_dbg.log:1`, `:2`, `:2487-2496` |
| K15 | Minidump naming/destination: `<exe dir>\skygfx_crash_YYYY-MM-DD_HHMMSS.dmp`, written from the unhandled filter with `MINIDUMP_EXCEPTION_INFORMATION` (ExceptionPointers passed through) | `crashhandler.cpp:1259`, `:1262-1310` (esp. `:1272-1276`, `:1283-1296`), `:1316-1357` (filter) |
| K16 | Game/image identity: `E:\games\gtasa_skygfx_plus\gta_sa.exe` (Hoodlum 1.0 US, imagebase `0x400000`, `.HOODLUM` section present); loaded `ntdll.dll` ts `0x701FA718` matches on-disk `SysWOW64\ntdll.dll` (size 1,700,440) | PE header parse this lane; per `docs/plans/2026-10-25-clouds-ffp-audit.md:6` (Hoodlum 1.0 US) |

### 1b. New this lane — the `0xC02D30` writer census (evidence for UNKNOWN (a))

Read-only byte scan of the whole on-disk image for the absolute address operand `30 2D C0 00`
(2026-11-27). Exactly **6 references**, classified by the preceding opcode:

| VA | Preceding bytes | Class | Note |
|---|---|---|---|
| `0x5D97FE` (insn `0x5D97FD`) | `A1 30 2D C0 00` | reader `mov eax,[0xC02D30]` | == `kSpecCbPoolLoad` (`crashhandler.cpp:266`) — CB slot-index math |
| `0x5D9852` | `8B 0D 30 2D C0 00 … E8` | reader `mov ecx,[0xC02D30]; call …` | CB region, pool-pointer pass to a call |
| `0x5D98D2` | `8B 0D 30 2D C0 00 … E8` | reader `mov ecx,[0xC02D30]; call …` | CB region, same shape |
| `0x5DA11A` (insn `0x5DA119`) | `A3 30 2D C0 00` | **WRITER** `mov [0xC02D30],eax` | prior-recon "CreatePipe store" — byte-verified identical (`0x5DA119 = A3 30 2D C0 00 B8`) |
| `0x5DA1BE` (insn `0x5DA1BE`) | `A1 30 2D C0 00 … 3B C3` | reader `mov eax,[0xC02D30]; cmp eax,ebx` | inside `DestroyPipe` (entry `0x5DA130`) — the null-check before free |
| `0x5DA20D` (insn `0x5DA20B`) | `89 1D 30 2D C0 00` | **WRITER** `mov [0xC02D30],ebx` (null) | prior-recon "DestroyPipe null" — byte-verified identical (`0x5DA20B = 89 1D 30 2D C0 00 …`) |

**Result:** the image contains exactly TWO absolute writers of the pool pointer — the prior-recon
pair `0x5DA119` / `0x5DA20B` — and **no third writer anywhere**. Both sit in the
CreatePipe/DestroyPipe pair; `0x5DA20B` is inside `DestroyPipe` itself, after the `0x5D5AF7`
tail-jmp FIX A neutralizes.

Caveats (why this is census, not proof): on-disk bytes only (the image has a `.HOODLUM` section
and known runtime-decrypted regions — `docs/obsidian/Crash Analysis Reference.md:145` warns on-disk
bytes can be junk at runtime-decrypted addresses); absolute addressing only (a register-indirect
store through a cached pointer copy would not show up); this scan does not track SLOT frees
(`CPool::Delete` writes only the byteMap, not the pointer).

---

## 2. UNKNOWN / to attribute

### (a) The EARLY FREER — is there a free of the pool or its slots BEFORE `DestroyPipe`?

- **What prior recon established (inherited, not re-run):** the two pointer writers `0x5DA119`
  (CreatePipe store) / `0x5DA20B` (DestroyPipe null) — both byte-verified identical on disk this
  lane (§1b). Nothing else in the image writes `0xC02D30`.
- **What this lane added:** the 6-reference census above — an early pointer-nulling freer is now
  *excluded* for the on-disk image (subject to the caveats in §1b). That narrows the early-freer
  hypothesis space to **slot-level frees** (byteMap `0x80` bEmpty bit set, pointer untouched), which
  a byte scan cannot see.
- **RE approach for the next pass (do NOT grep the exe — disassemble):**
  1. Disassemble `DestroyPipe` from `0x5DA130` through `0x5DA2xx` (Capstone/Ghidra — local tools
     exist: `E:\dev(dave)\tools\ghidra`, `tools/ida-free`; on-disk image = K16) and enumerate every
     `CPool::Delete`-shaped call (the `sar esi,3` index math at `0x5D97FD` is the template).
  2. Enumerate callers of the CB region (`0x5D9852` / `0x5D98D2` call sites) — those pass the pool
     pointer into calls; identify which free slots when.
  3. Decisive runtime test (USER-RUN, see §3/m1834): debugger data-breakpoint on the byteMap byte
     of a slot observed live (e.g. `0x771E2F8`/`0x7D3DFB0` from K5), continue through quit, and
     capture the FIRST writer stack before `DestroyPipe` runs. If no write occurs before the
     `0x5DA20B` null, the early freer does not exist and the crash is pure DestroyPipe-first
     ordering (already root-caused, K4).
- **Hypothesis (labelled):** no early freer exists; "early" was an artifact of observing the freed
  slot before recognizing `DestroyPipe` as the only free site. Test = step 3 above.

### (b) Renewed120.asi correlation — co-trigger or coincidence?

- **Observed, on disk:** the 20× `Renewed120.asi+0xD41D` NULLDEREF burst at 23:42:01.589–.732
  (K13) sat ~3 s before a CLEAN quit (K14) on a FIX-A build — i.e. burst present, fatal absent.
  Session 22:52 had no burst and died of the *other* quit-UAF `0x810CB2` (K7, pre-FIX-C build).
- **Dispatch-recorded (log since rotated off disk — treat as inherited, not re-verified):** a burst
  at **12:08:09** ~13 s before the fatal captured as `skygfx_crash_2026-11-25_120822.dmp` (file
  mtime 12:08:22 corroborates the fatal time; the burst line itself is gone — `skygfx_dbg.log`
  now starts at the 2026-11-26 23:41 session, `:1`). Recurring in the 23:42 session as well (K13).
- **Current tally:** 1 inherited burst→fatal pairing (11-25 12:08, pre-fix) vs 1 observed
  burst→clean-quit (11-26 23:42, post-FIX-A) vs 1 no-burst→different-fatal (11-26 22:54).
  **Insufficient to call it a co-trigger.**
- **Mechanism hypothesis (labelled):** Renewed120's per-frame streaming churn changes which clumps
  / materials are alive at `CGame::Shutdown`, shifting which `specData` slots survive long enough
  to dangle when `DestroyPipe` runs first (K4). Prediction: with FIX A off, bursts raise fatal
  probability; with FIX A on, bursts change nothing (K14 already consistent with this, n=1).
- **How to test (A/B):** rename `Renewed120.asi` → `Renewed120.asi.disabled`, launch a loaded
  game, quit from gameplay, ×N; restore, repeat ×N; count `addr=0x005D97E6` fatals and
  `mod=Renewed120.asi+0xD41D` receipts per arm. **Prerequisite:** the fatal arm only exists on a
  build WITHOUT FIX A — either a pre-`5816147` checkout build (orchestrator-only) or accept that on
  current builds the A/B measures burst-vs-no-burst on *other* shutdown faults (e.g. `0x810CB2`
  pre-FIX-C) instead. All runs USER-RUN (m1834).

### (c) Is FIX A (the NOP) sufficient prevention?

- **Mechanism:** FIX A removes the only free site on the quit path for the spec pool (§1b: no other
  writer; `0x5DA20B` is behind the NOP), so `specData` stays valid through `rsRWTERMINATE` — the
  UAF cannot form. FIX B is the backstop for any state where the pool is already gone
  (`crashhandler.cpp:250-251`).
- **Evidence so far:** 2 observed post-FIX-A sessions — `22:52` (clean re specpool; died of the
  *other* UAF `0x810CB2`, K7) and `23:41` (clean quit even WITH the Renewed120 burst, K14). Zero
  `addr=0x005D97E6` since `5816147`. Not yet the deferred 20× retest.
- **Verification (the standing deferred item):** quit ×20 from loaded gameplay with **PS2 + Xbox
  engaged** (the mode pair that exercises the car env-map/spec pipe hardest); per session require
  `[specpool_guard] … neutralized` at session start (K12) and **zero** `[CRASH] … addr=0x005D97E6`
  / `skygfx_crash_*.dmp` with `ExceptionAddress=0x5D97E6`. If that holds, the early-freer question
  (a) is academic for prevention — it stays open only as understanding/forensics value.
- **Residual risk if FIX A pattern-mismatches:** the patch is skipped with one receipt
  (`crashhandler.cpp:964`, `:916-918`) — so the retest must also assert the guard receipt says
  "neutralized", not "PATTERN MISMATCH".

---

## 3. Ranked next actions (effort = lane time; game runs are USER-RUN — m1834)

| # | Action | Effort | Why here |
|---|---|---|---|
| 1 | **FIX A sufficiency retest (deferred item):** ×20 quits, PS2+Xbox engaged; assert `[specpool_guard]=neutralized` + zero `addr=0x005D97E6` (§2c) | ~30 min user-run + 10 min log triage (lane) | Cheapest; if green, (a) becomes academic and the backlog can close the fatal |
| 2 | **Dump CONTEXT anomaly:** all three parsed dumps carry `CONTEXT.Eip=0x77B5411C` (ntdll+`0xD411C`; ntdll base `0x77AE0000`, loaded ts == on-disk) while `ExceptionRecord.ExceptionAddress` is `0x5D97E6`/`0x810CB2`, and `Esi=0x…1C58` in all three. Hypothesis: an earlier VEH in the chain rewrote `ContextRecord` (VEHs run first-registered-first; ExceptionRecord would keep the original address). Resolve with `cdb -z <dump> -c "!analyze -v; .ecxr; q"` — read-only on dumps, **no game launch** | 30–60 min, read-only | Register-level evidence in future dumps is only trustworthy once explained |
| 3 | **Early-freer RE:** §2(a) steps 1–2 (disassemble `0x5DA130…0x5DA2xx`, enumerate CB-region call targets) — fully offline | 2–4 h | Narrows (a) to a single runtime question; can run before/without any game run |
| 4 | **Renewed120 A/B:** §2(b) protocol; needs a pre-FIX-A build for the fatal arm (orchestrator decision) | ~45 min user-run ×2 arms + triage | Only after #1 — otherwise the fatal arm cannot occur and the test is vacuous |
| 5 | **Slot-free data breakpoint at quit** (§2a step 3) — the decisive runtime test | ~1 h user-run with debugger attached | Highest information gain for (a); last, because #1 may make it unnecessary |
| 6 | **Persist burst→fatal pairings when observed:** the 12:08:09 pairing is inherited-but-rotated; copy receipt lines into a `.serena/memories/` record on sight (standing directive 7: docs every wave, never chat-only) | 5 min each | Cheap insurance against log rotation |

**Standing retest protocol:** m1834 — *never launch `gta_sa.exe` without user permission; all game
runs are USER-run* (`docs/plans/2026-11-25-portable-core-system.md:87`). Gate discipline:
IDENTITY row machine-checked, RETEST row user-eye-gate only — receipts never tick it
(`docs/plans/2026-11-07-build-gate-checklist.md:144-165`); lanes never build, one integrated build
by the orchestrator (`.serena/memories/standing-directives-2026-11-10.md:37-38`).

---

## 4. Cross-links

- **Wave record:** `.serena/memories/wave-20-record.md` (NEVER-REVERT §7; receipt-token discipline).
- **Standing directives:** `.serena/memories/standing-directives-2026-11-10.md` (extends, never
  replaces `.serena/memories/standing-directives-2026-11-02.md`).
- **FIX locations (current line numbers, verified this lane):**
  - FIX A — `src/core/crashhandler.cpp:860-966` (contract `:860-887`, constants `:888-891`,
    `CrashHandler_TrySpecPoolGuard` `:898-943`, `CrashHandler_InstallSpecPoolGuard` `:946-966`,
    receipt `:960-965`, second call site `:974-977`).
  - FIX B — `crashhandler.cpp:239-270` (contract + constants `:265-270`),
    `crashSpecPoolSkipSafe` `:275-306`, VEH dispatch `:536-563`, receipt `:553-556`.
  - FIX C — `crashhandler.cpp:307-345` (contract; "early freer unidentified" `:318`),
    `crashMatFxSkipSafe` `:351-374`, VEH dispatch `:565-593`, receipt `:582-585`.
  - Budget defect that surfaced the burst — `crashhandler.cpp:375-411` (esp. `:378-384`).
  - Header own-list: `src/core/crashhandler.h:16`.
- **Commits:** `5816147` (FIX A/B), `15fbd64` (FIX C), `29da737` (W0 crash budget).
- **Minidumps:** `E:\games\gtasa_skygfx_plus\skygfx_crash_2026-11-25_120822.dmp` (390,601 B,
  `ExceptionAddress=0x5D97E6`, fault `0x771E2FC`) · `…_145608.dmp` (396,020 B, `0x5D97E6`, fault
  `0x7D3DFB4`) · related: `…_2026-11-26_225401.dmp` (`0x810CB2`, K7). Naming rule:
  `crashhandler.cpp:1259`, `:1272-1276`.
- **Log receipts:** `[specpool_guard]` — `E:\games\gtasa_skygfx_plus\skygfx_dbg.log:2`,
  `skygfx_dbg.prev.log:2`; `[specpool_skip]` / `[matfx_uaf_skip]` — sources
  `crashhandler.cpp:554`, `:583`, census tokens `docs/plans/2026-11-07-build-gate-checklist.md:140`,
  `:173`; Renewed120 burst — `skygfx_dbg.log:2312-2350`; clean post-burst quit —
  `skygfx_dbg.log:2487-2496`.
- **Prior forensics style/precedent:** `docs/plans/2026-11-09-crash-forensics.md` (minidump
  evidence-table method), `docs/obsidian/Crash Analysis Reference.md:145` (on-disk vs
  runtime-decrypted bytes caveat).

---

**Status:** DONE — one new doc written at `docs/plans/2026-11-27-crash-pool-uaf-attribution.md`
(KNOWN table K1-K16 + 0xC02D30 writer census new this lane; UNKNOWN (a)/(b)/(c) with labelled
hypotheses; 6 ranked actions with effort; cross-links all path/line-verified). No source edits, no
builds, no commits, no game runs. Objective | findings: early pointer-freer EXCLUDED by on-disk
6-ref census (only `0x5DA119`/`0x5DA20B` write `0xC02D30`); Renewed120 correlation = 1 inherited
pairing vs 1 clean-quit counterexample (insufficient); FIX A sufficiency = deferred ×20
PS2+Xbox retest, m1834 user-run | evidence: `crashhandler.cpp:240-270/307-345/860-966/1259-1276`,
`skygfx_dbg.log:2/2312-2350/2487-2496`, `skygfx_dbg.prev.log:7684-7686`, 3 minidumps parsed
(`ExceptionAddress` `0x5D97E6`/`0x5D97E6`/`0x810CB2`), byte checks `0x5D5AF7=E9 34 46 00 00`,
`0x5DA130=A1 28 2D C0 00`, `0x5D97E6=8B 46 04`, commits `5816147`/`15fbd64`/`29da737`, m1834 at
`docs/plans/2026-11-25-portable-core-system.md:87` | status: DONE (doc only; git status = this one
new file atop pre-existing orchestrator dirt — no lane-caused changes).
