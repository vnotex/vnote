# CI, Packaging & Release

This directory holds the GitHub Actions workflows. **This document also governs
[`src/Packaging.cmake`](../src/Packaging.cmake)**, which lives outside this directory and
therefore will not auto-load this file — open it explicitly when touching packaging, the
Windows Qt 6 packages, or updater artifacts.

See also: [../AGENTS.md](../AGENTS.md) (repo-wide rules, submodule push discipline) and
[../src/AGENTS.md](../src/AGENTS.md) (architecture, source-wide Qt patterns).

---

## Build execution

Linux and macOS set `CMAKE_BUILD_PARALLEL_LEVEL=2` for both packaging and test
builds; Make otherwise builds these large targets serially. Keep Cargo's separate
two-job limit. In test-enabled jobs, run both parent and native-editor suites even when one fails,
then fail the step if either returned nonzero. Preserve both original CTest logs
and the Typst JUnit reports before diagnostics; macOS artifact names include the Qt kit.
Upload reports for successful test steps too: a QtTest case can skip inside an otherwise
passing executable, so a green CTest result alone does not prove native input was exercised.
Diagnostic reruns require the test step itself to have failed and a nonempty
`Testing/Temporary/LastTestsFailed.log` in that suite's build tree. Parent and
native-editor histories are independent: `ctest --rerun-failed` without its own
failure history can rerun a passing suite after an unrelated failure.

Linux, Windows and macOS allow 180 minutes for cold builds, full native test suites,
package gates and artifact publication. Keep the individual CTest deadlines; the job
budget is not a test timeout.
macOS creates and unlocks its disposable test keychain only after test compilation,
right before running CTest. Cold builds must not consume the vault's unlock lease;
restore the original keychain before final packaging and signing.

The macOS matrix enables unit/native GUI tests only for Qt 6.10.3. Qt 6.9.3 sets
`VNOTE_BUILD_TESTS=OFF` and skips the test build, execution and disposable test keychain.
The existing `[Release]` condition disables these suites for both kits. Both Qt kits still
build and sign packages and run the signed-package ABI, consumer and WebEngine gates,
including native Intel verification; do not describe Qt 6.9.3 as unit-test validated.

## Interactive CI debugging

Use this when a failure needs the real hosted OS, desktop, Qt kit or CI setup.
`workflow_dispatch` accepts `debug_enabled=true` plus a single `debug_public_key`
(`ssh-ed25519`). `.github/scripts/debug-session.py` uses pinned Upterm 0.39.0 and the
pinned `uptermd.upterm.dev` host-certificate authority. There is no web-shell or
anonymous fallback. Never put a private key into inputs, logs or artifacts.

### Dispatch from the local checkout

The examples use PowerShell 7.3+, an authenticated GitHub CLI, OpenSSH and Python.
Start at the repository root, at the committed revision you intend to investigate.
Create a disposable key outside the checkout; keep diagnostic evidence outside this
key directory so it survives key cleanup:

```powershell
$debugDir = Join-Path ([IO.Path]::GetTempPath()) ('vnote-ci-ssh-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $debugDir | Out-Null
$key = Join-Path $debugDir 'id_ed25519'
ssh-keygen -q -t ed25519 -N '' -C vnote-ci-debug -f $key
if ($LASTEXITCODE -ne 0) { throw 'Key generation failed' }
$publicKey = (Get-Content -LiteralPath "$key.pub" -Raw).Trim()
```

Use a non-`master` branch at that revision. Debugging on `master` can resume into
continuous-release publication with ad-hoc runner edits; debug mode does not disable
publication. Follow the submodule push discipline before publishing the branch:

```powershell
$debugBranch = 'ci-debug-' + (git rev-parse --short HEAD).Trim()
git push --recurse-submodules=check origin "HEAD:refs/heads/$debugBranch"
if ($LASTEXITCODE -ne 0) { throw 'Debug branch push failed' }
gh workflow run ci-win.yml --ref $debugBranch `
  -f debug_enabled=true -f "debug_public_key=$publicKey"
if ($LASTEXITCODE -ne 0) { throw 'Debug dispatch failed' }
```

Choose the workflow and connection artifact for the required platform:

| Workflow | Connection artifact | Pause point |
|----------|---------------------|-------------|
| `ci-win.yml` | `debug-connection-windows` | After tools, before configure/build |
| `ci-linux.yml` | `debug-connection-linux` | After configure, before build and test dependencies |
| `ci-macos.yml` | `debug-connection-macos-<Qt version>` | After configure, before test build/keychain setup |

macOS build jobs also use the `Mac-code-sign` environment. Any configured approval or
branch policy applies before SSH setup; inspect a waiting/blocked job before treating
a missing connection artifact as a helper failure. Do not switch to `master` to bypass
an environment restriction.

Set `$runId` to the ID in the dispatched run URL. If the CLI did not return a URL,
use `gh run list --workflow ci-win.yml --branch $debugBranch --event workflow_dispatch`
and match the intended revision, not an unrelated latest run. Confirm `headSha` and
`headBranch` with `gh run view $runId --json headSha,headBranch,jobs`.

Download once the connection-upload step succeeds; the artifact is available while
the job is still running. Do not wait for workflow completion to obtain it. Each
macOS matrix job has its own session/artifact, including the Qt 6.9 job whose unit
suite is disabled. Account for every paused job. No guest within 15 minutes ends a
session; the normal job deadline also continues to run.

```powershell
$connectionDir = Join-Path $debugDir 'connection'
gh run download $runId --name debug-connection-windows --dir $connectionDir
if ($LASTEXITCODE -ne 0) { throw 'Connection artifact download failed' }
$connection = Get-Content (Join-Path $connectionDir 'connection.json') -Raw | ConvertFrom-Json
ssh-keygen -lf "$key.pub" -E sha256
$connection.keyFingerprint
```

Compare the `SHA256:` fingerprint tokens, not the entire lines: the key comments can
differ. The artifact contains `sshCommand`, `joinCommand`, `keyFingerprint` and
`restoreEnvironment`; it contains no private key. Use fresh metadata for this run,
not an expired session address from an old log.

### Connect with the pinned relay authority

Create a dedicated `known_hosts` from the checked-in helper's pin rather than trusting
`ssh-keyscan`, accepting a new host key interactively, or disabling host-key checks.
`runpy` below loads the definitions without invoking the helper's guarded `main()`:

```powershell
$knownHosts = (Join-Path $debugDir 'known_hosts').Replace('\', '/')
python -c "import runpy,sys;sys.stdout.write(runpy.run_path('.github/scripts/debug-session.py')['KNOWN_HOSTS'])" |
  Set-Content -LiteralPath $knownHosts -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw 'Relay pin extraction failed' }
if ($connection.sshCommand -notmatch '^ssh ([A-Za-z0-9_-]+@uptermd\.upterm\.dev)$') {
  throw 'Unexpected SSH command; inspect connection.json without executing its contents'
}
$destination = $Matches[1]
$sshArgs = @(
  '-F', 'none', '-i', $key,
  '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes', '-o', 'IdentityAgent=none',
  '-o', 'ForwardAgent=no', '-o', 'StrictHostKeyChecking=yes',
  '-o', ('UserKnownHostsFile="' + $knownHosts + '"'), '-o', 'GlobalKnownHostsFile=none'
)
ssh -tt @sshArgs $destination
```

This attaches to the hosted shell (`pwsh -NoProfile` on Windows, clean Bash elsewhere),
initially in `GITHUB_WORKSPACE`. For agent automation, use a named PTY session and
send commands to that shell; wait for an explicit completion marker and exit code.
A live PID, quiet terminal, or SSH disconnect does not prove command completion.

### Restore the environment and reproduce the workflow

The helper removes checkout/submodule HTTP auth and credential-bearing URL rewrites,
scrubs token/secret/key variables from the hosted process, and retains runner orphan
tracking. Do not inject credentials back into the session to bypass this isolation.
Artifact/signing steps keep their normal environment and run after session cleanup.

Upterm redirects three XDG variables for its own state. Before measuring application
behavior, restore the original values in `connection.json`'s `restoreEnvironment`;
JSON `null` means unset, not an empty or literal `null` value. On the Windows runner:

```powershell
$metadata = Join-Path $env:RUNNER_TEMP 'vnote-debug/connection.json'
$original = (Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json).restoreEnvironment
foreach ($entry in $original.PSObject.Properties) {
  if ($null -eq $entry.Value) {
    Remove-Item -LiteralPath ('Env:' + $entry.Name) -ErrorAction SilentlyContinue
  } else {
    [Environment]::SetEnvironmentVariable($entry.Name, $entry.Value, 'Process')
  }
}
$build = Join-Path (Split-Path $env:GITHUB_WORKSPACE -Parent) 'build'
```

On Bash, use `unset` for null entries and `export` with the recorded values otherwise.
Restore only the hosted shell's application environment, not the helper's state files.

Read the checked-out workflow's configure/build/test steps and reproduce their commands,
working directories and **step-local environment**. The build directory is the sibling
`build` of `GITHUB_WORKSPACE`, not a directory inside the checkout.

- **Windows:** the debug shell is not a Visual Studio developer shell. The earlier
  `vcvars64.bat` invocation affected only its own `cmd` process; `VS_CMD` and
  `VNOTE_TESTS` from later steps are not already set. Use the matrix's toolchain and
  import its environment into the hosted shell, or run the entire command chain in
  an initialized `cmd`. Match Release/Ninja and `VNOTE_BUILD_TESTS=ON` for test work.
- **Linux:** test dependencies, D-Bus/keyring setup and the Xvfb/native-platform wrapper
  come after the pause. Reproduce the workflow and `.github/scripts/run-linux-tests.sh`,
  rather than replacing native GUI tests with `offscreen` or `minimal`.
- **macOS:** preserve the configured Qt kit/architectures, normalize `TMPDIR` as the
  workflow does, and create/unlock the disposable test keychain after compilation.
  The Qt 6.9 configuration does not register the unit/native suites.
- **Package-dependent tests:** build/package/extract prerequisites in the original
  order. For example, Windows updater fixtures need the packaged installation; a
  target-only build is not equivalent to the full CI setup. A debug-shell-only failure
  (including PowerShell module lookup) is not automatically the original regression.

### Preserve evidence before rerunning

In the build directory, inspect `ctest --show-only=json-v1` for the executable, arguments, working
directory and test environment. Build the relevant target, then run its registered
CTest command without changing assertions, deadlines or platform behavior. A focused
Windows example, after the workflow's configure/toolchain prerequisites:

```powershell
cmake --build $build --target test_themeservice
if ($LASTEXITCODE -ne 0) { throw 'Theme test build failed' }
ctest --test-dir $build -C Release -R '^test_themeservice$' --output-on-failure --no-tests=error
$testExit = $LASTEXITCODE
Write-Output "DEBUG_CTEST_EXIT=$testExit"
```

Capture the **first** failure to files before standalone repetitions or full-order
experiments overwrite it. Keep CTest's `Testing/Temporary/Last*.log`, QtTest text/JUnit
reports, exit code, revision and relevant environment. The Windows theme registration
already writes `tests/gui/test_themeservice.txt` and `.xml` in the build tree. For other
QtTest executables, use unique `-o <file>,txt` and `-o <file>,junitxml` outputs while
retaining `-o -,txt` and the registered runtime environment. File loggers cannot explain
a failure before QtTest initializes. Inspect skips as well as pass counts.

Export evidence while the session is reachable: from another local terminal with the
same client variables, use `sftp @sshArgs $destination` (without SSH's `-tt`) and download
the files to a separate evidence directory. Resolve remote paths with SFTP's `pwd`/`ls`
instead of guessing Windows drive mappings. Ad-hoc files are not automatically uploaded
by the workflow; normal report-upload steps run only after the debug pause ends.

A relay disconnect can leave the runner and build alive. Check the run's job state and
reattach to the same session if still reachable; inspect the saved completion marker
and files instead of restarting the build blindly. If standalone runs pass, compare
normal CI setup/order before proposing production or timing changes. Ad-hoc debug
passes do not replace a clean CI run of the committed fix.

### Close the session and remove the key

Run `exit 0` **inside the hosted shell** to end it and resume the workflow. Closing the
local SSH client alone merely disconnects; it does not end the pause. Close each macOS
matrix session you opened and account for any other paused matrix jobs.

If the relay cannot be recovered, cancel the job with `gh run cancel $runId`, then
await shutdown with `gh run watch $runId --exit-status` (cancellation returns nonzero).
Verify the `Close SSH debug session` step succeeded via `gh run view $runId --json jobs`;
do not assume a lost connection means the exposed session is gone. Once closure is
confirmed and evidence is saved outside `$debugDir`, remove the local disposable key:

```powershell
Remove-Item -LiteralPath $debugDir -Recurse -Force
```

## Release packaging dependencies

`pack` must build `cmark_exe` before CPack runs: upstream cmark installs its CLI
unconditionally, even though Windows package pruning removes it afterward. Release
CI deliberately skips `all` to avoid building tests, so do not rely on a prior
all-target build or weaken the install/pruning checks to hide a missing executable.

### Native Typst runtime and attribution

VNote requires the pinned Rust 1.99.0/Typst 0.15.1 shared library and Qt PDF. Embedding forces
`VTEXTEDIT_ENABLE_TYPST=ON` and `VTEXTEDIT_BUILD_DEMOS=OFF`; do not ship upstream demos.
`vnote_typst_runtime` always refreshes the native runtime (and VTextEdit.dll on Windows),
even without relinking VNote. Both `pack` and direct `deploy` depend on this target and
`prepare_typst_notices`, retaining the existing cmark/lrelease prerequisites.

Provision `cargo +1.99.0 install cargo-about --locked --version 0.9.2 --features cli` and
fetch the backend's complete locked dependency graph before offline deployment. The `cli`
feature is required: installing this release without it produces no executable. Keep
`CARGO_BUILD_JOBS=2` and OS/Rust/lockfile-specific Cargo caches; Apple keys also include
the deployment target. Non-release native editor tests run from `build/libs/vtextedit`
with `--no-tests=error`, not just the parent's CTest root.

`package/prepare-typst-notices.py` uses `package/typst-about.toml` to generate full Rust
license texts, including verbatim typst-assets/font notices and the backend LGPL text.
Matching `sbom/qtpdf-<version>.spdx.json` and
`sbom/qtwebengine-chromium-pdf-<version>.spdx.json` are required SDK attribution inputs.
Qt 6.9.3 provides both when qtpdf and qtwebengine are installed; the Chromium PDF inventory
is in qtwebengine. Missing/empty inputs fail packaging; never substitute another Qt version.
Generation is deployment-only, with no dependency installation at configure or runtime.
Notices ship in Windows `licenses/typst`, Linux `share/licenses/vnote/typst`, and macOS
`Contents/Resources/licenses/typst`.

Windows deployment scans both vnote and VTextEdit for Qt6Pdf and keeps
`vtextedit_typst.dll` in the flat runtime directory, never the pruned lib/bin directories.
Linux scans the installed `libvtextedit_typst.so.1` as a linuxdeploy library and checks
install/deploy exit codes. Universal macOS libraries build both Rust triples with the job's
deployment target, then lipo once. Stage `libvtextedit_typst.1.dylib` explicitly in
`Contents/Frameworks` through vnote_typst_runtime/deploy, fix load paths and sign before
deep bundle signing; ordinary install() does not assemble the VNote app. Resolve the
packaged VTextEdit filename from
VNote's linked dependency, not an unversioned dylib name; rpath cleanup and final DMG checks
must inspect that same library. Remove only rpaths outside the final DMG path allowlist.
Inspect both slices' minimum-OS metadata and reject build-tree dependencies. Metadata from a newer runner is not a Monterey launch test.
Run `scripts/e2e/smoke_typst_package.py --library <extracted-library>` against final package
contents without developer Cargo/Qt/Typst overrides. It calls C ABI 1 in a supervised
Python test worker and verifies real PDF/heading output and reply lifetime after session
destruction. Also launch the packaged consumer and inspect native dependency closure;
ctypes alone does not prove VTextEdit/VNote linkage. Reject the old Typst executable.
On macOS, architecture probes and ABI workers reuse the gate's already-running Python
(`sys.executable`), not a different `/usr/bin/python3` runtime. Keep the 15-second architecture
probe, mandatory native-architecture execution, and existing non-host availability policy.
`scripts/e2e/smoke_macos_consumer.py` requires the expected native architecture and launches
VNote with the unchanged 30-second budget and an isolated home. Test-enabled builds run ARM
native suites; all builds retain both available ABI slices and the WebEngine gate. Separate
`macos-15-intel` jobs download
the same signed DMGs and require native x86_64 ABI and consumer execution for both Qt kits.
Cold Rosetta translation of QtWebEngineCore can exceed the launch budget before main; do not
warm its cache, retry to green, or increase the deadline instead of using native Intel proof.
Staged DMG artifacts are inputs to verification, not publication approval. Both continuous and
versioned publication depend on all ARM build/gates and both Intel verification jobs, and
publish the unchanged signed artifacts. Native macOS 15 Intel proof is not a Monterey test.
A packaged-consumer timeout remains a failure. Capture bounded native stack/screen diagnostics
under `RUNNER_TEMP`, terminate only that unreaped probe's own process group, and retain the
matrix-specific diagnostic artifact. Include open-file, memory-map and bounded read-only
Rosetta-daemon samples to locate pre-main loader stalls; pre-create privileged sample output
as the runner user. A separate one-day `failed-package-macos-<Qt version>` artifact retains
only the exact signed DMG for reproduction, never adjacent certificates/keychains or the build
directory. It is diagnostic-only, not a release. Do not mask the timeout, force-unmount a busy
package, or move SSH sessions past signing-secret provisioning.

## macOS runtime composition

Run `package/prune-macos-package.cmake` with `VNOTE_APP_DIR` after each deployment
pass and after the last CI framework fixup, **before signing**. CPack's external
macOS packager rewrites the build bundle; Windows install-staging pruning does not
apply. Complete all test builds before final `pack`/deployment: those builds can relink
VNote/VTextEdit and restore build-tree RPATHs. Both framework-fixup passes precede the
final RPATH cleanup and pruning; no rebuild follows before signing. Use `VERIFY_ONLY=ON`
on the mounted signed DMG; it must never mutate it.

Remove `PlugIns/imageformats/libqpdf.dylib`, `PlugIns/position/libqtposition_nmea.dylib`
(and their `_debug` variants), `PlugIns/sqldrivers`, `QtSql.framework`, and
`QtSerialPort.framework`. VNote and its widget tests must not require QtSql; vxcore
uses native SQLite instead. Keep qtserialport in the CI SDK so macdeployqt can scan
the NMEA plugin before pruning. SQL-driver Homebrew libraries/workarounds are not needed.
Keep QtPdf for Typst, QtPositioning and other positioning plugins, TLS backends,
QML/Quick/WebEngine, DevTools, universal slices and attribution files.

Only `en-US.pak`, `zh-CN.pak`, `ja.pak`, and `ko.pak` ship in
`Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Resources/qtwebengine_locales`.
Missing/empty retained packs or another locale directory fail packaging. Do not
prune sibling WebEngine resource paks, ICU data, snapshots or the process helper.
Qt's resource fallback supplies English for unsupported locales without changing
VNote's application locale or date/number formatting.

The final-DMG gate rejects load commands to the removed frameworks in **all**
Mach-O files. `scripts/e2e/smoke_webengine_package.py` compiles a disposable native
probe against the SDK but runs against the signed package: separate processes for
each supported locale plus German/British-English fallback, actual DOM rendering,
and mapped framework/locale paths in both browser and helper. It must not resolve
runtime resources from the SDK or ship its probe in the app.
Build the probe with Qt's imported CMake targets, including their transitive header modules,
not a hand-maintained framework/include list. Declare the ARM yield intrinsic before Qt headers
on ARM hosts. Configure and compile share one 120-second budget; target only the host architecture.
Use `BUILD_WITH_INSTALL_RPATH` with the signed package Frameworks directory and no SDK link-path
RPATH additions. Keep the 45-second render bound and all locale checks. Allow 60 seconds for
graceful shutdown after rendering and mapping checks pass; QtWebEngine helper teardown must not
share the shorter 15-second post-kill pipe-drain limit. A shutdown timeout still fails the gate.

## Windows runtime (Qt 6)

Windows packages use Qt 6 only and publish the `win64` artifact family. Qt uses
its native Schannel TLS backend; Git sync uses WinHTTP. Do not bundle OpenSSL
DLLs or a separate UCRT redistributable: supported Windows versions provide UCRT.
Keep the MSVC runtime deployment and its fallback for newer toolsets, the flat
package layout, and the SDK/stray-executable composition gates. WiX stages the
application under `Unspecified/`; pruning resolves that component root before
checking for `vnote.exe`. ZIP staging remains flat.

Deploy both VNote and VTextEdit without `--no-pdf`: Typst requires `Qt6Pdf.dll`.
Prune only the unused `imageformats/qpdf.dll` plugin and NMEA position plugin after
deployment (including their debug variants); NMEA needs SerialPort, and Qt 6.10.3
still deploys it with `--exclude-plugins`. Keep `Qt6Positioning.dll`, the other
positioning plugins, and WebEngine's QML/Quick/OpenGL dependencies.
Do not prune the dynamically loaded D3D/DXC compilers or the MSVC runtime set.
The extracted-ZIP composition gate rejects PDF image plugins, NMEA, and application-local
UCRT/API-set DLLs recursively; the Typst gate requires a nonempty `Qt6Pdf.dll`.
Keep `qtpdf` in the CI SDK for the required runtime and its attribution.
---

## Update artifacts

Ordinary startup/manual checks remain metadata-only and read-only for the installation;
`UpdateService` ignores assets. The sole installation entry point is the explicit Windows
`Update Now` notification action launching the deployed external PowerShell updater, not an
in-process installer or a manual-dialog action. The script consumes signed **full packages
only**, with no unsigned or cross-source fallback. See
[../docs/update-signing.md](../docs/update-signing.md) and root
[AGENTS.md § Update Check](../AGENTS.md#update-check).

On the existing `[Release]` master-push condition, `win64` must publish a ZIP,
`.manifest.json` and `.manifest.json.minisig`. Retain optional delta publication,
although this updater never consumes deltas. Preserve the stable-channel and
artifact-list gates. Missing `MINISIGN_SECRET_KEY` must fail release signing, and
secret files stay outside the workspace. Missing Gitee mirrors fail closed at
runtime; never switch the user's source to GitHub.

`prepare_win_updater` must run `package/prepare-win-updater.cmake` with
`UPDATER_OUTPUT_DIR=<build>/src/win-updater` as a **deploy-only prerequisite**: no network on
configure or ordinary builds. Pin minisign 0.11 archive, executable and upstream ISC license
by SHA-256; accept only hash-matching cache entries and extract only in the build tree.
Signing uses this prepared verifier, not a second unpinned download.

Install `update-vnote.ps1`, `minisign.exe` and `LICENSE.minisign` under `updater/`, all
non-optional; retain the existing libsodium notices. Keep the root executable allowlist at
`vnote.exe` and `QtWebEngineProcess.exe`. Require `vtextedit_typst.dll` separately.
The extracted-package gate must
assert all three helper files and run the **packaged** `updater/minisign.exe -v`,
prepending that package root to the subprocess PATH for its runtime DLLs. Retain
all Qt 6 package-composition checks; verifier presence alone is not runtime proof.

## Linux test runtime

`ci-linux.yml` installs test-only runtime dependencies after packaging and runs
both the suite and failure diagnostics through `.github/scripts/run-linux-tests.sh`.
The wrapper creates a private D-Bus session and unlocked GNOME keyring, verifies
Secret Service with a write/read/delete probe, then tears down the daemon and
private XDG directories. CTest runs under Xvfb so tests selecting the native xcb
backend have a display. Install `xvfb` and `xauth` unconditionally: the final AppImage
consumer check also needs them on release builds. It launches packaged `AppRun --help`
with `QT_QPA_PLATFORM=xcb` under `xvfb-run -a`; the AppImage ships xcb, not offscreen.
Keep Qt lookup inside the extracted package and fail on plugin/display/load errors.
The child `test_typsteditor` suite must select `QT_QPA_PLATFORM=xcb` explicitly:
its native layout/raster scenarios must not inherit the parent's offscreen default.
Keep the credential tests on a real vault; headers from
`libsecret-1-dev` alone do not provide one.

TLS fixtures use Ubuntu's `/usr/bin/openssl` explicitly, not a PATH executable
shadowed by the separate packaging OpenSSL install. These environment changes
apply only to tests; release commits skip vault setup and test invocations, not the
Xvfb-backed packaged-consumer check.

## macOS test temporary paths

Both non-release test launches resolve `TMPDIR` to its physical path before
starting Qt or Python fixtures. The system `/var` alias resolves to `/private/var`
and otherwise trips the intentional sync path-safety checks. Keep those guards
and deliberate symlinks inside test fixtures unchanged; only the outer temporary
root is normalized, without affecting build or packaging paths.
