# Tasks: Natural Sort in Notebook Explorer

**Input**: Design documents from `specs/001-natural-sort-explorer/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓

**Tests**: Included — plan gates require build + test pass after each phase.

**Organization**: Tasks are grouped by user story. Because US1, US2, and US3 all share the same two implementation units (`naturalCompare()` + `lessThan()` update), foundational tasks are isolated in Phase 2 and story tasks focus on their specific test scenarios and integration steps.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on in-progress tasks)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)
- Exact file paths are included in every description

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Declare the utility interface and register the new test target before any implementation begins. These two tasks are independent and can run in parallel.

- [x] T001 [P] Create `src/utils/stringutils.h` — declare `naturalCompare(const QString &a, const QString &b)` in the `vnotex` namespace with include guard and `#include <QString>`
- [x] T002 [P] Add `test_stringutils` target to `tests/utils/CMakeLists.txt` using `add_qt_test()`, linking `src/utils/stringutils.cpp`, flagged `GUILESS`

---

## Phase 2: Foundational (Blocking Prerequisite for All User Stories)

**Purpose**: Implement `naturalCompare()` — the shared algorithm that all three user stories depend on.

**⚠️ CRITICAL**: No user story test or integration work can begin until T003 is complete.

- [x] T003 Create `src/utils/stringutils.cpp` — implement the tokenize-and-compare algorithm: walk both strings extracting alternating digit / non-digit runs; compare digit runs as `qulonglong`; compare non-digit runs with `Qt::CaseInsensitive`; shorter string wins if common prefix is equal

**Checkpoint**: Foundation ready — compile-check only: `cmake --build build`

---

## Phase 3: User Story 1 — Sort Notes with Numeric Names Naturally (Priority: P1) 🎯 MVP

**Goal**: "By Name" sort displays "Note 1", "Note 2", "Note 10", "Note 20" in that order instead of lexicographic order.

**Independent Test**: Create notes named "Note 1", "Note 10", "Note 2", "Note 20" in a test notebook; select "By Name" sort; verify display order matches human-numeric expectations.

### Tests for User Story 1

> **Write these tests FIRST — they must FAIL before T005**

- [x] T004 [US1] Create `tests/utils/test_stringutils.cpp` — add data-driven test class `TestStringUtils` (pattern: `TestPathUtils`) with US1 scenarios:
  - `testNumericSequence` / `testNumericSequence_data`: "Note 1" < "Note 2" < "Note 10" < "Note 20"
  - `testChapterSequence` / `testChapterSequence_data`: "Chapter 9" < "Chapter 10" < "Chapter 12"
  - `testLeadingNumbers` / `testLeadingNumbers_data`: "1 Introduction" < "2 Background" < "10 Conclusion"
  - `testOnlyNumbers` / `testOnlyNumbers_data`: "001" < "002" < "010" (treated as 1 < 2 < 10)
  - `testPureAlpha` / `testPureAlpha_data`: "Alpha" < "Beta" < "Gamma" (same as lexicographic)
  - `testSymmetry`: `naturalCompare(a, b) == !naturalCompare(b, a)` for all non-equal pairs

### Implementation for User Story 1

- [x] T005 [US1] Integrate `naturalCompare()` into `src/models/notebooknodeproxymodel.cpp`: add `#include <utils/stringutils.h>` at top; in `lessThan()` replace `QString::compare(leftInfo.name, rightInfo.name, Qt::CaseInsensitive) < 0` with `naturalCompare(leftInfo.name, rightInfo.name)` for the `OrderedByName` case

### Phase 3 Gate

- [x] T006 [US1] Run gate: `cmake --build build && ctest --test-dir build -R test_stringutils` — zero build errors, all test_stringutils cases pass

**Checkpoint**: US1 fully functional — "By Name" sort is now natural sort for notes with embedded numbers.

---

## Phase 4: User Story 2 — Natural Sort Applies to Both Folders and Notes (Priority: P1)

**Goal**: Both folders and notes sort naturally under "By Name"; the reversed "By Name" variant also sorts naturally in descending order.

**Independent Test**: Create numbered subfolders ("Section 1", "Section 10", "Section 2") and notes ("Note 1", "Note 10", "Note 2"); activate "By Name" sort in both single-panel and two-panel explorer; verify both panels show natural order.

### Tests for User Story 2

- [x] T007 [US2] Add US2 test cases to `tests/utils/test_stringutils.cpp`:
  - `testMixedAlphaNumeric` / `testMixedAlphaNumeric_data`: "Alpha" < "Beta 1" < "Beta 2" < "Beta 10" < "Gamma"
  - `testIdenticalNames`: `naturalCompare("Note 1", "Note 1")` returns `false` (strict weak ordering)
  - `testEmptyStrings` / `testEmptyStrings_data`: `""` < `"A"`, `""` < `"1"`

### Implementation for User Story 2

- [x] T008 [US2] Update `src/models/notebooknodeproxymodel.cpp` `lessThan()` — replace `OrderedByNameReversed` case's `QString::compare(...) > 0` with `naturalCompare(rightInfo.name, leftInfo.name)` (arguments reversed for descending order)

### Phase 4 Gate

- [x] T009 [US2] Run gate: `cmake --build build && ctest --test-dir build` — zero build errors, all tests pass (US1 + US2 scenarios)

**Checkpoint**: US1 and US2 both functional — natural sort applies to folders and notes in ascending and descending "By Name" order.

---

## Phase 5: User Story 3 — Natural Sort Is Case-Insensitive (Priority: P2)

**Goal**: "note 1" and "NOTE 2" both sort before "Note 10" regardless of case.

**Independent Test**: Notes named "note 1", "Note 10", "NOTE 2" — "By Name" sort produces: note 1, NOTE 2, Note 10.

### Tests for User Story 3

- [x] T010 [US3] Add US3 test cases to `tests/utils/test_stringutils.cpp`:
  - `testCaseInsensitive` / `testCaseInsensitive_data`: "note 1" < "NOTE 2" < "Note 10" (case makes no difference for numeric ordering)
  - `testCaseInsensitiveAlpha` / `testCaseInsensitiveAlpha_data`: "alpha" == "ALPHA" == "Alpha" (all compare equal, stable)

> **Note**: US3 is verified entirely by `naturalCompare()` properties — no additional implementation changes are required beyond what was done in Phases 3–4. If tests were green in Phase 4, only test compilation is needed here.

### Phase 5 Gate

- [x] T011 [US3] Run gate: `cmake --build build && ctest --test-dir build` — zero build errors, all tests pass (US1 + US2 + US3 scenarios)

**Checkpoint**: All three user stories fully functional and tested.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Final validation confirming no regressions in the running application.

- [ ] T012 Manual acceptance validation per `specs/001-natural-sort-explorer/plan.md` Acceptance Validation section: open app, create notes "Note 1" / "Note 10" / "Note 2" / "Note 20", select "By Name", verify order; repeat with folders; verify "By Name Reversed" order; verify purely alphabetic names unchanged
- [ ] T013 [P] Verify no regressions in other sort modes: "By Created Time" and "By Modified Time" still sort correctly after the proxy model change

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — T001 and T002 can start immediately and run in parallel
- **Foundational (Phase 2)**: Depends on T001 (header must exist for `.cpp` to compile) — BLOCKS all story work
- **US1 (Phase 3)**: Depends on Phase 2 — T004 (write tests) and T005 (integrate) can overlap
- **US2 (Phase 4)**: Depends on Phase 3 gate passing (T006)
- **US3 (Phase 5)**: Depends on Phase 4 gate passing (T009)
- **Polish (Phase 6)**: Depends on Phase 5 gate passing (T011)

### User Story Dependencies

- **US1 (P1)**: Starts after Foundational (Phase 2)
- **US2 (P1)**: Starts after US1 gate (T006) — `OrderedByNameReversed` case is one additional line
- **US3 (P2)**: Starts after US2 gate (T009) — pure test addition, no new code

### Parallel Opportunities

- T001 and T002 (Phase 1) can run in parallel — different files
- T007 (US2 tests) can be written during Phase 3 implementation (different file from T005)
- T010 (US3 tests) can be written during Phase 4 (different file from T008)

---

## Parallel Example: Phase 1

```text
# Launch both setup tasks simultaneously:
Task: "Create src/utils/stringutils.h with naturalCompare() declaration" (T001)
Task: "Add test_stringutils to tests/utils/CMakeLists.txt" (T002)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only — ~4 tasks)

1. Complete Phase 1: T001 + T002 (parallel, ~10 min)
2. Complete Phase 2: T003 (implement algorithm, ~20 min)
3. Complete Phase 3: T004 (write tests) → T005 (integrate) → T006 (gate)
4. **STOP and VALIDATE**: US1 independently testable — "By Name" sorts naturally
5. Ship as MVP

### Incremental Delivery

1. T001–T003 → Utility ready
2. T004–T006 → US1 done, gate green (**MVP**) — note order correct in explorer
3. T007–T009 → US2 done, gate green — reversed sort also natural
4. T010–T011 → US3 done, gate green — case insensitivity verified
5. T012–T013 → Polish complete

---

## Notes

- **Total tasks**: 13 (including 3 gate tasks)
- **Gate tasks**: T006, T009, T011 — each requires zero build errors + all tests pass
- **Test tasks**: T004, T007, T010 — write tests before corresponding implementation
- [P] tasks = different files, no blocking dependencies
- No new enum values, no UI changes, no settings changes — scope is strictly `stringutils.*` + `notebooknodeproxymodel.cpp`
