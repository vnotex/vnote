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
- [ ] All 12 audit tasks have a written verdict (PASS / FIX-FILED / DEFERRED-WITH-REASON).
- [ ] Every FIX-FILED task ships a commit pair (vxcore submodule + parent) that closes the deviation.
- [ ] `ctest` green on both build trees (parent + vxcore).
- [ ] No new entries in vxcore matching the forbidden patterns (greps in A1, A2 return zero).

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

- [ ] **A1. Qt-free vxcore.** Grep `libs/vxcore/src/` and `libs/vxcore/include/` for `QTimer`, `QThread`, `QObject`, `QString`, `Q_OBJECT`, `#include <Q`. Expected: zero hits. Any hit = contract violation (Layer 3/4 leak).

- [ ] **A2. No static initializers in sync.** Grep `libs/vxcore/src/sync/` for `__attribute__((constructor))`, file-scope `static const` with non-trivial ctors, file-scope `namespace { ... }` registration tokens, and any pattern resembling the old `BackendRegistration`. Expected: only `sync_builtin_backends.cpp` performs explicit registration; no implicit self-registration.

- [ ] **A3. `MaybeEnqueueSync` is fact-only.** Read `libs/vxcore/src/sync/sync_manager.cpp` `MaybeEnqueueSync`. Confirm: marks dirty, emits `sync.should_run`, returns. No `if (now - last < interval)`, no timer arming, no coalescing, no filtering. If any scheduling logic remains, file fix.

- [ ] **A4. `SyncService::onSyncShouldRun` owns debounce/throttle.** Read `src/core/services/syncservice.cpp`. Currently this handler enqueues immediately via `SyncWorkQueueManager` with `coalesceKey="trigger"`. Coalescing is present; explicit debounce (timer-based delay between user save and enqueue) is NOT. File a follow-up task: introduce optional debounce window in `SyncService` (seeded from `SyncConfig::interval_seconds` hint) and document it as the Layer-3 home for this concern.

- [ ] **A5. No consumer reaches into vxcore internals.** Grep `src/` for direct uses of vxcore `SyncManager` methods, `backends_`, `states_`, libgit2 symbols, or includes from `libs/vxcore/src/`. Consumers must only use the C ABI plus `services/syncservice.h`. Any leak = contract violation.

- [ ] **A6. `TriggerSync` emits lifecycle events once each.** Read `SyncManager::TriggerSync`. Confirm exactly one `sync.started`, one `sync.finished`, optional one `sync.conflict` per call. No emission from `MaybeEnqueueSync`, no emission from backends. Cross-check `EventBridge` is the sole Qt subscriber.

- [ ] **A7. Setter propagation audit.** Grep all `Set*Manager(...)` and similar wiring setters in vxcore. For each one that stores a pointer used by sub-objects, confirm the setter walks the sub-objects and propagates. Cross-check the `NotebookManager::SetEventManager` fix in commit `144ca4e` is the template; find any other setter that stores once and forgets to push down.

- [ ] **A8. Backend factory uses explicit registry.** Read `vxcore_context_create`. Confirm exactly one call to `RegisterBuiltinBackends(registry)`. Confirm `sync_manager.cpp` `EnableSyncImpl` factory branch is the only `make_unique<GitSyncBackend>` site; no other construction path exists.

- [ ] **A9. Event emission is lossless.** Audit `FolderManager`, `BufferManager`, `NotebookManager` mutation sites. Every state mutation that consumers might care about MUST emit an event. No "we only emit on Nth call" or "we skip if recent". Catalog any filtering that exists; either justify or file a fix.

- [ ] **A10. Threading & locking discipline.** Re-verify `state_mutex_` rules from `libs/vxcore/src/sync/AGENTS.md` § SyncManager Locking Discipline against current `sync_manager.cpp`: no external calls under lock, no recursive mutex, no per-map mutex creep. Match Qt-side rules in `src/core/services/AGENTS.md` § SyncService.

- [ ] **A11. ABI snapshot.** Diff `include/vxcore/vxcore.h` and `include/vxcore/vxcore_types.h` against the last tagged release. Any signature/enum-value change = ABI break; either revert or bump version intentionally.

- [ ] **A12. Docs match reality.** Read `libs/vxcore/src/sync/AGENTS.md` § Sync Event Catalog and § Event-Driven Dirty Tracking. Confirm narrative matches post-deliverable-1 reality (no debounce gate inside `MaybeEnqueueSync`; consumer owns scheduling). File doc fix for any drift.

### Wave B — Fix (sequential, atomic commits)

- [ ] **B1..Bn. One commit pair per FIX-FILED audit.** Per per-task atomic-commit rule: vxcore submodule commit + parent submodule-bump commit, randomized night timestamp. Reference the audit task ID in the commit message.

---

## Final Verification Wave

- [ ] **F1.** Re-run greps from A1, A2, A5. All return zero/expected results.
- [ ] **F2.** Parent: `cmake --build build-debug --target vnote` + `ctest --test-dir build-debug -C Debug -R "^test_sync"` green.
- [ ] **F3.** vxcore: `ctest --test-dir libs/vxcore/build_test -C Debug -R "^test_sync$"` + `^test_event_manager$` green.
- [ ] **F4.** `vnote.exe` smoke per AGENTS.md PATH+VTextEdit.dll-loaded pattern.

---

## Commit Strategy

Per `AGENTS.md` rule 13: random night timestamp (author + commit date). One commit pair per audit fix (vxcore submodule + parent bump). Reference audit task ID and the contract layer/anti-pattern violated.

---

## Success Criteria

- Every audit task has a written verdict.
- Zero remaining instances of the three documented anti-patterns.
- All four contract layers cleanly separated; consumers own scheduling, vxcore owns facts.
- Greens on both test suites; vnote.exe loads cleanly.
