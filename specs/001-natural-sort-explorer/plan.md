# Implementation Plan: Natural Sort in Notebook Explorer

**Branch**: `001-natural-sort-explorer` | **Date**: 2026-03-06 | **Spec**: `specs/001-natural-sort-explorer/spec.md`
**Input**: Feature specification from `specs/001-natural-sort-explorer/spec.md`

## Summary

Upgrade the existing "By Name" alphabetical sort in the notebook explorer to use natural sort order (embedded numeric substrings compared as integers). The change is entirely internal to `NotebookNodeProxyModel::lessThan()`; no new menu options, enum values, or settings are added. A new `naturalCompare()` utility in `src/utils/stringutils.h/cpp` implements the algorithm and is independently unit-tested.

## Technical Context

**Language/Version**: C++14 with Qt 6.8.3
**Primary Dependencies**: Qt6 (Core, Widgets, Test)
**Storage**: N/A — no persistence changes
**Testing**: QtTest framework via `add_qt_test()` CMake helper; CTest runner
**Target Platform**: macOS, Linux, Windows (cross-platform Qt desktop app)
**Project Type**: Desktop GUI application (Qt/C++)
**Performance Goals**: No specific targets; existing sort performance is sufficient for hundreds–low thousands of items per folder (per spec Assumptions)
**Constraints**: C++14 compatible; Qt6 only; no third-party sorting libraries
**Scale/Scope**: Single `lessThan()` method change + one new utility file pair + one new test file

## Constitution Check

The project constitution file is an unfilled template (no specific governance rules defined). Standard good-engineering practices apply:
- Keep changes minimal and focused on the spec
- Write tests before (or alongside) implementation
- No regressions for existing sort behavior

**GATE PASS**: No constitution violations detected.

## Project Structure

### Documentation (this feature)

```text
specs/001-natural-sort-explorer/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks — not created here)
```

### Source Code (files touched by this feature)

```text
src/utils/
├── stringutils.h        # NEW — naturalCompare() declaration
└── stringutils.cpp      # NEW — naturalCompare() implementation

src/models/
└── notebooknodeproxymodel.cpp   # MODIFIED — lessThan() uses naturalCompare()

tests/utils/
├── test_stringutils.cpp # NEW — unit tests for naturalCompare()
└── CMakeLists.txt       # MODIFIED — add test_stringutils target
```

No other files require modification.

**Structure Decision**: Single-project layout matching existing `src/utils/` + `tests/utils/` pattern.

## Complexity Tracking

No constitution violations requiring justification.

---

## Phase 1 — Implement `naturalCompare()` Utility

**Goal**: Introduce and fully test the natural sort comparison function in isolation, with no changes to the proxy model yet.

### 1.1 Create `src/utils/stringutils.h`

Declare `naturalCompare()` in the `vnotex` namespace:

```cpp
namespace vnotex {
// Returns true if a < b under natural (human-friendly) sort order.
// Comparison is case-insensitive. Embedded numeric substrings are
// compared as unsigned integers rather than lexicographically.
bool naturalCompare(const QString &a, const QString &b);
}
```

### 1.2 Create `src/utils/stringutils.cpp`

Implement the tokenize-and-compare algorithm:

1. Walk both strings simultaneously extracting alternating digit / non-digit runs.
2. For each segment pair:
   - Both digit → convert with `toULongLong()`, compare numerically.
   - Otherwise → compare with `QString::compare(Qt::CaseInsensitive)`.
3. If common prefix is equal, the shorter string is less.

### 1.3 Create `tests/utils/test_stringutils.cpp`

Unit tests covering all spec scenarios and edge cases. Test class pattern follows `TestPathUtils` in `tests/utils/test_pathutils.cpp`.

Test cases (data-driven with `_data()` helpers):

| Test method | Covers |
|---|---|
| `testNumericSequence` | "Note 1" < "Note 2" < "Note 10" < "Note 20" (US-1 scenario 1) |
| `testChapterSequence` | "Chapter 9" < "Chapter 10" (US-1 scenario 2) |
| `testMixedAlphaNumeric` | Alpha, Beta 1, Beta 2, Beta 10, Gamma order (US-1 scenario 3) |
| `testCaseInsensitive` | "note 1" < "NOTE 2" < "Note 10" (US-3 scenario 1) |
| `testLeadingNumbers` | "1 Introduction" < "2 Background" < "10 Conclusion" |
| `testOnlyNumbers` | "001" == 1 < "002" == 2 < "010" == 10 |
| `testPureAlpha` | "Alpha" < "Beta" < "Gamma" (same as lexicographic) |
| `testIdenticalNames` | "Note 1" == "Note 1" returns false for both directions |
| `testEmptyStrings` | "" < "A" and "" < "1" |
| `testSymmetry` | `naturalCompare(a,b) == !naturalCompare(b,a)` for non-equal pairs |

### 1.4 Update `tests/utils/CMakeLists.txt`

Add:
```cmake
add_qt_test(test_stringutils
  SOURCES
    test_stringutils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/stringutils.cpp
  GUILESS
)
```

### Phase 1 Gate

```bash
cmake --build build
ctest --test-dir build -R test_stringutils
```

**Required**: Zero build errors. All `test_stringutils` test cases pass.

---

## Phase 2 — Integrate into `NotebookNodeProxyModel::lessThan()`

**Goal**: Wire `naturalCompare()` into the proxy model, replacing `QString::compare()` for the `OrderedByName` and `OrderedByNameReversed` cases.

### 2.1 Modify `src/models/notebooknodeproxymodel.cpp`

In `lessThan()` at line 142, replace:

```cpp
case ViewOrder::OrderedByName:
  return QString::compare(leftInfo.name, rightInfo.name, Qt::CaseInsensitive) < 0;

case ViewOrder::OrderedByNameReversed:
  return QString::compare(leftInfo.name, rightInfo.name, Qt::CaseInsensitive) > 0;
```

With:

```cpp
case ViewOrder::OrderedByName:
  return naturalCompare(leftInfo.name, rightInfo.name);

case ViewOrder::OrderedByNameReversed:
  return naturalCompare(rightInfo.name, leftInfo.name);
```

Add the include at the top of the file:

```cpp
#include <utils/stringutils.h>
```

No other changes to `notebooknodeproxymodel.cpp`.

### Phase 2 Gate

```bash
cmake --build build
ctest --test-dir build
```

**Required**: Zero build errors. All existing tests pass (no regressions). All `test_stringutils` tests pass.

---

## Acceptance Validation

After Phase 2 gate passes, manually verify in the running application:

1. Open a notebook with notes named "Note 1", "Note 10", "Note 2", "Note 20".
2. Select "By Name" sort → verify display order: Note 1, Note 2, Note 10, Note 20.
3. Create folders named "Section 1", "Section 10", "Section 2" → verify both folders and notes sort naturally.
4. Verify notes with purely alphabetic names ("Alpha", "Beta", "Gamma") still sort correctly.
5. Verify "By Name Reversed" shows Note 20, Note 10, Note 2, Note 1.
