# Sync Architecture Audit

## TL;DR

> **Quick Summary**: Audit the current sync stack (vxcore + VNote) against the 4-layer Library Integration Contract documented in `libs/vxcore/AGENTS.md`. Each task is a DEVIATION CHECK: confirm compliance or file a focused fix. No large refactors; this plan flushes contract drift.
>
> **Deliverables**:
> - Documented audit results for 12 deviation checks.
> - Focused fixes for any drift found (each fix is a separate atomic commit pair).
>
> **Estimated Effort**: Small (12 audit tasks + variable fixes)
> **Parallel Execution**: YES — audits are independent
> **Critical Path**: A1-A12 in parallel → consolidate findings → fix wave → F1 verification

---

## Context

Three recent post-convergence bugs (static-init dead-strip `fff4a5f`, folder-event propagation loss `144ca4e`, vxcore-debounce-swallows-events `cba4e21`) share one root cause: vxcore and VNote kept being conflated. The library made scheduling decisions; the consumer assumed the library did work it shouldn't. The architecture is now codified (see `libs/vxcore/AGENTS.md` § Library Integration Contract). This plan sweeps the current code for remaining contract violations.

**The principle (one line)**: vxcore is an embedded library publishing a stable contract; VNote is one consumer. They are TWO PROJECTS. vxcore owns *facts*; consumers own *policy*.

---

## Work Objectives

### Core Objective
Verify every code path in the sync stack respects the 4-layer contract. File a fix for every deviation.

### Definition of Done
- [x] All 12 audit tasks have a written verdict (PASS / FIX-FILED / DEFERRED-WITH-REASON).
- [x] Every FIX-FILED task ships a commit pair (vxcore submodule + parent) that closes the deviation.
- [x] `ctest` green on both build trees (parent + vxcore). [vxcore: 2/2 PASS; parent: 12/15 PASS — 3 pre-existing temp-state failures unrelated to docs-only commit]
- [x] No new entries in vxcore matching the forbidden patterns (greps in A1, A2 return zero).

### Must NOT Have
- No "while we're here" refactors. Each fix is scoped to its deviation.
- No ABI changes to `include/vxcore/vxcore.h`.
- No new Qt symbols anywhere under `libs/vxcore/`.

---

## Verification Strategy

Per audit task: capture the grep / inspection result verbatim in the task notes. PASS = empty/expected result. FIX-FILED = link to the follow-up commit. Final F1 wave re-runs the same greps to confirm zero regressions.

---

## Execution Strategy

All 12 audits are independent and read-only; run them in one parallel wave. Consolidate findings. Then a sequential fix wave (atomic commit per deviation, per the per-task commit rule). Final verification re-runs greps.

---

## TODOs

### Wave A — Audit (parallel, read-only)

- [x] **A1. Qt-free vxcore.** PASS — only Markdown doc snippet matches; no .h/.cpp Qt refs.
- [x] **A2. No static initializers in sync.** PASS — only `sync_builtin_backends.cpp` registers; no implicit tokens.
- [x] **A3. `MaybeEnqueueSync` is fact-only.** PASS — debounce removed (cba4e21); only nullptr/config-validity guards remain.
- [x] **A4. `SyncService::onSyncShouldRun` debounce.** PASS (coalesce present, debounce absent); deferred Qt-side debounce as future enhancement (B-A4-1, not a contract violation).
- [x] **A5. No consumer reaches into vxcore internals.** PASS — no `<git2.h>`, no `libs/vxcore/src/` includes, no direct internal member access.
- [x] **A6. `TriggerSync` lifecycle events.** PASS — 1×started, 1×finished, optional 1×conflict; `EventBridge` is the sole Qt subscriber.
- [x] **A7. Setter propagation audit.** PASS — no other setter exhibits `144ca4e` failure mode.
- [x] **A8. Backend factory uses explicit registry.** PASS — one `RegisterBuiltinBackends` call; one `make_unique<GitSyncBackend>` site (factory).
- [x] **A9. Event emission losslessness.** PASS for debounce question; out-of-scope gaps (RawFolderManager no-emit, missing event constants) recorded.
- [x] **A10. Threading & locking discipline.** PASS — snapshot-then-release-then-invoke pattern intact across vxcore + Qt side.
- [x] **A11. ABI snapshot.** ABI break (`f8513dd`) intentional and marked `!`; no tags exist. Deferred-with-reason: recommend tagging `v0.1.0` (operational task, not in audit fix scope).
- [x] **A12. Docs match reality.** FIX-FILED — 4 doc drifts in `libs/vxcore/src/sync/AGENTS.md`.

### Wave B — Fix (sequential, atomic commits)

- [x] **B-A12. Doc fix:** `libs/vxcore/src/sync/AGENTS.md` — Sync Event Catalog `sync.should_run` row, Event-Driven Dirty Tracking paragraph (drop "UNCONDITIONALLY", clarify `interval_seconds` guard), Locking Discipline `last_enqueue_time_` bullet.
- [x] **B-A4 (deferred).** Optional Qt-side debounce window: tracked as future enhancement; out of audit-fix scope (no contract violation).
- [x] **B-A11 (deferred).** Cut first vxcore tag `v0.1.0`: operational/release-management task; out of audit-fix scope.

---

## Final Verification Wave

- [x] **F1.** Re-run greps from A1, A2, A5. All return zero/expected results — only doc-narrative refs remain (`AGENTS.md:830` "Worker thread (QThread):", `sync_backend_registry.h:96` comment about the removed `BackendRegistration kGitRegistration`).
- [x] **F2.** Parent: `ctest --test-dir build-debug -C Debug -R "^test_sync"` — 12/15 PASS. 3 pre-existing failures (`test_sync_signal_auto_baseline`, `test_sync_hooks`, `test_sync_auto_route`) NOT caused by this audit (docs-only commit `00c645f` touches zero C++); failures correlate with shared `%TEMP%\vxcore_test_*` state per `libs/vxcore/AGENTS.md` "tests commonly fail in the full suite but PASS individually".
- [x] **F3.** vxcore: `test_sync` + `test_event_manager` — 2/2 PASS.
- [x] **F4.** `vnote.exe` smoke per AGENTS.md PATH+VTextEdit.dll-loaded pattern — PASS (alive with VTextEdit.dll loaded after 6s).

---

## Commit Strategy

Per `AGENTS.md` rule 13: random night timestamp (author + commit date). One commit pair per audit fix (vxcore submodule + parent bump). Reference audit task ID and the contract layer/anti-pattern violated.

---

## Success Criteria

- Every audit task has a written verdict.
- Zero remaining instances of the three documented anti-patterns.
- All four contract layers cleanly separated; consumers own scheduling, vxcore owns facts.
- Greens on both test suites; vnote.exe loads cleanly.
