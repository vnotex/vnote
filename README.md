# VNote
![CI-Windows](https://github.com/vnotex/vnote/actions/workflows/ci-win.yml/badge.svg?branch=master) ![CI-Linux](https://github.com/vnotex/vnote/actions/workflows/ci-linux.yml/badge.svg?branch=master) ![CI-MacOS](https://github.com/vnotex/vnote/actions/workflows/ci-macos.yml/badge.svg?branch=master)

[简体中文](README_zh_CN.md)

[Project on Gitee](https://gitee.com/vnotex/vnote)

A pleasant note-taking platform.

For more information, please visit [**VNote's Home Page**](https://docs.vnote.fun).

![VNote](pics/vnote.png)

## Description
**VNote** is a Qt-based, free and open source note-taking application. It combines note management with an excellent editing experience across multiple document formats.

VNote runs on **Linux**, **Windows**, and **macOS**, and requires **Qt 6.9+**. Qt 5.15 and Windows 7 are no longer supported.

### Supported formats
**VNote is more than a Markdown editor — these formats are supported today:**

* **Markdown** (`.md`, `.mkd`, `.rmd`, `.markdown`): write, edit, and preview Markdown notes
* **Plain text and source code** (`.txt`, `.log`, `.py`, `.cpp`, `.js`, `.json`, `.yaml`, and more): edit text notes, logs, code, and configuration files
* **Mind maps** (`.mmm`, `.emind`): create and edit mind maps with the native mind-map editor
* **PDF** (`.pdf`): read PDF documents with the built-in viewer

![Main](pics/main.webp)

## Downloads
Continuous builds on `master` branch could be found at the [Continuous Build](https://github.com/vnotex/vnote/releases/tag/continuous-build) release.

Latest stable builds could be found at the [latest release](https://github.com/vnotex/vnote/releases/latest) or [Gitee release](https://gitee.com/vnotex/vnote/releases)

## Supports
* [GitHub Issues](https://github.com/vnotex/vnote/issues);
* Email: `tamlokx at gmail.com`;

Thank [users who donated to VNote](https://github.com/vnotex/vnote/wiki/Donate-List)!

## Development

### Prerequisites

* Git, CMake 3.21+, Ninja, Perl, Python 3, and a C++20-capable compiler
  (the syntax indexer uses C++20; VNote uses C++17)
* Qt 6.9+ development packages, including Core5Compat, SVG, WebChannel, WebEngineWidgets,
  PDF, LinguistTools, and matching Qt Gui private headers
* On Windows: Visual Studio's **Desktop development with C++** workload and Windows SDK,
  with a 64-bit MSVC Qt kit (the development setup uses Qt 6.10.3 MSVC2022 64bit)
* [Rustup](https://rustup.rs/) and the Rust version pinned in
  [rust-toolchain.toml](libs/vtextedit/tools/typst-backend/rust-toolchain.toml)
  (currently **1.99.0**); use the `x86_64-pc-windows-msvc` host for the Windows kit above
* Optional: clang-format for the formatting hooks

### Initialize the checkout

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

### Install the Typst build toolchain

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

CMake builds the locked Rust dependencies and copies `vtextedit-typst` beside VNote.
A separate `typst` CLI or Tinymist installation is not required. The first build needs
network access to fetch dependencies.

### Qt Creator

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
   The Typst helper is a build dependency, not a separate run target.
5. Open a `.typst` note in VNote and check that its preview renders. To smoke-test
   the Windows helper directly from the repository root:
   ```powershell
   python scripts/e2e/smoke_typst_package.py --helper build-typst-release/src/vtextedit-typst.exe
   ```

For a second VNote process, `--no-single-instance` skips locking/IPC but **does not isolate
user data**. Follow the [isolated GUI smoke recipe](tests/AGENTS.md#agent-gui-smoke-testing-windows)
when testing with a disposable profile and notes.

For more development guidelines, see [AGENTS.md](AGENTS.md).

## License
VNote is licensed under [GNU LGPLv3](https://opensource.org/licenses/LGPL-3.0).

Bundled third-party material and the notices its licenses require — the Lucide
(ISC/MIT) and IconPark (Apache-2.0) icon sets — are recorded in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
