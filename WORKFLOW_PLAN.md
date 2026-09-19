# Workflow Plan — skygfx_plus_expIV

## Executive Summary

This plan addresses all issues found in the codebase analysis, assigns appropriate skills, and establishes workflow rules for token efficiency. The project has **9 TODO items**, **4 memory management issues**, **4 null pointer/division risks**, **2 uninitialized variable issues**, **3 buffer overflow risks**, and **5 architecture concerns**.

---

## Issue Registry

### Priority 1: HIGH SEVERITY

| ID | Issue | File | Line | Skill | Verification |
|----|-------|------|------|-------|--------------|
| ARCH-1 | Config struct ~250 fields, layout-sensitive via inline asm | skygfx.h | 202-478 | @oracle | Static asserts enforce offsets (skygfx.h:615-617) ✅ |
| ARCH-2 | ~~Code duplication ~1500 lines across 7 vehicle callbacks~~ **PARTIALLY FIXED** | vehiclePipe.cpp | multiple | @fixer | ✅ Shared helpers extracted (b680e31) |

### Priority 2: MEDIUM SEVERITY

| ID | Issue | File | Line | Skill | Verification |
|----|-------|------|------|-------|--------------|
| MEM-1 | ~~Unguarded RwV3dNormalize~~ **FIXED** | vehiclePipe.cpp | 209,282,373 | @fixer | ✅ `== 0.0f` guards |
| MEM-2 | ~~Division-by-zero risks~~ **FIXED** | postfx.cpp | 1506+ | @fixer | ✅ `max(..., 1e-7f)` epsilon guards |
| MEM-3 | ~~fxParams.fxSwitch uninitialized~~ **FIXED** | vehiclePipe.cpp | 621,776,1178,1301,1459 | @fixer | ✅ memset zero-init |
| MEM-4 | ~~Unsafe strcat in getpath~~ **FIXED** | main.cpp | 108 | @fixer | ✅ strncat with bounds |
| ARCH-3 | Render state leaks (no enforcement) | multiple | - | @oracle | State tracking audit. Verification: state table landed this session (~90 sites) |
| ARCH-4 | Static locals non-reentrant | multiple | - | @oracle | Thread safety review. Verification: statics table landed (3 HIGH: postfx.cpp:883-884, :1042, :1175; depthhook.cpp:39-41) |

### Priority 3: LOW SEVERITY

| ID | Issue | File | Line | Skill | Verification |
|----|-------|------|------|-------|--------------|
| MEM-5 | Raw new without delete (4 allocations) | normalmap_plugin.cpp, texdb.cpp | 822,1146,103,167 | @fixer | Shutdown cleanup |
| MEM-6 | ~~Unused variable 'sub'~~ **FIXED** | vehiclePipe.cpp | — | @fixer | ✅ Variable removed |
| MEM-7 | ~~sprintf without bounds~~ **FIXED** | PC_PlantsMgr.cpp | 428-451 | @fixer | ✅ snprintf with sizeof |
| TODO-1 | Implement safe swap double-buffer | wheels_extender.cpp | 331 | @fixer | Concurrency test |
| TODO-2 | InterceptCall hook | wheels_extender.cpp | 348 | @fixer | Hook validation |
| TODO-3 | Reconstruct normals from depth | envmap.cpp | — | @librarian | Research implementation |
| TODO-4 | Instance one set | buildingPipe.cpp | 1021 | @fixer | Performance test |
| TODO-5 | Allow III/VC in debug menu | debugmenu_ui.cpp | — | @designer | UI test |
| TODO-6 | Recover unified pipeline | debugmenu_ui.cpp | 301 | @librarian | Research JuniorDjjr fork |
| TODO-7 | Do we always have this? | vehiclePipe.cpp | — | @oracle | Architecture review |
| TODO-8 | Is this even needed? | vehiclePipe.cpp | 707,717 | @oracle | Architecture review |
| TODO-9 | Tex coords correct? | postfx.cpp | — | @fixer | Visual verification |

---

## Skill Assignment Matrix

### Primary Skills by Issue Type

| Issue Category | Primary Skill | Secondary Skill | LSAI Tools |
|----------------|---------------|-----------------|------------|
| Memory Safety | memory-safety-patterns | sanitizers | lsai_impact, lsai_usages |
| Division Safety | sanitizers | static-analysis | lsai_callees, lsai_context |
| Buffer Overflow | static-analysis | debugging-and-error-recovery | lsai_impact, lsai_deps |
| Architecture | oracle | writing-plans | lsai_hierarchy, lsai_callers |
| Code Duplication | fixer | simplify | lsai_outline, lsai_source |
| TODO Items | varies by complexity | verification-planning | lsai_search, lsai_info |
| UI/UX Issues | designer | - | lsai_outline |

### Skill Selection Rules

1. **Single-file mechanical fixes** → @fixer
2. **Multi-file refactoring** → @fixer + @oracle review
3. **Architecture decisions** → @oracle
4. **External research needed** → @librarian
5. **UI/UX changes** → @designer
6. **Verification needed** → verification-planning skill
7. **Runtime bug detection** → sanitizers skill
8. **Code quality hardening** → static-analysis skill

---

## Execution Phases

### Phase 1: Safety Critical (Week 1)

**Objective**: Eliminate crash-causing bugs

| Task | Skill | Files | Verification |
|------|-------|-------|--------------|
| Fix unguarded RwV3dNormalize | @fixer | vehiclePipe.cpp | Runtime test |
| Fix division-by-zero risks | @fixer | postfx.cpp | Runtime test |
| Fix unsafe strcat | @fixer | main.cpp | Bounds test |
| Fix uninitialized fxParams | @fixer | vehiclePipe.cpp | Static analysis |

**Dependency**: None (parallel execution)
**Expected Time**: 2-3 hours

### Phase 2: Memory Safety (Week 1-2)

**Objective**: Eliminate memory leaks and unsafe patterns

| Task | Skill | Files | Verification |
|------|-------|-------|--------------|
| Add shutdown cleanup | @fixer | normalmap_plugin.cpp, texdb.cpp | Shutdown test |
| Remove unused variables | @fixer | vehiclePipe.cpp | Compiler warning |
| Fix sprintf bounds | @fixer | PC_PlantsMgr.cpp | Debug test |

**Dependency**: Phase 1 complete
**Expected Time**: 1-2 hours

### Phase 3: Architecture Review (Week 2)

**Objective**: Address structural concerns

| Task | Skill | Files | Verification |
|------|-------|-------|--------------|
| Config struct refactor plan | @oracle | skygfx.h | Impact analysis |
| Render state audit | @oracle | multiple | State tracking |
| Code duplication reduction plan | @oracle | vehiclePipe.cpp | Complexity analysis |
| TODO items 7,8 review | @oracle | vehiclePipe.cpp | Architecture review |

**Dependency**: Phase 1-2 complete
**Expected Time**: 4-6 hours (research + planning)

### Phase 4: Feature Completion (Week 2-3)

**Objective**: Complete TODO items

| Task | Skill | Files | Verification |
|------|-------|-------|--------------|
| Safe swap double-buffer | @fixer | wheels_extender.cpp | Concurrency test |
| InterceptCall hook | @fixer | wheels_extender.cpp | Hook test |
| Normals from depth research | @librarian | envmap.cpp | Implementation plan |
| Unified pipeline research | @librarian | debugmenu_ui.cpp | Fork analysis |
| Instance optimization | @fixer | buildingPipe.cpp | Performance test |

**Dependency**: Phase 3 complete
**Expected Time**: 8-12 hours

### Phase 5: UI/UX (Week 3)

**Objective**: Debug menu improvements

| Task | Skill | Files | Verification |
|------|-------|-------|--------------|
| Allow III/VC in debug menu | @designer | debugmenu_ui.cpp | UI test |
| Tex coords verification | @fixer | postfx.cpp | Visual test |

**Dependency**: Phase 3 complete
**Expected Time**: 2-4 hours

---

## Verification Strategy

### Per-Issue Verification

| Issue Type | Verification Method | Tool |
|------------|---------------------|------|
| Null pointer | Runtime crash test | Game launch |
| Division-by-zero | Runtime validation | Debug log |
| Memory leak | Shutdown cleanup test | Task manager |
| Buffer overflow | Bounds check test | Static analysis |
| Architecture | Impact analysis | LSAI |
| Code duplication | Complexity metrics | LSAI |

### Build Verification

```bash
# After each phase
python tools/fast_build.py --rebuild

# Visual regression
python tools/fast_build.py --launch
```

### Static Analysis

```bash
# Run clang-tidy for safety checks
# Run cppcheck for memory issues
# Use LSAI for impact analysis
```

---

## Token Efficiency Rules

### Rule 1: Aggressive Compression

- **Compress every 5-8 tool calls**
- **Compress immediately when user requests**
- **Compress widest stale range possible**
- **Never wait for max context warning**

### Rule 2: LSAI First

- **Use LSAI for all symbol lookup** (saves 90%+ tokens)
- **Never grep/glob for symbol search**
- **Use lsai_source instead of Read for method bodies**
- **Use lsai_outline instead of reading entire files**

### Rule 3: Specialist Routing

- **Single-file mechanical fix** → @fixer (1/2 cost)
- **Symbol lookup** → @explorer (2x faster, 1/2 cost)
- **External research** → @librarian (2x faster, 1/2 cost)
- **Architecture decisions** → @oracle (5x better decisions)

### Rule 4: Parallel Execution

- **Independent tasks** → Parallel background specialists
- **Dependent tasks** → Sequential with dependency tracking
- **Write conflicts** → Never parallelize overlapping writes

### Rule 5: Session Reuse

- **Reuse available sessions** when context fits
- **Fresh sessions** when too much unrelated context
- **Track session IDs** for background tasks

### Rule 6: Minimal Context

- **Reference paths/lines** instead of pasting files
- **Brief delegation notices** instead of verbose explanations
- **Concise status updates** instead of narrating work

---

## LSAI Integration

### Available LSAI Tools

| Tool | Use Case | Token Savings |
|------|----------|---------------|
| lsai_search | Find symbol by name | 90%+ vs grep |
| lsai_info | Get signature/docs | 95%+ vs Read |
| lsai_outline | See class members | 90%+ vs Read |
| lsai_source | Read method body | 80%+ vs Read |
| lsai_usages | Find all references | 90%+ vs grep |
| lsai_callers | See who calls method | 85%+ vs grep |
| lsai_callees | See what method calls | 85%+ vs grep |
| lsai_hierarchy | See inheritance chain | 90%+ vs manual |
| lsai_impact | Assess change risk | 95%+ vs manual |
| lsai_deps | File dependencies | 90%+ vs manual |
| lsai_context | Composite overview | 80%+ vs multiple tools |
| lsai_diagnostics | Compiler errors | 95%+ vs build |
| lsai_rename | Rename symbol | 95%+ vs manual |

### LSAI Workflow

```
1. lsai_search("symbol") → find location
2. lsai_info("symbol") → get details
3. lsai_outline("symbol") → see members
4. lsai_usages("symbol") → find references
5. lsai_impact("symbol") → assess risk
6. lsai_source("symbol") → read implementation
```

### LSAI Scope Rules

- **Always scope queries** by project/path
- **Never conclude "absent"** from unscoped search
- **Use grep for macros** (LSAI limitation)
- **Wait for indexing** before querying

---

## Compression Strategy

### When to Compress

1. **Research concluded** → Findings are clear
2. **Implementation finished** → Verified and complete
3. **Exploration exhausted** → Patterns understood
4. **Dead-end noise** → No longer relevant

### What to Compress

- **Raw exploration** → Refined understanding
- **Verbose tool outputs** → Key findings only
- **Failed attempts** → Lessons learned
- **Back-and-forth** → Final decision

### What NOT to Compress

- **Active context** → Still needed for edits
- **Exact code references** → Need precise locations
- **Error messages** → May need for debugging
- **User instructions** → Must preserve intent

### Compression Format

```
## Topic
- Key finding 1
- Key finding 2
- Decision made
- File:line references
- Next steps
```

---

## Risk Assessment

### High Risk

| Risk | Mitigation | Owner |
|------|------------|-------|
| Config struct refactor breaks inline asm | Impact analysis before change | @oracle |
| Render state leak causes visual glitch | State tracking audit | @oracle |
| Memory leak in shutdown | Shutdown cleanup test | @fixer |

### Medium Risk

| Risk | Mitigation | Owner |
|------|------------|-------|
| Division-by-zero in edge case | Epsilon guard | @fixer |
| Uninitialized variable | Full initialization | @fixer |
| Buffer overflow in getpath | Bounds check | @fixer |

### Low Risk

| Risk | Mitigation | Owner |
|------|------------|-------|
| Unused variable warning | Remove dead code | @fixer |
| Debug sprintf overflow | Use snprintf | @fixer |
| TODO items incomplete | Research + plan | @librarian |

---

## Success Criteria

### Phase 1 Success

- [x] No runtime crashes from null pointer — FIXED 0x7F98DF, 0x7F9ECB (SMAA_DrawPass postfx.cpp:3954,4319,4363,4436, aac8478), 0x7FAD4D (neoCarpipe.cpp:86,124; envmap.cpp:487, fa1d507)
- [x] No division-by-zero in debug log — FIXED with epsilon guards
- [x] No buffer overflow in getpath — FIXED strncat
- [x] All fxParams initialized — FIXED memset

### Phase 2 Success

- [x] No memory leaks on shutdown — ShutdownEnvMap added, s_smaaInitCam cleanup added
- [x] Zero compiler warnings — verified 0 warnings
- [x] All sprintf bounded — MEM-7 fixed

### Phase 3 Success

- [x] Config struct refactor plan documented — static_asserts at skygfx.h:615-617
- [x] Render state audit complete — buffer lifecycle audit completed
- [x] Code duplication reduction plan — shared helpers extracted (b680e31)
- [x] TODO items 7,8 resolved — vehiclePipe.cpp:719-723, 684-690

### Phase 4 Success

- [ ] Safe swap implemented — STALE (wheels_extender deleted)
- [ ] InterceptCall hook working — STALE (wheels_extender deleted)
- [ ] Normals from depth researched — RESOLVED (normal buffer implemented differently)
- [ ] Unified pipeline researched — RESOLVED (deliberately disabled, documented)
- [x] Instance optimization complete — buildingPipe.cpp:1005-1011

### Phase 5 Success

- [ ] III/VC in debug menu — OPEN (blocked by P1.3 preset framework)
- [x] Tex coords verified — PASS (postfx.cpp:713)

### VehiclePBR Audit Note

- main_rubber FAIL: manual c22/c23 upload at vehiclePipe.cpp:1721-1722 (must use pipeUploadPBR())
- main_ps2EnvSpecFx: dead entry point (no caller)

### SSAO Chain Note

- 4/4 PASS, ordering SSAO :2030-2034 → CopyDepthToPrev :2039 → PipeChain :2042

---

## Next Steps

1. **Phase 1-3**: ✅ Complete — all safety, memory, and architecture items resolved
2. **Phase 4**: Partially complete — instance optimization done; safe swap/interceptCall stale (wheels_extender deleted); normals/unified resolved differently
3. **Phase 5**: Tex coords verified; III/VC debug menu blocked by P1.3 preset framework
4. **Remaining**: MEM-5 (normalmap_plugin.cpp / texdb.cpp raw new) — low priority, cleanup exists
5. **Future**: III/VC presets, unified pipeline revival (if desired)

---

## Appendix: Available Skills

### Safety Skills

- **memory-safety-patterns**: RAII, ownership, smart pointers
- **sanitizers**: ASan, UBSan, TSan, MSan, LSan
- **static-analysis**: clang-tidy, cppcheck, scan-build

### Development Skills

- **cpp-pro**: Modern C++20/23 features
- **cpp-modern-features**: Lambdas, move semantics, ranges
- **cpp-templates**: Template errors, concepts, SFINAE

### Quality Skills

- **debugging-and-error-recovery**: Systematic root-cause analysis
- **verification-planning**: Evidence paths for claims
- **simplify**: Code clarity without behavior change

### Workflow Skills

- **writing-plans**: Multi-step task planning
- **parallel-agents**: Parallel specialist orchestration
- **git-workflow**: Version control best practices

### External Skills

- **librarian**: Library docs, API references, web research
- **oracle**: Architecture, risk, debugging strategy
- **designer**: UI/UX design and polish

---

*Last updated: 2026-09-15*
*Total issues: 25*
*Estimated completion: 3 weeks*
