# Research: Natural Sort in Notebook Explorer

**Branch**: `001-natural-sort-explorer` | **Date**: 2026-03-06

## Findings

### Decision: Natural sort algorithm approach

**Decision**: Tokenize-and-compare — split each string into alternating alphabetic and numeric segments, then compare segment-by-segment (numeric segments as integers, alphabetic segments case-insensitively).

**Rationale**: This is the standard natural sort algorithm (Strnatcmp), well-understood, correct for all spec edge cases, and trivially implementable in C++14 with Qt string types.

**Alternatives considered**:
- Qt's `QCollator` with `setNumericMode(true)`: Available in Qt6 and handles locale-aware numeric comparison. Rejected because locale-specific behavior may be unpredictable across platforms; the tokenize-and-compare approach gives deterministic, locale-independent results matching user expectations.
- `strnatcmp` (Martin Pool's library): Third-party C library, overkill for this scope.

---

### Decision: Where to implement `naturalCompare()`

**Decision**: New files `src/utils/stringutils.h` and `src/utils/stringutils.cpp`, following the established project pattern (`pathutils`, `fileutils`, `htmlutils`, etc.).

**Rationale**: Placing the function in `src/utils/` makes it independently testable without requiring a GUI proxy model, matches the project's existing utility conventions, and allows future reuse.

**Alternatives considered**:
- Static function inside `notebooknodeproxymodel.cpp`: Simpler (zero new files) but cannot be unit-tested without coupling to the GUI model.

---

### Decision: Scope of change — no enum changes, no UI changes

**Decision**: Modify only `NotebookNodeProxyModel::lessThan()` (cases `OrderedByName` and `OrderedByNameReversed`) to call `naturalCompare()` instead of `QString::compare()`. No new `ViewOrder` enum values, no menu changes, no settings changes.

**Rationale**: The spec clarification (2026-03-06) confirmed that natural sort replaces the existing alphabetical behavior in-place. The `ViewOrder` enum, menus, and configuration persistence are unchanged.

---

### Key file locations

| Role | Path |
|---|---|
| Sort enum (`ViewOrder`) | `src/core/global.h:144` |
| Proxy model (sort logic) | `src/models/notebooknodeproxymodel.cpp:116` (`lessThan`) |
| Proxy model header | `src/models/notebooknodeproxymodel.h` |
| Existing util pattern | `src/utils/pathutils.h/cpp` |
| Test pattern | `tests/utils/test_pathutils.cpp` + `tests/utils/CMakeLists.txt` |
| New utility (to create) | `src/utils/stringutils.h/cpp` |
| New test (to create) | `tests/utils/test_stringutils.cpp` |

---

### Build & test commands

```bash
# Build (from repo root, assumes build/ directory exists)
cmake --build build

# Run all tests
ctest --test-dir build

# Run only the new natural sort unit test
ctest --test-dir build -R test_stringutils
```
