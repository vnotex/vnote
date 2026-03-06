# Feature Specification: Natural Sort in Notebook Explorer

**Feature Branch**: `001-natural-sort-explorer`
**Created**: 2026-03-06
**Status**: Draft
**Input**: User description: "Add natural sort to notebook explorer pane so files sort in human-friendly order (Note 1, Note 2, Note 10) instead of lexicographic order (Note 1, Note 10, Note 2)"

**Reference**: [GitHub Issue #2683](https://github.com/vnotex/vnote/issues/2683)

## Overview

Currently, the notebook explorer sorts note and folder names using standard lexicographic (dictionary) order. This causes names containing embedded numbers to appear in a counterintuitive sequence: "Note 1", "Note 10", "Note 2" instead of the human-expected order "Note 1", "Note 2", "Note 10".

Natural sort (also called human-friendly sort) compares embedded numeric substrings as integers rather than character-by-character, producing the order users intuitively expect. The existing By Name (alphabetical) sort option will be upgraded to use natural sort order — no new menu option is introduced.

## Clarifications

### Session 2026-03-06

- Q: Should "Natural Sort" be a new separate menu option, or should it replace the existing alphabetical sort behavior? → A: Natural sort is not a new preference. The existing By Name (alphabetical) sort will use natural sort order. There is no separate "Natural Sort" option and no need for an "unnatural sort" alternative.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Sort Notes with Numeric Names Naturally (Priority: P1)

A user has a notebook with sequentially numbered notes (e.g., "Chapter 1", "Chapter 2", ..., "Chapter 10", "Chapter 11"). When they sort By Name (alphabetically), the explorer displays items in human-expected numeric sequence.

**Why this priority**: This is the core problem reported by users. Without this story, the feature delivers no value.

**Independent Test**: Can be fully tested by creating notes named with embedded numbers, using By Name sort, and verifying the order displayed in the explorer matches human-numeric expectations.

**Acceptance Scenarios**:

1. **Given** a folder containing notes named "Note 1", "Note 10", "Note 2", "Note 20", **When** the user sorts By Name (alphabetically), **Then** the explorer displays them as: "Note 1", "Note 2", "Note 10", "Note 20"
2. **Given** a folder containing items named "Chapter 1", "Chapter 2", ..., "Chapter 12", **When** By Name sort is active, **Then** "Chapter 9" appears before "Chapter 10"
3. **Given** a mix of notes with and without embedded numbers (e.g., "Alpha", "Beta 1", "Beta 2", "Beta 10", "Gamma"), **When** By Name sort is active, **Then** numeric portions are compared as integers and non-numeric parts are compared alphabetically

---

### User Story 2 - Natural Sort Applies to Both Folders and Notes (Priority: P1)

Natural sort must work uniformly for both folders and note files within the explorer, regardless of which explorer mode (single panel or two-panel) is in use.

**Why this priority**: Inconsistent behavior between folders and notes would confuse users; the sort order applies to the whole explorer.

**Independent Test**: Tested by creating numbered subfolders and numbered notes and verifying both are ordered naturally when By Name sort is active.

**Acceptance Scenarios**:

1. **Given** subfolders named "Section 1", "Section 10", "Section 2" and notes named "Note 1", "Note 10", "Note 2", **When** By Name sort is active, **Then** both folders and notes appear in natural numeric order
2. **Given** the two-column explorer view (folders left, notes right), **When** By Name sort is active, **Then** both the folder panel and the note panel apply natural sort order
3. **Given** the single-panel combined explorer view, **When** By Name sort is active, **Then** all items (folders and notes) in the panel are sorted in natural order

---

### User Story 3 - Natural Sort Is Case-Insensitive (Priority: P2)

Natural sort must treat uppercase and lowercase letters equivalently when comparing names.

**Why this priority**: Consistent with existing sort behavior; mixed-case note names are common.

**Independent Test**: Create notes with mixed case names containing numbers and verify case does not affect numeric ordering.

**Acceptance Scenarios**:

1. **Given** notes named "note 1", "Note 10", "NOTE 2", **When** By Name sort is active, **Then** "note 1" and "NOTE 2" appear before "Note 10" regardless of case differences

---

### Edge Cases

- What happens when two notes have identical names differing only in case? The sort remains stable and deterministic.
- What happens with names that contain only numbers (e.g., "001", "002", "010")? They are treated as numeric values: 1, 2, 10.
- What happens with names that start with numbers (e.g., "1 Introduction", "2 Background", "10 Conclusion")? Leading numeric components are compared as integers: 1, 2, 10.
- What happens with notes that have no numbers in their names? They are sorted alphabetically, identical to the previous By Name behavior.
- What happens when the user has "View By Configuration" (manual ordering) selected? Natural sort applies only when By Name sort is active; the manual order is unaffected.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The existing By Name sort option MUST use natural sort order, comparing embedded numeric substrings as integers rather than as character sequences.
- **FR-002**: Natural sort MUST be case-insensitive (names differing only in case compare equal for letter parts).
- **FR-003**: Natural sort MUST preserve the existing priority rules: external (unindexed) nodes appear before indexed nodes, and folders appear before files at the same level.
- **FR-004**: Natural sort MUST apply consistently across all explorer modes (single-panel combined view and two-panel view).
- **FR-005**: Notes with no embedded numbers MUST sort identically under the updated By Name sort as they did under the previous lexicographic By Name sort (no regression for users without numbered notes).

### Key Entities

- **View Order**: The sort mode selected by the user. The existing By Name option is upgraded to use natural sort order; no new sort options are added.
- **Explorer Item**: A note or folder displayed in the notebook explorer. Items have a display name that may contain mixed alphabetic and numeric segments.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: When By Name sort is active, a folder containing notes named "Note 1" through "Note 20" displays them in the order Note 1, Note 2, ..., Note 10, Note 11, ..., Note 20 with 100% accuracy.
- **SC-002**: Natural sort applies to both folders and notes in both single-panel and two-panel explorer modes.
- **SC-003**: Notes with purely alphabetic names (no embedded numbers) sort identically under the updated By Name sort and the previous lexicographic By Name sort, producing no regression for users without numbered notes.

## Assumptions

- Natural sort will use the same case-insensitivity rules as the existing By Name sort.
- Natural sort is applied only to the notes/folders explorer (not the notebook selector panel); this scope can be expanded without impacting the core design.
- Performance is not a distinguishing concern for typical notebook sizes (hundreds to low thousands of notes per folder); no specific performance targets beyond current sort behavior are required.
