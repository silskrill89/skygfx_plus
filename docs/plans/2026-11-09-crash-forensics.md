# Crash Forensics — 0x42480000 read fault, 2026-11-09 04:59:16

**Lane:** debugger (forensics re-dispatch; prior lane returned empty)
**Mode:** READ-ONLY on source + one doc write. No builds, no edits, no commits.
**Artifacts:** `skygfx_crash_2026-11-09_045917.dmp` (423,061 B), `EarshotCrash_2026-11-09_045917.dmp` (419,218 B), `skygfx_dbg.log`, `gta_sa.exe`

---

## 1. Headline verdict

**This is not a data read through `0x42480000`. It is a *control transfer into* `0x42480000` — `EIP == 0x42480000` at fault time.**

| | |
|---|---|
| Faulting instruction | `gta_sa.exe + 0x163289` → `FF 50 0C` = **`call dword ptr [eax+0Ch]`** |
| Return address (`[ESP]`) | `0x0056328C` = `gta_sa.exe + 0x16328C` — exactly the next instruction after that `call` |
| Why logged as `op=0(READ)` | fetching the first instruction from a **non-present** page raises AV with `ExceptionInformation[0]=0`. No NX violation is involved, so this is *not* DEP. |
| Most probable owner | **gta_sa.exe game code, acting on a stale pool object.** No `skygfx.asi` frame appears in the near call chain. Not attributable to our mod on current evidence. |

The "float 50.0" (`0x42480000`) is **not** a value anybody wrote into a pointer at runtime — it is a
float that already ships inside `gta_sa.exe`. See §4.

---

## 2. Evidence table

| # | Fact | Source |
|---|---|---|
| E1 | Crash at `04:59:16.059`, draw counter `#141624` (`0x22938`); same counter as the `[SMAA] stage-0 restore` line 26 ms earlier | `skygfx_dbg.log:5414`, `:5415` |
| E2 | `CRASH[1]: READ at 0x42480000 (zone=HEAP/DLL) EAX=00863C40 EBX=148AE354 ECX=15BFDF5C EDX=0000009C ESI=15BFDF5C EDI=00000002 EBP=148AE5B0 ESP=0177F66C` | `skygfx_dbg.log:5415` |
| E3 | `[CRASH] code=0xC0000005 addr=0x42480000 op=0(READ) fault=0x42480000` | `skygfx_dbg.log:5416` |
| E4 | **Clean shutdown ran 18 s AFTER the crash** (`04:59:34`: `texdefs shutdown complete`, `envmap shutdown complete`, `water_rt` releases, `ForwardPlus` releases, `Ragdoll_Shutdown`) → this was **not** a shutdown-time crash | `skygfx_dbg.log:5451`–`:5464` |
| E5 | Both dumps: identical `ExceptionCode=0xC0000005`, `npar=2`, `ExceptionInformation=[0x0, 0x42480000]`, `ThreadId=0x1F14`, 128 modules, 43 threads | parsed `MINIDUMP_EXCEPTION_STREAM` of both files |
| E6 | True x86 `CONTEXT` located at dump-file base `0x7D36`; **all 8 logged registers reproduce exactly** and `CS=0x0023 SS=0x002B EFL=0x00210206` (self-consistent ring-3 context) | brute-force `EIP==faultaddr` scan + CS/SS/ESP validation, cross-checked against E2 |
| E7 | **`EIP = 0x42480000`** (== fault address), not merely a deref target | recovered CONTEXT, `base+192` |
| E8 | `[ESP] = 0x0056328C` → `gta_sa.exe + 0x16328C` | read via captured stack range `0x0177DC24..0x01780300` |
| E9 | Instruction bytes at `gta_sa.exe` VA `0x00563280`: `56 8B 74 24 08 8B 06 8B CE FF 50 0C 8A 46 36 24 07 3C 01 76 0C 3C 05 73 08 8B CE 5E E9 BF F5 FD` | read from `gta_sa.exe` (14,383,616 B, `TimeDateStamp=0x427101CA` == module-list ts) |
| E10 | 19 occurrences of `0x42480000` inside `gta_sa.exe` within `RVA 0x463000..0x464800` — the window containing `EAX`; 288 in the whole image | pattern scan of the EXE file |
| E11 | Module layout: `skygfx.asi` @ `0x5ED10000` (size `0x45E000`, ts `0x6AF151FB`, CV `skygfx.pdb` age=70 guid `d557d235a6b60f479494871c50f733b9`); `earshot_reminid.asi` @ `0x5F450000` (CV `earshot_reminid.pdb`); `gta_sa.exe` @ `0x00400000` | `MINIDUMP_MODULE_LIST` |
| E12 | Near-stack frames (`ESP .. ESP+0x300`): **only `gta_sa.exe`** plus one `III.VC.SA.LimitAdjuster.asi+0x268B1` at `ESP-0x10`; deeper → `earshot_reminid.asi`, `nvoglv32.dll`, `vulkan.dll`. **No `skygfx.asi` return address in that window.** | stack dword scan |
| E13 | Source grep for `50.0f` finds nothing in any SMAA / postfx / window-close path — only `vehiclePipe.cpp:213,214,249,250,301,302,325,326`, `neoWaterdrops.cpp:606`, `envmap.cpp:96,113,607` (all `250.0f * mult` or `*50.0f` scale math) | `src/**` |
| E14 | The exact log string `stage-0 restore` and `[postfx_flush_id] 0x701FE9` **do not exist in this source tree** → the deployed ASI is not built from this checkout | grep over `src/` |
| E15 | No prior crash in any surviving log: `skygfx_dbg.prev.log` has 0 `CRASH`/`0x42480000` hits; only `skygfx_dbg.log` and `EarshotReminid_dbg.log` contain `0x42480000` | grep across all `*.log` |

---

## 3. Dump analysis — one fault or two?

**One fault, two writers.** Both files carry a byte-identical exception stream (E5), the same thread id,
the same module list, and were written in the same second. EarshotReminid ships its own crash dumper
(`earshot_reminid.pdb` in its CV record) and its own log also contains `0x42480000` (E15).

- `skygfx_crash_*` = written by our handler (bigger: it appends our last-log ring).
- `EarshotCrash_*` = written by EarshotReminid's handler (419,218 B vs 423,061 B).

**Method note (important).** `cdb`/`windbg` are absent on this box (`where cdb` → "Could not find files";
no Windows Kits debugger dir). No `python -m minidump` and no `construct` either. The dumps were parsed
with a hand-written `struct` parser. A first pass using the documented `MINIDUMP_THREAD` stride produced
a garbage context (`EIP=00200216`, `ESP=0220007F` — unaligned), so the real context was recovered by
scanning for a base satisfying `u32(base+192)==faultaddr && CS==0x23 && SS==0x2B && ESP` inside a captured
range, then validating against the registers our handler already logged (8/8 match, E6). Everything below
rests on that validated context.

Two copies of the context exist in the dump (bases `0x7D36` and `0x1F98F`), consistent with the context
being captured once per handler plus a copy inside a captured stack range.

---

## 4. Attribution chain (evidence → conclusion)

**Step 1 — the call site.** Decoding E9:

```
00563280  56            push esi
00563281  8B7424 08     mov esi,[esp+8]        ; esi = object ptr
00563285  8B06          mov eax,[esi]          ; eax = obj->vptr  == 0x00863C40
00563287  8BCE          mov ecx,esi
00563289  FF50 0C       call dword ptr [eax+0Ch]   <-- FAULT
0056328C  8A46 36       mov al,[esi+0x36]      ; == the address found at [ESP]
0056328F  24 07         and al,7
00563291  3C 01         cmp al,1
00563293  76 0C         jbe  short +0Ch
```

This is a plain C++ virtual dispatch through slot 3, on an object carrying a 3-bit type tag at `+0x36`.
It is game code, not a D3D call, not a hook trampoline.

**Step 2 — what `EAX=0x00863C40` really is.** `0x00863C40` is *inside the gta_sa.exe image*
(`0x00400000..0x01577000`). Reading the pristine EXE at RVA `0x00463C40`:

```
00863C40 : 0054F5D0   ; a code pointer into .text
00863C44 : 3BA3D70A   ; float 0.005...
00863C48 : 40666666   ; float 3.6
00863C4C : 42480000   ; float 50.0   <-- [EAX+0Ch], i.e. the "function" that got called
00863C50 : "PedAttractors\0PedIntelligence\0TaskAllocator\0NodeRoute\0PatrolRoute\0PointerRoute..."
```

So `[EAX+0Ch]` is a **float already present in the shipped binary** (E10: 19 such floats in that window,
288 in the image). Nothing had to be written at runtime for this read to yield `50.0f`.

**Step 3 — therefore the actual defect is one level up.** `0x00863C40` is not a vtable — it is a static
float/string data record. The heap object at `ESI = 0x15BFDF5C` had its **vptr field equal to
`0x00863C40`**, i.e. it pointed at that data record. Then `call [vptr+0Ch]` read the float and branched
to it. This is precisely the *stale-object / wrong-field / double-deref* class: either the object was
freed and its first dword recycled, or a pointer to that static record was stored where an object
pointer was expected.

Corroboration that the frame is already unhealthy: `EBP = 0x148AE5B0` is a **heap** address, not a stack
address, so the frame-pointer chain is broken before the fault (EBP-chain unwind yields garbage
`ret=F2E8C4F3`).

**Step 4 — the 50.0f source-grep lead (task 3) is a dead end, and that is the finding.** No SMAA,
postfx, window-close or raster path in our source contains `50.0f` (E13), and the float in question is
static game data, not something a mod writes. So "who wrote 50.0 into a pointer" has no answer — nobody
did. The actionable question is "why is that object's vptr `0x00863C40`".

---

## 5. Answers to the four questions

### (a) Faulting frame + most probable owner
- **Frame:** `gta_sa.exe + 0x163289`, `call dword ptr [eax+0Ch]`, return `gta_sa.exe + 0x16328C`. Game image — not `skygfx.asi`, not `earshot_reminid.asi`, not `d3d9.dll`.
- **Owner ranking:**
  1. **Game / foreign ASI acting on a stale pool object (most likely).** Nothing of ours appears in the `ESP..ESP+0x300` window (E12). The type-tagged dispatch (`+0x36` tag 1..5) is a pooled-entity pattern; a pool whose object was reused while a reference survived produces exactly this.
  2. **fastman92LimitAdjuster** — explicitly worth suspecting: it is loaded, it re-sizes/relocates SA pools, and `III.VC.SA.LimitAdjuster.asi+0x268B1` sits adjacent to the frame at `ESP-0x10`. (Suggestive, not proof: that slot is below ESP, i.e. stale scratch.)
  3. **Our mod — least likely.** No frame of ours, no `50.0f` in any related path, and the target float is static game data. Cannot be *formally* excluded: heap corruption is detected far from its cause, and SMAA was the last thing we did.
- **Environment smell worth fixing regardless:** two copies of `gta_bridge.asi` are loaded simultaneously (`0x5EB10000` ts `0x6A93AF2C` and `0x5F400000` ts `0x6A9F2348`). Duplicate global state in a duplicated ASI is a corruption risk independent of this fault.

### (b) Does it cluster at close / after `[SMAA] stage-0 restore`?
**For this instance: no — the premise is disproven.**
- The crash is **mid-frame**, at draw `#141624`, with rendering still running.
- A **complete, clean shutdown ran 18 s later** at `04:59:34` (E4). If this were a close-time fault, no clean shutdown would follow it.
- The SMAA correlation is **temporal only**. The logged pointers from that line (`raster=04C1F070`, `devTex=0CC42E50`) are unrelated to the faulting object (`ESI=0x15BFDF5C`) and the log itself reports the shutdown below as clean, so "stale raster / devTex released too early" is not supported here.
- Historical timing **cannot** be corroborated: no surviving log other than today's contains `0x42480000` (E15), and the older session logs rotated away. Only dump filenames remain (§6).

### (c) Minimal fix sketch
Since the executing frame is game code, there is no line of ours to fix. Recommended:

1. **Close as not-ours** with evidence E9 + E11 + E12, unless WinDbg shows a `skygfx.asi` frame.
2. **Defensive guard we can add** (cheap, no behaviour change):
   - In `src/core/debug_handl.cpp`, make the handler distinguish **`EIP == fault address`** ("branched to non-present page") from a true data read. Today it prints `op=0(READ)`, which is exactly what sent this investigation down the "someone stored 50.0f in a pointer" path. Log the branch together with `[EAX]`, `[EAX+0Ch]` and 16 bytes at `ESI` — that single change would have made this dump self-attributing.
   - Add an `IsBadReadPtr`-style liveness probe immediately before any of our restored-pointer writes in the SMAA/postfx stage-0 path (`src/render/postfx.cpp`, `CPostEffects::DrawSMAA`), and refuse to restore a raster/`devTex` that is no longer readable. This converts a future variant into a logged skip instead of a fault.
3. **Bisect the environment** to promote hypotheses: (i) remove the duplicate `gta_bridge.asi`; (ii) run one session with `III.VC.SA.LimitAdjuster.asi` disabled; (iii) run one with `earshot_reminid.asi` disabled. If the fault persists across all three, it is a vanilla-SA UAF.

### (d) What would settle it
Install Windows SDK debugging tools, then against either dump:
```
cdb -z E:\games\gtasa_skygfx_plus\skygfx_crash_2026-11-09_045917.dmp
.sympath+ E:\games\gtasa_skygfx_plus      ; skygfx.pdb is here and matches the CV record
.ecxr ; kb ; u 0x00563289 ; dps 0x00863C40 ; !analyze -v
```
This yields named frames for the `gta_sa.exe` chain and the deeper `earshot_reminid.asi` frames, and a
definitive yes/no on any `skygfx.asi` participation. My prediction: none.

---

## 6. Cross-session dump timeline (from filenames; the only surviving historical evidence)

| Timestamp (from filename) | skygfx dump | Earshot dump | Paired? |
|---|---|---|---|
| 2026-10-16 00:09:45 | — | 447,769 B | Earshot only |
| 2026-10-20 20:12:10 | — | 335,544 B | Earshot only |
| 2026-10-21 17:41:56 | 179,925 B | 183,198 B | yes |
| 2026-10-21 17:42:17 | 181,448 B | 181,536 B | yes |
| 2026-10-26 05:45:10 | — | 421,510 B | Earshot only |
| 2026-10-31 00:51:45/46 | 448,379 B | 447,464 B | yes |
| 2026-10-31 02:17:08 | 413,548 B | 414,047 B | yes |
| 2026-11-01 19:05:25 | 400,466 B | 398,403 B | yes |
| **2026-11-09 04:59:17** | **423,061 B** | **419,218 B** | **yes — this crash** |

Observations: the paired pattern (near-identical size ~1 s apart) recurs, confirming two VEH writers on
most crash days; on 10-16, 10-20 and 10-26 only Earshot's handler produced a dump. Because only today's
log survives with `0x42480000` in it (E15), **I do not claim the October crashes share this call site** —
they are the same *shape* (paired dumps, same zone), nothing more. Confirm with the WinDbg run above.

---

## 7. Honest limits of this analysis

- The exception record's `ExceptionAddress` reads as `0x4248000000000000` in my parser (the 64-bit field is read at a straddled offset); I used `ExceptionInformation[1]` (`0x42480000`) as the authoritative fault address. Consequence: the "unresolved" `info[1]` values printed for older dumps cannot be trusted as fault addresses.
- A first, discarded sweep mis-masked that field and produced a 12-dump "constant EAX" family; that claim is retracted and is **not** used above.
- Only the two 11-09 dumps yielded a recoverable CONTEXT; historical contexts were not independently recovered.
- No skygfx.asi frame was observed within `ESP..ESP+0x300`; a full unwind may in principle find one deeper.
- Instruction bytes at `EIP` are (correctly) unavailable — `0x42480000` is unmapped. All disassembly above is from the on-disk `gta_sa.exe`, whose `TimeDateStamp` matches the loaded module, so it is the right image.

---

## 8. Second pass — the 16:00:06 fault at `gta_sa.exe+0xA8E49` (fx-system list REMOVE)

Read-only forensics pass. No source edits, no builds. Tooling: raw `MINIDUMP_*` parsing in Python
(scripts in `%TEMP%\fx8\`) + capstone 5.0.1 disassembly of the on-disk `gta_sa.exe`
(`TimeDateStamp=0x006AE000`, image base `0x00400000`). No cdb/WinDbg available on this box.

> **Parser correction to §7.** §7 records `ExceptionAddress` reading as `0x4248000000000000`
> attributed to a straddled field. With the correct `MINIDUMP_HEADER`
> (`NumberOfStreams@8`, `StreamDirectoryRva@12`, `TimeDateStamp@20`) and
> `MINIDUMP_EXCEPTION_STREAM` layout (`MINIDUMP_EXCEPTION` begins at `+8`; `ExceptionAddress@+24`,
> `NumberParameters@+32`, `ExceptionInformation@+40`, `ThreadContext@+160`), that field reads cleanly
> as `0x0000000042480000`. The straddle was a parser artifact, not a dump defect; the
> `info[1]=0x42480000` conclusion itself stands.

### 8.1 Dump identities (16:00:06 event)

| File | Size | Stream `TimeDateStamp` | ExceptionCode | `ExceptionAddress` |
|---|---|---|---|---|
| `skygfx_crash_2026-11-09_160006.dmp` | 426,100 B | 2026-11-09 16:00:06 | `0xC0000005` | `0x004A8E49` |
| `EarshotCrash_2026-11-09_160006.dmp` | 420,499 B | 2026-11-09 16:00:06 | `0xC0000005` | `0x004A8E49` |

One fault, two VEH dumpers — the pairing already established in §6. Both carry an identical
`CONTEXT` (§8.4). For contrast, the earlier same-day pair `*_045917` is the `EIP=0x42480000`
branch-to-float event documented in §1–§7 and is **not** this call site.

### 8.2 Naming the code at `+0xA8E49` — a doubly-linked list REMOVE

`0x4A8E30` disassembles as a complete `thiscall` unlink taking one argument:

```
004A8E30  56                push    esi
004A8E31  8B 74 24 08       mov     esi, [esp+8]        ; arg0 = node to unlink
004A8E35  8B 56 04          mov     edx, [esi+4]        ; prev  = node->prev   (+4)
004A8E38  85 D2             test    edx, edx
004A8E3A  8B 06             mov     eax, [esi]          ; next  = node->next   (+0)
004A8E3C  74 04             je      0x4A8E42
004A8E3E  89 02             mov     [edx], eax          ; prev->next = next      <-- ran OK
004A8E40  EB 03             jmp     0x4A8E45
004A8E42  89 41 04          mov     [ecx+4], eax        ; node was head
004A8E45  85 C0             test    eax, eax
004A8E47  74 10             je      0x4A8E59
004A8E49  89 50 04          mov     [eax+4], edx        ; next->prev = prev      <-- FAULTS
004A8E4C  8B 41 08          mov     eax, [ecx+8]        ; count
004A8E4F  8D 50 FF          lea     edx, [eax-1]
004A8E52  89 51 08          mov     [ecx+8], edx        ; count--                (never reached)
004A8E55  5E                pop     esi
004A8E56  C2 04 00          ret     4
004A8E59  8B 46 04          mov     eax, [esi+4]        ; node was tail
004A8E5C  89 01             mov     [ecx], eax
004A8E5E  ...               mov eax,[ecx+8]; lea edx,[eax-1]; mov [ecx+8],edx ; count--
```

So the container at `ECX` is `{ +0 tail, +4 head, +8 count }` and each node is
`{ +0 next, +4 prev }` — a classic intrusive doubly-linked list with a counted header.
An **INSERT twin** sits immediately above at `0x4A8DF0–0x4A8E21`, doing the mirror operations with
`lea edx,[eax+1]` / `mov [ecx+8],edx` (`count++`), which confirms the header layout rather than
leaving it inferred.

**Identity of the container.** `ECX` is a *static* (`0x00A9AE8C`, byte-identical across every
occurrence) and the callers push `0x00A9AE80` (visible at `ESP+0x10` and `ESP+0x14`).
`0x00A9AE8C − 0x00A9AE80 = 0xC`, matching the in-tree RCA already recorded at
`src/render/neopipe.cpp:1258-1272`: *"fxManager's list-REMOVE (0x4A8E49, node layout
+0=next/+4=prev, header 0xA9AE8C = fxManager 0xA9AE80+0xC = live systems list)"*. My disassembly
confirms the structure (link pair + counted header, INSERT/REMOVE pair) and confirms the caller
pushes `0xA9AE80`; **the symbol name `fxManager` itself is inherited from that comment — this exe
ships stripped and I cannot verify the name independently.** The callers land at `+0xA9886`,
`+0xA9AD3` (both inside the same `0x4A8xxx` FX block) and `+0x13C1F5` (which itself runs
`fld`/`fmul` on float operands at `0x53C21A`).

### 8.3 Memory evidence from the 16:00 dump (the decisive part)

The container static `0x00A9AE8C` is **not captured** in either minidump, but the two *nodes* are:

| Address | Role | Words read from dump |
|---|---|---|
| `0x22548B98` | node being unlinked (`ESI=EDI`) | `3E2FCF44 225474A0 17A21E24 22C49688 3F800000 …` |
| `0x225474A0` | its `prev` (`EDX`) | `3E2FCF44 19B5B640 17A26300 22C49688 3F800000 …` |

Two conclusions:

1. **`node->next` (`+0`) held float bits, `node->prev` (`+4`) was a valid heap pointer.**
   `0x3E2FCF44` decodes to `0.171689093f`. The node's payload is genuinely float-bearing
   (`+0x10 = 0x3F800000` = `1.0f`), consistent with an FX/particle record rather than a pure
   allocator node — so *only the link word* is poisoned, not the whole record.
2. **The corruption had already propagated before the fault.** `prev` (`0x225474A0`) also contains
   `0x3E2FCF44` at `+0`, i.e. the instruction at `0x4A8E3E` (`prev->next = next`) **executed
   successfully** and wrote the float value into a live list element. The crash is the *second*
   half of the same unlink. Any post-mortem that reads the list after this point sees two elements
   pointing at `0x3E2FCF44`.

### 8.4 Recurrence — verified, not assumed

Contexts recovered both from the declared `ThreadContext` location and by brute-force scan for
`Eip==0x004A8E49 ∧ SegCs==0x23 ∧ SegSs==0x2B`; the two agree exactly in every case.

| Date (dump `TimeDateStamp`) | Dump(s) | `EAX` (`node->next`) | `EDX` (`prev`) | `ESI=EDI` (node) | `ECX` | `ESP` | `EBX` | `info[1]` | as float |
|---|---|---|---|---|---|---|---|---|---|
| 2026-10-15 23:09:46 UTC (file `10-16_000945`) | Earshot only, 447,769 B | `0x3F6735FB` | `0x27B93090` | `0x27B934F0` | `0x00A9AE8C` | `0x0177F514` | 3 | `0x3F6735FF` | 0.903167427 |
| 2026-11-01 19:05:25 | both, 400,466 / 398,403 B | `0x3F70ACE0` | `0x22FB1E70` | `0x22FB17E0` | `0x00A9AE8C` | `0x0177F514` | 3 | `0x3F70ACE4` | 0.940137863 |
| **2026-11-09 16:00:06** | both, 426,100 / 420,499 B | `0x3E2FCF44` | `0x225474A0` | `0x22548B98` | `0x00A9AE8C` | `0x0177F514` | 3 | `0x3E2FCF48` | 0.171689093 |

Internal consistency checks that make this table trustworthy rather than coincidental:

- **`info[1] == EAX + 4` in all three rows** — exactly the operand of `mov [eax+4],edx`. The fault
  address is arithmetically derived from the poisoned link word, independently of any log.
- `ECX` (the static container) and `ESP` are **identical across three dates 24 days apart**; `EBX=3`
  likewise. That is the signature of one unchanging code path, not three different bugs.
- Only the heap/`EAX` values differ, and each is in the float domain.

`EBP` is *also* float-domain in all three (`0x3B5B544F`=0.00335f, `0x3CF50713`=0.02991f,
`0x3C51F52B`=0.01281f), which reinforces the "float data where pointers live" reading of this frame
family.

### 8.5 Mechanism, and how it relates to the `0x42480000` class

Same *class*, different *role*:

| | `0x42480000` event (§1–§7, 04:59:17) | this event (16:00:06) |
|---|---|---|
| Fault kind | `EIP = 0x42480000` — branch to non-present page | `WRITE` to `[EAX+4]`, `EAX` = float bits |
| Float value | `0x42480000` = **50.0f exactly** | `0x3E2FCF44` ≈ 0.1717f (etc.) |
| Float landed in | a **vtable/code pointer** (executed) | a **list link word** (dereferenced) |
| `info[0]` | `0` (read) | `1` (write) |

Both are "a float bit-pattern occupying a slot the CPU then uses as an address". They are not the
same bug: the 04:59 one is a corrupted *indirect call target*, this one is a corrupted *intrusive
list link*, and they are at unrelated call sites. **This 16:00 site is the same site as 10-16 and
11-01**, and it is the site already analysed in-tree at `src/render/neopipe.cpp:1258` — that comment
quotes `0x3F70ACE0 (~0.9688f)`, which is precisely the **11-01** dump above. So §8 is independent
corroboration of an existing RCA plus new memory evidence (§8.3), not a new discovery.

### 8.6 The `earshot_reminid.asi+0x55A65` frame is a stack-scan artifact

`earshot_reminid.asi` is loaded at base `0x5E700000`, size `0x1C6000` (→ `0x5E8C6000`), so
`+0x55A65` = `0x5E755A65`. That value sits at **`ESP+0x54`**. Its neighbourhood:

```
ESP+48  0x3D41E0DC   ~0.0470f
ESP+4C  0x3FCEDFB9   ~1.6158f
ESP+50  0xBFC00000   -1.5f exactly
ESP+54  0x5E755A65   <-- "earshot_reminid.asi+0x55A65"  (also float-domain: ~4.4e18f)
ESP+58  0x436056E3   ~224.34f
ESP+5C  0x0177F5E8
```

Reasons this is **not** a return address:

1. It lies inside a contiguous run of float arguments/locals, not at a frame boundary. The frame it
   sits in belongs to caller `0x53C1F5`, which genuinely computes floats
   (`0x53C21A fld dword ptr [0xb7cb5c]` / `fmul dword ptr [0x858b38]`).
2. It is **not stable across runs**: the same `ESP+0x54` slot in the 10-16 dump holds
   `0x5F2E5785` — a *different* float-domain value. A real return address at a fixed call site is
   deterministic.
3. Its neighbours include `-1.5f` exactly — unmistakable float data.
4. Pointer-scan unwinding of this stack also "finds" `gta_sa.exe+0x210073`, `+0x33005F`, `+0x39006B`,
   `+0x260067`, `+0x1F0078`, `+0x2C0070`, `+0x330075` at `ESP+0xDC…0xF4` — which are simply the
   UTF-16 bytes of **`sa_skygfx_plus`** (0x73 0x00 0x61 0x00 0x5F … ) sitting on the stack as string
   data. Demonstrates the scan's false-positive rate directly.

**Conclusion: Earshot is innocent.** No genuine `earshot_reminid.asi` return address appears in the
faulting frame; its only "presence" is a float coincidentally inside its 1.8 MB image range.
(The later hits at `ESP+0xC4/+0xD0/+0xD4` may be genuine but lie past the exception frame and belong
to Earshot's own VEH/dump path, not to the fault.)

### 8.7 Verdict

**Not attributable to skygfx. Close as environment/game-side FX-list corruption.**

Evidence chain, strongest first:

1. **Zero `skygfx.asi` frames.** `skygfx.asi` occupies `0x5E000000–0x5E465000`. Scanning every
   4-byte slot from `ESP` through `ESP+0x400` yields **no value inside that range**. All three real
   return addresses resolve to `gta_sa.exe` (`+0xA9886`, `+0xA9AD3`, `+0x13C1F5`). *(file:line —
   evidence is in the dumps, cited above.)*
2. **The frame is self-consistent**, which rules out a smashed/foreign frame: `[ESP]` = saved `ESI`
   = `0x22548B98` = the node arg, `[ESP+4]` = return `0x004A9886`, `[ESP+8]` = the very same node
   arg consumed by `ret 4`. The frame is the game's own, intact.
3. **The corrupted word is one link of a game-owned intrusive list** whose header is a static
   (`0x00A9AE8C`) and whose node payload is genuine FX float data (`1.0f` at `+0x10`). Our code does
   not own or allocate these nodes.
4. **Timing exonerates the new gates.** The ragdoll/dismember churn gates are new this week, yet
   the earliest instance of this exact site is **2026-10-16** — three weeks earlier.
5. **Prior source analysis agrees** (`src/render/neopipe.cpp:1258-1272`): the writes reached the
   header from *outside* the list machinery, via float data through a stale pool pointer — i.e.
   game-side FX memory reuse. My memory read (§8.3) strengthens this: the poisoning is confined to
   the link word while the rest of the record stays intact, which is what stale-pointer float writes
   into a recycled node look like.
6. `[STATS] av_write=1` is self-counted by our own handler (`src/core/debug_handl.cpp:610`) — one
   write-type AV *observed*, not an external writer. It is not evidence against us.

One caveat worth recording: the in-tree hardening (vtable-signature gate `IsFxRenderPrim`,
`neopipe.cpp:1286-1292`) was committed **2026-11-02**, i.e. *before* the 16:00 crash. That it did not
prevent this event is consistent with (not evidence against) point 5 — the gate constrains **our**
writes, and nothing in the evidence says our writes were involved. Whether the `skygfx.asi` actually
running at 16:00 was built from that source is unverifiable: the file's mtime is `2026-11-09 21:28`,
a rebuild after the crash.

### 8.8 Honest limits of this pass

- **No symbols.** `fxManager` / "live systems list" naming is inherited from `neopipe.cpp:1258`;
  I verified the *structure* (link pair + counted header + INSERT/REMOVE pair + `0xA9AE80` pushed by
  callers) but cannot verify the *name*. A symbolised `kb` would settle it.
- **No cdb/WinDbg on this box**; everything here is raw minidump parsing + capstone. No unwind
  database used, so no full call-chain reconstruction (FPO omitted-frame-pointer frames may hide
  something beyond `ESP+0x400`).
- **Container header not readable.** `0x00A9AE8C` is absent from both dumps' memory ranges, so
  head/tail/count were never observed — only inferred from the code and from `ECX`.
- Brute-force `Eip` scanning can in principle match a stale copy of the context; here it agreed with
  the declared `ThreadContext` in every dump, and the `info[1]==EAX+4` identity independently
  corroborates each row.
- The 10-16 row's local time vs UTC is offset (dump stamps UTC); dates in §8.4 are given as read
  from the stream so they compare cleanly.
- Phase-of-session (mid-render vs exit teardown) is only established for 16:00:06 from the user's
  report and the log; for 10-16 and 11-01 the surrounding logs do not survive, so "always at exit"
  is **not** established.

### 8.9 Recommended follow-up (no source change made by this pass)

1. Close this crash class as not-ours; keep the §8.4 table as the fingerprint for triage.
2. Highest-value discriminator still unavailable offline: reproduce with `earshot_reminid.asi` and
   `III.VC.SA.LimitAdjuster.asi` disabled separately (already §5 recommendation (iii)) — the goal is
   to find which teardown frees the FX node whose `+0` later holds float bits.
3. Optional and cheap: have `debug_handl.cpp` log the full unlink pair (`node`, `node->next`,
   `node->prev`, `[node->next+4]`) when the fault site is `+0xA8E49`; given §8.3, capturing
   `prev->next` after the fact is what proves the propagation.

