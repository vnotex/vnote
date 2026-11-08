# Development

[简体中文](development_zh_CN.md)

Run the commands below from the repository root unless noted otherwise.

## Prerequisites

* Git, CMake 3.21+, Ninja, Python 3, and a C++20-capable compiler
  (the syntax indexer uses C++20; VNote uses C++17)
* Qt 6.9+ development packages, including Core5Compat, SVG, WebChannel, WebEngineWidgets,
  PDF, LinguistTools, and matching Qt Gui private headers
* On Windows: Visual Studio's **Desktop development with C++** workload and Windows SDK,
  with a 64-bit MSVC Qt kit (the development setup uses Qt 6.10.3 MSVC2022 64bit)
* [Rustup](https://rustup.rs/) and the Rust version pinned in
  [rust-toolchain.toml](../libs/vtextedit/tools/typst-backend/rust-toolchain.toml)
  (currently **1.99.0**); use the `x86_64-pc-windows-msvc` host for the Windows kit above
* Optional: clang-format for the formatting hooks

## Initialize the checkout

After cloning the repository, run the initialization script to set up your development environment:

**Linux/macOS:**
```bash
bash scripts/init.sh
```

**Windows:**
```cmd
scripts\init.cmd
```

This script will:
* Initialize and update all git submodules
* Install pre-commit hooks for automatic code formatting with clang-format
* Set up the vtextedit submodule pre-commit hook

## Install the Typst build toolchain

VNote requires the native Typst backend. Install Rustup first, then install the pinned
Rust toolchain. These commands assume Rustup's default installation location.

**Windows (PowerShell, no PATH change required):**
```powershell
& "$env:USERPROFILE/.cargo/bin/rustup.exe" toolchain install 1.99.0 --profile minimal
Push-Location libs/vtextedit/tools/typst-backend
& "$env:USERPROFILE/.cargo/bin/cargo.exe" --version
& "$env:USERPROFILE/.cargo/bin/rustc.exe" --version
Pop-Location
```

**Linux/macOS:**
```bash
rustup toolchain install 1.99.0 --profile minimal
(cd libs/vtextedit/tools/typst-backend && cargo --version && rustc --version)
```

CMake builds the locked Rust dependencies into the `vtextedit_typst` shared library,
linked by VTextEdit and bundled with VNote. No separate `typst` CLI or Tinymist
installation is required. The first build needs network access to fetch dependencies.

## Qt Creator

1. Open the root `CMakeLists.txt` and select a kit matching your Qt and compiler
   architecture. On Windows, select **Desktop Qt 6.10.3 MSVC2022 64bit** if installed.
2. Under **Projects > Build Settings**, use Ninja and separate fresh build directories,
   such as `build-typst-debug` and `build-typst-release`. Do not reuse a stale build cache
   from another toolchain.
3. In **Initial Configuration**, add this Windows CMake parameter for both profiles:
   ```text
   -DVTEXTEDIT_CARGO_EXECUTABLE:FILEPATH=%{Env:USERPROFILE}/.cargo/bin/cargo.exe
   ```
   For a custom Cargo installation, use its actual executable path. On Linux/macOS,
   ensure Cargo is on Qt Creator's build environment PATH, or set the same CMake variable
   to its executable path. Initial Configuration applies to a new build directory;
   an already configured directory needs its current CMake configuration updated too.
4. Add `-DVNOTE_BUILD_TESTS:BOOL=ON` to the Debug profile if you want the VNote tests.
   Configure, then build the **vnote** and **lrelease** targets; select **vnote** to run/debug.
   The Typst shared library is a build dependency, not a separate run target.
5. Open a `.typst` note in VNote and check that its preview renders. To smoke-test
   the Windows library directly from the repository root:
   ```powershell
   python scripts/e2e/smoke_typst_package.py --library build-typst-release/src/vtextedit_typst.dll
   ```

Typst runs on bounded background threads inside VNote. Timeouts discard results but
cannot forcibly stop native calls; shutdown waits for active calls to return. The
shared library is required at startup, and native compiler faults can affect VNote.

For a second VNote process, `--no-single-instance` skips locking/IPC but **does not isolate
user data**. Follow the [isolated GUI smoke recipe](../tests/AGENTS.md#agent-gui-smoke-testing-windows)
when testing with a disposable profile and notes.

For more development guidelines, see [AGENTS.md](../AGENTS.md).
