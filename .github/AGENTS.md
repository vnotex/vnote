# CI, Packaging & Release

This directory holds the GitHub Actions workflows. **This document also governs
[`src/Packaging.cmake`](../src/Packaging.cmake)**, which lives outside this directory and
therefore will not auto-load this file — open it explicitly when touching packaging, the
Windows Qt 6 packages, or updater artifacts.

See also: [../AGENTS.md](../AGENTS.md) (repo-wide rules, submodule push discipline) and
[../src/AGENTS.md](../src/AGENTS.md) (architecture, source-wide Qt patterns).

---

## Release packaging dependencies

`pack` must build `cmark_exe` before CPack runs: upstream cmark installs its CLI
unconditionally, even though Windows package pruning removes it afterward. Release
CI deliberately skips `all` to avoid building tests, so do not rely on a prior
all-target build or weaken the install/pruning checks to hide a missing executable.

### Native Typst runtime and attribution

VNote requires the pinned Rust 1.99.0/Typst 0.15.1 helper and Qt PDF. Embedding forces
`VTEXTEDIT_ENABLE_TYPST=ON` and `VTEXTEDIT_BUILD_DEMOS=OFF`; do not ship upstream demos.
`vnote_typst_runtime` always refreshes the adjacent helper (and VTextEdit.dll on Windows),
even without relinking VNote. Both `pack` and direct `deploy` depend on this target and
`prepare_typst_notices`, retaining the existing cmark/lrelease prerequisites.

Provision `cargo +1.99.0 install cargo-about --locked --version 0.9.2 --features cli` and
fetch the helper's complete locked dependency graph before offline deployment. The `cli`
feature is required: installing this release without it produces no executable. Keep
`CARGO_BUILD_JOBS=2` and OS/Rust/lockfile-specific Cargo caches; Apple keys also include
the deployment target. Non-release native editor tests run from `build/libs/vtextedit`
with `--no-tests=error`, not just the parent's CTest root.

`package/prepare-typst-notices.py` uses `package/typst-about.toml` to generate full Rust
license texts, including verbatim typst-assets/font notices and the helper LGPL text.
Matching `sbom/qtpdf-<version>.spdx.json` and
`sbom/qtwebengine-chromium-pdf-<version>.spdx.json` are required SDK attribution inputs.
Qt 6.9.3 provides both when qtpdf and qtwebengine are installed; the Chromium PDF inventory
is in qtwebengine. Missing/empty inputs fail packaging; never substitute another Qt version.
Generation is deployment-only, with no dependency installation at configure or runtime.
Notices ship in Windows `licenses/typst`, Linux `share/licenses/vnote/typst`, and macOS
`Contents/Resources/licenses/typst`.

Windows deployment scans both vnote and VTextEdit for Qt6Pdf. Linux scans the installed
helper as a second linuxdeploy executable and checks install/deploy exit codes. Universal
macOS helpers build both Rust triples with the job's deployment target, then lipo once;
sign the helper before deep bundle signing. Resolve the packaged VTextEdit filename from
VNote's linked dependency, not an unversioned dylib name; rpath cleanup and final DMG checks
must inspect that same library. Remove only rpaths outside the final DMG path allowlist.
Inspect both slices' minimum-OS metadata and reject build-tree dependencies. Metadata from a newer runner is not a Monterey launch test.
Run `scripts/e2e/smoke_typst_package.py --helper <extracted-helper>` against final package
contents without developer Cargo/Qt/Typst overrides; it must compile real output, not just
find a source file or executable.

## Windows runtime (Qt 6)

Windows packages use Qt 6 only and publish the `win64` artifact family. Qt uses
its native Schannel TLS backend; Git sync uses WinHTTP. Do not bundle OpenSSL
DLLs or a separate UCRT redistributable: supported Windows versions provide UCRT.
Keep the MSVC runtime deployment and its fallback for newer toolsets, the flat
package layout, and the SDK/stray-executable composition gates.

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
`vnote.exe`, `QtWebEngineProcess.exe`, and `vtextedit-typst.exe`. The extracted-package gate must
assert all three helper files and run the **packaged** `updater/minisign.exe -v`,
prepending that package root to the subprocess PATH for its runtime DLLs. Retain
all Qt 6 package-composition checks; verifier presence alone is not runtime proof.

## Linux test runtime

`ci-linux.yml` installs test-only runtime dependencies after packaging and runs
both the suite and failure diagnostics through `scripts/run-linux-tests.sh`.
The wrapper creates a private D-Bus session and unlocked GNOME keyring, verifies
Secret Service with a write/read/delete probe, then tears down the daemon and
private XDG directories. Keep the credential tests on a real vault; headers from
`libsecret-1-dev` alone do not provide one.

TLS fixtures use Ubuntu's `/usr/bin/openssl` explicitly, not a PATH executable
shadowed by the separate packaging OpenSSL install. These environment changes
apply only to tests; release commits skip the runtime setup and test invocations.

## macOS test temporary paths

Both non-release test launches resolve `TMPDIR` to its physical path before
starting Qt or Python fixtures. The system `/var` alias resolves to `/private/var`
and otherwise trips the intentional sync path-safety checks. Keep those guards
and deliberate symlinks inside test fixtures unchanged; only the outer temporary
root is normalized, without affecting build or packaging paths.
