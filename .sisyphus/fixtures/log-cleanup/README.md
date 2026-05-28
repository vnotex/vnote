# VNote Log Cleanup QA Fixture

This fixture provides a hermetic, deterministic VNote launch environment for the log cleanup QA plan.

## Notebooks

| Notebook ID | Type | Sync | State | Purpose |
|---|---|---|---|---|
| `fix-local-001` | Bundled | No | S0 (clean) | Local notebook with 18+ sub-folders for testing SyncFolderToStore coverage |
| `fix-onedrive-002` | Bundled | No | Broken config | Triggers "Failed to load bundled notebook config" Warning (no vx_notebook/config.json) |
| `fix-sync-003` | Bundled | Yes (Git) | S2 (partial) | Sync-enabled but no PAT in keychain; reconcile hits "incomplete" branch |

## Expected Default-Level Log Output

When run with baseline logging rules (`*.debug=false, vnote.*=true`):
- ~12 ±2 Info lines from the fixture loading operations
- 0 sync-success lines (S2 state means reconcile fails on PAT check)
- 1 Warning from fix-onedrive-002 (missing config)

## Usage

```powershell
# Run with default settings (10-second timeout)
powershell -File .sisyphus/fixtures/log-cleanup/run-vnote.ps1 -EvidencePath .sisyphus/evidence/output.txt

# Run with custom log rules
powershell -File .sisyphus/fixtures/log-cleanup/run-vnote.ps1 `
  -EvidencePath .sisyphus/evidence/output.txt `
  -QtLoggingRules "*.debug=false; vnote.*=true" `
  -WaitSeconds 15
```

## Fixture Contents

- `appdata/VNote/vnotex.json` — Main config (theme=pure, no plugins)
- `localappdata/VNote/session.json` — Empty session shell
- `localappdata/VNote/vxsession.json` — Session with 3 notebooks, 2 buffers, 1 workspace
- `notebooks/fix-local-001/` — 18 sub-folders (folder01..folder18) + test.md
- `notebooks/fix-sync-003/` — Sync config + test.md
- `notebooks/fix-onedrive-002/` — Deliberately broken (no config.json)
- `git-remote.git/` — Bare git repo for sync remote
- `runtime/` — Per-run state (created/cleaned by helper, ignored in git)

## Implementation Notes

- Paths in JSON use forward slashes and are relative to repo root (except runtime/)
- UUIDs for buffers and workspace are stable across runs
- No PAT registered for fix-sync-003 → deterministic S2 state
- fix-onedrive-002 has no `vx_notebook/config.json` → deterministic Warning
