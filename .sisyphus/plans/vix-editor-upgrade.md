# Vix Editor Upgrade Plan

## TL;DR

> **Quick Summary**: Upgrade vix editor with modern C++ refactoring (C++20, RAII), add Undo/Redo system, and improve local rule-based AI while maintaining backward compatibility.
>
> **Deliverables**:
> - Modernized C++ codebase with proper architecture
> - Undo/Redo with 100-level history stack
> - Enhanced AI ghost suggestions with more language support
> - All existing features preserved (backward compatible)
>
> **Estimated Effort**: Medium-Large
> **Parallel Execution**: YES - 3 waves
> **Critical Path**: Refactor infrastructure → Undo/Redo → AI improvements → Final QA

---

## Context

### Original Request
User wants to upgrade vix editor with:
1. Modern C++ refactoring (C++20, RAII, smart pointers)
2. Add Undo/Redo system
3. Improve local rule-based AI (not LLM)
4. Stay backward compatible with existing keybinds

### Interview Summary
**Key Discussions**:
- Priority #1: Undo/Redo system
- Keep rule-based AI, improve locally (more languages, better context)
- Must preserve existing keybinds and UI feel
- Single 404-line class needs modularization

**Research Findings**:
- Current code: Single Vix_ultimate class, manual resource management, uses `using namespace std`
- Build: g++ with Python 3.14 and ncurses linking
- AI: vix_brain.py provides ghost suggestions and C++ linting via Python embedding

### Metis Review
**Identified Gaps** (addressed):
- Missing acceptance criteria for AI improvements → Added specific language list and metrics
- Scope creep risk on "better AI" → Bounded to existing algorithm with enhancements
- Undo/Redo specifics undefined → Defined: 100-level in-memory, all edits undoable, capped

---

## Work Objectives

### Core Objective
Transform vix editor from a monolithic C++17 codebase into a modern C++20 editor with proper architecture while adding the missing Undo/Redo feature and improving the existing AI layer.

### Concrete Deliverables
- `src/` reorganized with separate modules (Editor, Buffer, History, AI)
- Undo/Redo system with command pattern
- Enhanced vix_brain.py with more language support
- Build passes with `-std=c++20`
- All existing keybinds work identically

### Definition of Done
- [ ] `vix` launches and displays editor
- [ ] Open file, make changes, undo them, redo them
- [ ] Ghost suggestions work for C++, Python, JavaScript, Rust
- [ ] All existing keybinds (Ctrl+S, Ctrl+Q, etc.) work as before
- [ ] Build completes without warnings

### Must Have
- Undo/Redo with at least 100 levels
- Modern C++ (no raw pointers, proper RAII)
- Backward compatible keybinds
- Enhanced AI for C++, Python, JavaScript, Rust, Go

### Must NOT Have (Guardrails)
- LLM integration (explicitly excluded by user)
- Tab/multi-file support (scope creep - deferred to future)
- Plugin system (scope creep - deferred to future)
- Breaking changes to keybinds

---

## Verification Strategy

### Test Decision
- **Infrastructure exists**: NO
- **Add test infrastructure**: YES - GoogleTest for C++ unit tests
- **Agent-Executed QA**: ALWAYS - Terminal UI requires hands-on verification

### QA Policy
Every task includes agent-executed QA scenarios. Evidence saved to `.sisyphus/evidence/`.

**Verification Method**: interactive_bash (tmux) for terminal UI testing
- Launch `vix` in tmux session
- Simulate keypresses with send-keys
- Verify screen content matches expected
- Capture screenshots of key states

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (Foundation - Start Immediately):
├── Task 1: Set up C++20 build configuration
├── Task 2: Create modular directory structure
├── Task 3: Extract Buffer class from Vix_ultimate
├── Task 4: Create History class (undo/redo infrastructure)
└── Task 5: Set up GoogleTest

Wave 2 (Core Features - After Wave 1):
├── Task 6: Implement Command pattern for undo/redo
├── Task 7: Integrate undo/redo into editor
├── Task 8: Add proper resource management (RAII)
├── Task 9: Enhance vix_brain.py - more languages
└── Task 10: Improve suggestion algorithm

Wave 3 (Polish - After Wave 2):
├── Task 11: Add keyboard shortcuts documentation
├── Task 12: Final integration testing
├── Task 13: Performance verification
└── Task 14: Code review and cleanup
```

### Dependency Matrix
- **1-5**: - - (can start immediately)
- **6**: 4 - 7, 8
- **7**: 6 - 12
- **8**: 3, 6 - 12
- **9**: - - 10
- **10**: 9 - 12
- **11-14**: 7, 8, 10 - (final verification)

---

## TODOs

- [x] 1. Set up C++20 build configuration

  **What to do**:
  - Update build.sh to use `-std=c++20`
  - Add C++20 feature checks (concepts, std::format)
  - Verify GCC version supports C++20
  - Add compiler warning flags (-Wall -Wextra -Wpedantic)

  **Must NOT do**:
  - Change the executable output name
  - Modify source files yet

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Simple build configuration task
  - **Skills**: [`build-systems/cmake`, `build-systems/make`]
  - **Skills Evaluated but Omitted**: None needed

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 2-5)
  - **Blocks**: None
  - **Blocked By**: None

  **References**:
  - `build.sh` - Current build script to modify
  - GCC C++20 support: https://gcc.gnu.org/projects/cxx-status.html#cxx20

  **Acceptance Criteria**:
  - [ ] Build script updated with `-std=c++20`
  - [ ] `g++ --version` confirms GCC 10+ available
  - [ ] `bash build.sh` completes without new warnings

  **QA Scenarios**:
  ```
  Scenario: Verify C++20 build works
    Tool: Bash
    Preconditions: Clean build directory
    Steps:
      1. Run: bash /home/jadkeskes/Documents/vix-editor/build.sh
      2. Check output for "c++20" or "c++2a" in compiler command
      3. Verify binary created at /home/jadkeskes/Documents/vix-editor/vix
    Expected Result: Build succeeds, binary exists
    Evidence: .sisyphus/evidence/task-1-build-cpp20.log
  ```

  **Commit**: YES
  - Message: `build: enable C++20 standard`
  - Files: `build.sh`

---

- [x] 2. Create modular directory structure
- [x] 3. Extract Buffer class from Vix_ultimate
- [x] 4. Create History class (undo/redo infrastructure)
- [x] 5. Set up GoogleTest
- [x] 6. Implement Command pattern for undo/redo
- [x] 7. Integrate undo/redo into editor
- [x] 8. Add proper resource management (RAII)
- [x] 9. Enhance vix_brain.py - more languages
- [x] 10. Improve suggestion algorithm
- [x] 11. Add keyboard shortcuts documentation
- [x] 12. Final integration testing
- [x] 13. Performance verification
- [x] 14. Code review and cleanup

  **What to do**:
  - Run clang-format on all files
  - Review for any TODO/FIXME comments
  - Check for code smells
  - Ensure consistent style

  **Must NOT do**:
  - Make functional changes

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: Cleanup
  - **Skills**: [`code-review`]
  - **Skills Evaluated but Omitted**: None

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3
  - **Blocks**: None
  - **Blocked By**: Tasks 7, 8, 10

  **Acceptance Criteria**:
  - [ ] clang-format passes
  - [ ] No TODO/FIXME in critical paths
  - [ ] Code follows style guide

  **Commit**: YES
  - Message: `style: format and cleanup`
  - Files: All source files

---

## Final Verification Wave

- [ ] F1. **Plan Compliance Audit** — `oracle`
  Read the plan end-to-end. For each "Must Have": verify implementation exists. For each "Must NOT Have": search codebase for forbidden patterns.
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

- [ ] F2. **Code Quality Review** — `unspecified-high`
  Run `g++ -std=c++20 -Wall -Wextra` + static analysis. Review all changed files for: raw pointers, missing RAII, using namespace in headers.
  Output: `Build [PASS/FAIL] | Static Analysis [PASS/FAIL] | VERDICT`

- [ ] F3. **Real Manual QA** — `unspecified-high`
  Execute EVERY QA scenario from EVERY task. Test undo/redo extensively. Test all language suggestions. Verify backward compatibility.
  Output: `Scenarios [N/N pass] | Integration [N/N] | VERDICT`

- [ ] F4. **Scope Fidelity Check** — `deep`
  For each task: read "What to do", read actual diff. Verify nothing beyond scope was built. Check no LLM, no tabs, no plugin system added.
  Output: `Tasks [N/N compliant] | Scope Creep [CLEAN/N issues] | VERDICT`

---

## Commit Strategy

- **Wave 1**: 5 commits (cpp20, structure, buffer, history, tests)
- **Wave 2**: 5 commits (commands, integration, raii, ai-enhance, algo)
- **Wave 3**: 4 commits (docs, integration-test, perf, cleanup)
- **Total**: 14 commits

---

## Success Criteria

### Verification Commands
```bash
bash build.sh  # Must complete without errors
./vix --help || ./vix  # Launches editor
# In editor:
# - Open file with :e test.js
# - Type "fun" → sees "ction" suggestion
# - Type "hello", Ctrl+Z → undoes, Ctrl+Y → redoes
# - Ctrl+Q to quit
```

### Final Checklist
- [ ] All "Must Have" present
- [ ] All "Must NOT Have" absent
- [ ] C++20 builds without warnings
- [ ] Undo/Redo works with 100+ levels
- [ ] AI suggestions work for C++, Python, JS, Rust, Go
- [ ] All existing keybinds work identically
