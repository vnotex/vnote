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

## Windows runtime (Qt 6)

Windows packages use Qt 6 only and publish the `win64` artifact family. Qt uses
its native Schannel TLS backend; Git sync uses WinHTTP. Do not bundle OpenSSL
DLLs or a separate UCRT redistributable: supported Windows versions provide UCRT.
Keep the MSVC runtime deployment and its fallback for newer toolsets, the flat
package layout, and the SDK/stray-executable composition gates.
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
`vnote.exe` and `QtWebEngineProcess.exe`. The extracted-package gate must
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
