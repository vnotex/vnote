---
name: release-version
description: Steps to cut a VNote version release (e.g. v4.3.0) — bump the version with scripts/update_version.py, refresh and fill zh_CN/ja translations via lupdate, write the changes.md changelog, and trigger the CI [Release] build. Use when asked to "release", "cut a release", "bump the version", or "prepare vX.Y.Z".
---

# VNote Version Release

End-to-end checklist for releasing a new VNote version. Replace `X.Y.Z` with the
target version (e.g. `4.3.0`) throughout.

## 0. Preconditions

- You are on `master` (releases are published from `master`), working tree clean
  except for the release changes.
- Decide `X.Y.Z`. Confirm the previous tag with `git tag | Sort-Object -Descending`.
- Submodules are already pinned to their intended commits (see root `AGENTS.md`
  § Submodule Push Discipline — push submodules BEFORE the parent).

## 1. Bump the version (use the script — do NOT hand-edit)

```pwsh
python scripts/update_version.py X.Y.Z
```

`scripts/update_version.py` is the single source of truth. It updates:
- `CMakeLists.txt` — `project(... VERSION X.Y.Z ...)`
- `.github/workflows/ci-win.yml`, `ci-linux.yml`, `ci-macos.yml` — `VNOTE_VER: X.Y.Z`
- `src/data/core/Info.plist` — short (`X.Y`) and full (`X.Y.Z`, `X.Y.Z.1`) strings
- `src/core/configmgr2.cpp` — `ConfigMgr2::c_version{X, Y, Z}`
- `src/data/core/fun.vnote.app.VNote.metainfo.xml` — prepends a dated `<release>` entry

Note: `ci-linux-tsan.yml` is NOT touched by the script (its `VNOTE_VER` is a
build sanity value, not a release version) — leave it alone.

Verify: `git diff --stat` should show exactly the files listed above.

## 2. Update translations (extract, then fill, both locales)

The two maintained catalogs are `src/data/core/translations/vnote_zh_CN.ts`
(Simplified Chinese) and `vnote_ja.ts` (Japanese).

### 2a. Extract new/changed strings with lupdate

`lupdate` ships with Qt. On this machine: `C:\Qt\6.9.3\msvc2022_64\bin\lupdate.exe`
(fall back to `Get-Command lupdate`).

```pwsh
& "C:\Qt\6.9.3\msvc2022_64\bin\lupdate.exe" -no-obsolete -locations relative src `
  -ts src/data/core/translations/vnote_zh_CN.ts src/data/core/translations/vnote_ja.ts
```

- `-no-obsolete` drops entries no longer in the source (keeps the catalog lean).
- Harmless `pdf.js` JS parse errors are expected — lupdate still finishes.
- The summary reports "N new" strings; those become `type="unfinished"`.

### 2b. Fill in the unfinished translations

Every `type="unfinished"` entry must be translated for BOTH locales. Count them:

```pwsh
(Select-String -Path "src/data/core/translations/vnote_zh_CN.ts" -Pattern 'type="unfinished"').Count
(Select-String -Path "src/data/core/translations/vnote_ja.ts" -Pattern 'type="unfinished"').Count
```

Extract the source strings needing translation, then for each `<message>` whose
`<translation type="unfinished">` is empty, provide the localized text and drop
the `type="unfinished"` attribute (`<translation>...</translation>`).

Practical approach: script it. Build a `source -> translation` map per locale and
rewrite each unfinished `<message>` block, escaping `&`/`<`/`>` in the output and
preserving `%1`/`%2` placeholders, `&`-accelerators (e.g. `&View` -> `查看(&V)` /
`表示(&V)`), and literal newlines. Re-run the count above; both must reach `0`.

The `.qm` binaries are generated at build time by the `lrelease` CMake target
(see `src/CMakeLists.txt`), so you do NOT commit `.qm` files.

## 3. Write the changelog

Prepend a new `## vX.Y.Z` section at the TOP of `changes.md` (right under the
`# Changes` header, above the previous version).

- Summarize `git log <prev-tag>..HEAD --oneline` grouped by theme (Editor, Export,
  Tasks, Fixes, Security, Translations, …), matching the style of existing entries.
- Lead with a one-line summary sentence "… on top of VNote <prev>:".
- Always end with a **Translations** bullet noting zh_CN + ja were updated.

## 4. Review

Per repo `AGENTS.md` rule 17, delegate to the `review` subagent (Task tool) for a
read-only second opinion on the release diff before finalizing.

## 5. Commit and trigger the release

CI publishes a (draft) GitHub release from `master` ONLY when the head commit
message starts with `[Release]` (see the `Release` job in each `ci-*.yml`;
condition: `github.ref == 'refs/heads/master' && startsWith(head_commit.message, '[Release]')`).
It creates tag `vX.Y.Z` and uploads the platform artifacts.

- Commit message MUST start with `[Release]`, e.g. `[Release] VNote X.Y.Z`.
- Follow repo `AGENTS.md` rule 13 for author/commit date (night-time), and only
  commit when the user explicitly asks.
- If submodule pointers moved, push submodules first, then the parent (rule +
  root `AGENTS.md` § Submodule Push Discipline).

## 6. Once CI is green, assemble the draft release

The `[Release]` commit makes CI create a **draft** GitHub release for tag `vX.Y.Z`.
Wait until ALL platform jobs are green, then make sure the draft carries the four
release artifacts before publishing. The `ncipollo/release-action` step in each
`ci-*.yml` uploads its own platform's asset directly, but confirm all four are
present (and if any job's upload was skipped/failed, download that job's build
artifact and attach it manually).

The four artifacts (with `X.Y.Z` substituted):

| Platform | Artifact file | Produced by |
|----------|---------------|-------------|
| Linux | `VNote-X.Y.Z-linux-x64.AppImage` | `ci-linux.yml` |
| macOS | `VNote-X.Y.Z-mac-<arch>.dmg` (universal) | `ci-macos.yml` |
| Win64 (Qt 6) | `VNote-X.Y.Z-win64.zip` | `ci-win.yml` (suffix `""`) |
| Windows 7 (Qt 5.15) | `VNote-X.Y.Z-win64-windows7.zip` | `ci-win.yml` (suffix `-windows7`) |

Watch the runs and confirm the draft, using `gh`:

```pwsh
# Watch the release-triggering runs on master until they finish.
gh run list --branch master --limit 8
gh run watch <run-id>

# Inspect the draft release and its currently-attached assets.
gh release view vX.Y.Z
```

If an asset is missing, download it from the corresponding workflow run and
upload it to the draft:

```pwsh
# Download the build artifact(s) from a finished run into ./_artifacts.
gh run download <run-id> -D _artifacts

# Attach a missing asset to the draft release (repeat per file).
gh release upload vX.Y.Z "_artifacts\<path>\VNote-X.Y.Z-...zip" --clobber
```

### Set the release description from `changes.md`

The draft's body MUST be the `## vX.Y.Z` section of `changes.md` (the same
changelog written in step 3) — nothing more, nothing less. CI seeds a generic
body, so overwrite it. Extract exactly that one section (from its `## vX.Y.Z`
heading up to, but not including, the next `## ` heading) and set it as the notes:

```pwsh
# Extract the ## vX.Y.Z section into a temp notes file...
$ver = "X.Y.Z"
$md  = Get-Content changes.md -Raw
$sec = [regex]::Match($md, "(?ms)^## v$([regex]::Escape($ver))\b.*?(?=^## |\z)").Value.TrimEnd()
Set-Content -Path notes.md -Value $sec -NoNewline -Encoding utf8

# ...and apply it as the draft's description.
gh release edit vX.Y.Z --notes-file notes.md
```

Drop the leading `## vX.Y.Z` line if you prefer the version to appear only as the
release title; keep the bullet body either way. Verify with `gh release view vX.Y.Z`.

### Publish

Only when the draft `vX.Y.Z` release shows all **4** artifacts (linux / macos /
win64 / windows7) AND its body matches the `changes.md` section do you publish it:

```pwsh
gh release edit vX.Y.Z --draft=false
```

Publishing fires the `Gitee Mirror` workflow (`.github/workflows/gitee-mirror.yml`),
which mirrors the release to `gitee.com/vnotex/vnote`. Confirm it started and check
its result:

```pwsh
gh run list --workflow "Gitee Mirror" --limit 3
curl.exe -s "https://gitee.com/api/v5/repos/vnotex/vnote/releases" |
  ConvertFrom-Json | Select-Object tag_name, name
```

Expect exactly one Gitee release with `tag_name = vX.Y.Z`. If no run appeared,
re-drive it manually with `gh workflow run "Gitee Mirror" -f tag=vX.Y.Z`.

## Quick reference

| Step | Command / File |
|------|----------------|
| Version bump | `python scripts/update_version.py X.Y.Z` |
| Extract strings | `lupdate -no-obsolete -locations relative src -ts <zh_CN.ts> <ja.ts>` |
| Fill translations | edit `vnote_zh_CN.ts`, `vnote_ja.ts` until 0 `unfinished` |
| Changelog | prepend `## vX.Y.Z` to `changes.md` |
| Release trigger | commit on `master` with message starting `[Release]` |
| Set description | `gh release edit vX.Y.Z --notes-file notes.md` (the `## vX.Y.Z` section of `changes.md`) |
| Assemble release | wait for green CI, ensure draft `vX.Y.Z` has all 4 artifacts + changelog body, then `gh release edit vX.Y.Z --draft=false` |

## Release artifacts (must all be present before publishing)

1. `VNote-X.Y.Z-linux-x64.AppImage` (linux)
2. `VNote-X.Y.Z-mac-<arch>.dmg` (macos, universal)
3. `VNote-X.Y.Z-win64.zip` (win64, Qt 6)
4. `VNote-X.Y.Z-win64-windows7.zip` (windows7, Qt 5.15)
