# CI, Packaging & Release

This directory holds the GitHub Actions workflows. **This document also governs
[`src/Packaging.cmake`](../src/Packaging.cmake)**, which lives outside this directory and
therefore will not auto-load this file — open it explicitly when touching packaging, the
Windows 7 / Qt 5 variant, or bundled OpenSSL.

See also: [../AGENTS.md](../AGENTS.md) (repo-wide rules, submodule push discipline) and
[../src/AGENTS.md](../src/AGENTS.md) (architecture, source-wide Qt patterns).

---

## Windows 7 variant (Qt 5.15) and OpenSSL

The `win64-windows7` package is built against Qt 5.15.2, which has **no Schannel TLS backend**
on Windows — unlike Qt 6, which falls back to Schannel and therefore ships no OpenSSL at all.
Git sync is unaffected on both variants because libgit2 uses WinHTTP. So on Qt 5 a missing or
unloadable OpenSSL breaks exactly two things: the update check
(`src/core/services/updateservice.cpp`) and image hosting (`src/imagehost/`).

- CI **builds OpenSSL 1.1.1w from source** in the Qt5 job (`.github/workflows/ci-win.yml`),
  cached under `${{runner.workspace}}/openssl-1.1.1w-win64` with a version-pinned key. This
  replaced prebuilt 1.1.1j DLLs that imported `MSVCR100.dll` (the VC++ 2010 runtime, absent
  from the package and from a clean Windows box), which is why 4.4.2's win7 build had no TLS.
  Qt's own `tools_openssl_x64` is **delisted** from `download.qt.io`, and the pinned Qt 5.15.2
  dlopens the literal names `libssl-1_1-x64` / `libcrypto-1_1-x64`, so OpenSSL 3 is not a
  drop-in substitute for it.
- **OpenSSL 1.1.1 is EOL; 1.1.1w (Sep 2023) is the final release.** The variant will accrue
  unpatched CVEs. That is inherent to shipping Qt 5.15.2 and is only fixable by retiring the
  Qt 5 variant.
- Three gates, none of which replaces the others:
  1. `dumpbin /dependents` on both DLLs, parsed into trimmed basenames, rejecting any
     `MSVCR*`/`MSVCP*` and anything outside an explicit system + modern-runtime allowlist.
     This checks COMPOSITION, not content: it cannot detect a substituted DLL with the same
     import table. Content is pinned only by the tarball SHA-256, which is verified in the
     build step and therefore skipped on a cache hit.
  2. A post-extraction check that every non-system dependency actually ships at the package root.
  3. `tools/tlsprobe` — run **from inside the packaged directory** so it reproduces
     `vnote.exe`'s real DLL load context — asserting `QSslSocket::supportsSsl()`. It has no
     `install()` rule and must never enter the package.
- `src/Packaging.cmake` turns the previously `OPTIONAL` (and therefore silently no-op) OpenSSL
  install into a `FATAL_ERROR` when the exact pair is missing on Qt 5 / Windows / x64. This
  runs at **configure** time, so a contributor building Qt 5 locally without OpenSSL opts out
  with `-DVNOTE_REQUIRE_BUNDLED_OPENSSL=OFF` (default **ON**, so CI is safe by default and a
  workflow edit cannot silently drop the gate).
- The same guard asserts that `InstallRequiredSystemLibraries` actually resolved the UCRT
  redist. Windows 7 has no in-box UCRT, and the module's failure warnings are deliberately
  suppressed, so without this the omission would only surface at the last CI gate.
- **`LICENSE.OpenSSL` must remain in the package.** OpenSSL 1.1.1's dual OpenSSL/SSLeay license
  requires reproducing the notice with binary redistribution; 4.4.2 shipped the DLLs with no
  notice at all. It is installed non-`OPTIONAL` from `-DOPENSSL_LICENSE_FILE=`, keyed off
  **whether any OpenSSL DLL is being installed** rather than off the `-x64` names — the install
  globs are gated by neither Qt major nor word size, so keying it to the names would let a
  32-bit Qt 5 build (or a Qt 6 build pointed at an OpenSSL dir) ship binaries with no notice.

---

## Update artifacts

Release CI still publishes manifests, minisign signatures and delta ZIPs (see
[../docs/update-signing.md](../docs/update-signing.md)). **The VNote client does not consume
them**: it never downloads, extracts, executes or installs a release artifact, and never
modifies its own install directory. These artifacts exist as the interface for a future
*external* updater. Client-side rules: root [AGENTS.md § Update Check](../AGENTS.md#update-check);
implementation detail: [../src/core/services/AGENTS.md § Update Check](../src/core/services/AGENTS.md#update-check).
