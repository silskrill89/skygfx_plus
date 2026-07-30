# Workflow Plan — skygfx_plus_expIV

## Executive Summary

This plan addresses all issues found in the codebase analysis, assigns appropriate skills, and establishes workflow rules for token efficiency. The project has **9 TODO items**, **4 memory management issues**, **4 null pointer/division risks**, **2 uninitialized variable issues**, **3 buffer overflow risks**, and **5 architecture concerns**.

---

## Issue Registry

### Priority 1: HIGH SEVERITY

| ID | Issue | File | Line | Skill | Verification |
|----|-------|------|------|-------|--------------|
| ARCH-1 | Config struct ~250 fields, layout-sensitive via inline asm | skygfx.h | 202-478 | @oracle | Static analysis + impact analysis |
| ARCH-2 | Code duplication ~1500 lines across 7 vehicle callbacks | vehiclePipe.cpp | multiple | @fixer | Build + visual regression |

### Priority 2: MEDIUM SEVERITY

| ID | Issue | File | Line | Skill | Verification |
|----|-------|------|------|-------|--------------|
| MEM-1 | Unguarded RwV3dNormalize (4 locations) | vehiclePipe.cpp | 193,268,345 | @fixer | Unit test + runtime check |
| MEM-2 | Division-by-zero risks (3 locations) | postfx.cpp | 638,643,1065 | @fixer | Runtime validation |
| MEM-3 | fxParams.fxSwitch uninitialized | vehiclePipe.cpp | 458,649 | @fixer | Static analysis |
| MEM-4 | Unsafe strcat in getpath | main.cpp | 105 | @fixer | Bounds check test |
| ARCH-3 | Render state leaks (no enforcement) | multiple | - | @oracle | State tracking audit |
| ARCH-4 | Static locals non-reentrant | multiple | - | @oracle | Thread safety review |

### Priority 3: LOW SEVERITY

| ID | Issue | File | Line | Skill | Verification |
|----|-------|------|------|-------|--------------|
| MEM-5 | Raw new without delete (4 allocations) | normalmap_plugin.cpp, texdb.cpp | 822,1146,103,167 | @fixer | Shutdown cleanup |
| MEM-6 | Unused variable 'sub' | vehiclePipe.cpp | 249,323 | @fixer | Compiler warning |
| MEM-7 | sprintf without bounds | PC_PlantsMgr.cpp | 428-451 | @fixer | Debug-only path |
| TODO-1 | Implement safe swap double-buffer | wheels_extender.cpp | 331 | @fixer | Concurrency test |
| TODO-2 | InterceptCall hook | wheels_extender.cpp | 348 | @fixer | Hook validation |
| TODO-3 | Reconstruct normals from depth | envmap.cpp | 629 | @librarian | Research implementation |
| TODO-4 | Instance one set | buildingPipe.cpp | 1021 | @fixer | Performance test |
| TODO-5 | Allow III/VC in debug menu | debugmenu_ui.cpp | 174 | @designer | UI test |
| TODO-6 | Recover unified pipeline | debugmenu_ui.cpp | 301 | @librarian | Research JuniorDjjr fork |
| TODO-7 | Do we always have this? | vehiclePipe.cpp | 72 | @oracle | Architecture review |
| TODO-8 | Is this even needed? | vehiclePipe.cpp | 557 | @oracle | Architecture review |
| TODO-9 | Tex coords correct? | postfx.cpp | 299 | @fixer | Visual verification |

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

- [ ] No runtime crashes from null pointer
- [ ] No division-by-zero in debug log
- [ ] No buffer overflow in getpath
- [ ] All fxParams initialized

### Phase 2 Success

- [ ] No memory leaks on shutdown
- [ ] Zero compiler warnings
- [ ] All sprintf bounded

### Phase 3 Success

- [ ] Config struct refactor plan documented
- [ ] Render state audit complete
- [ ] Code duplication reduction plan
- [ ] TODO items 7,8 resolved

### Phase 4 Success

- [ ] Safe swap implemented
- [ ] InterceptCall hook working
- [ ] Normals from depth researched
- [ ] Unified pipeline researched
- [ ] Instance optimization complete

### Phase 5 Success

- [ ] III/VC in debug menu
- [ ] Tex coords verified

---

## Next Steps

1. **Start Phase 1** — Safety critical fixes
2. **Use LSAI** for all symbol lookups
3. **Compress aggressively** to stay in context
4. **Reuse sessions** when possible
5. **Track progress** in this document

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

*Last updated: 2026-07-30*
*Total issues: 25*
*Estimated completion: 3 weeks*
