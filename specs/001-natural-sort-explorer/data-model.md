# Data Model: Natural Sort in Notebook Explorer

**Branch**: `001-natural-sort-explorer` | **Date**: 2026-03-06

## Summary

This feature requires **no new entities, no schema changes, and no new enum values**. It modifies the comparison behavior of an existing sort mode (`OrderedByName` / `OrderedByNameReversed`) in place.

---

## Existing Entities (unchanged)

### `ViewOrder` enum — `src/core/global.h:144`

```cpp
enum ViewOrder {
  OrderedByConfiguration = 0,
  OrderedByNameReversed,        // ← natural sort (descending) after this change
  OrderedByCreatedTime,
  OrderedByCreatedTimeReversed,
  OrderedByModifiedTime,
  OrderedByModifiedTimeReversed,
  ViewOrderMax
};
```

**No new values added.** `OrderedByName` and `OrderedByNameReversed` retain their integer values, preserving any saved configuration.

---

### `NodeInfo` struct — `src/core/nodeinfo.h`

Key fields used by the sort:

| Field | Type | Role in sort |
|---|---|---|
| `name` | `QString` | The display name compared by `naturalCompare()` |
| `isFolder` | `bool` | Folders sorted before files (existing priority, unchanged) |
| `isExternal` | `bool` | External nodes sorted before indexed nodes (existing priority, unchanged) |

No fields added or removed.

---

## New Utility

### `naturalCompare(const QString&, const QString&)` — `src/utils/stringutils.h`

A free function. Not an entity or stored state.

**Signature**:
```cpp
namespace vnotex {
// Returns true if a < b under natural (human-friendly) sort order.
// Comparison is case-insensitive. Embedded numeric substrings are
// compared as unsigned integers.
bool naturalCompare(const QString &a, const QString &b);
}
```

**Algorithm**:
1. Walk both strings simultaneously, extracting alternating runs of digit characters and non-digit characters.
2. For each pair of segments:
   - Both digit runs → convert to `qulonglong`, compare numerically.
   - Otherwise → compare as substrings with `Qt::CaseInsensitive`.
3. If all common segments are equal, the shorter string sorts first.

**Properties**:
- Deterministic, locale-independent
- Case-insensitive for alphabetic characters (spec FR-002)
- Numeric runs compared as integers, not strings (spec FR-001)
- Pure strings with no numbers → identical ordering to `QString::compare` with `Qt::CaseInsensitive` (spec FR-005)
