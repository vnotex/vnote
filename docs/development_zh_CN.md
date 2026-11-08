# 开发

[English](development.md)

除非另有说明，以下命令均在仓库根目录执行。

## 前置依赖

* Git、CMake 3.21+、Ninja、Python 3 和支持 C++20 的编译器
  （语法索引生成器使用 C++20；VNote 使用 C++17）
* Qt 6.9 及以上版本的开发包，包含 Core5Compat、SVG、WebChannel、WebEngineWidgets、PDF、
  LinguistTools，以及版本匹配的 Qt Gui 私有头文件
* Windows：Visual Studio 的“使用 C++ 的桌面开发”工作负载和 Windows SDK，
  搭配 64 位 MSVC Qt Kit（开发环境使用 Qt 6.10.3 MSVC2022 64bit）
* [Rustup](https://rustup.rs/) 和
  [rust-toolchain.toml](../libs/vtextedit/tools/typst-backend/rust-toolchain.toml)
  锁定的 Rust 版本（当前为 **1.99.0**）；上述 Windows Kit 使用
  `x86_64-pc-windows-msvc` 主机工具链
* 可选：用于格式化钩子的 clang-format

## 初始化仓库

克隆仓库后，运行初始化脚本来设置开发环境：

**Linux/macOS:**
```bash
bash scripts/init.sh
```

**Windows:**
```cmd
scripts\init.cmd
```

此脚本将会：
* 初始化并更新所有 git 子模块
* 安装用于自动代码格式化的 pre-commit 钩子（需要 clang-format）
* 设置 vtextedit 子模块的 pre-commit 钩子

## 安装 Typst 构建工具链

VNote 需要原生 Typst 后端。先安装 Rustup，再安装锁定的 Rust 工具链。
以下命令使用 Rustup 的默认安装位置。

**Windows（PowerShell，无需修改 PATH）：**
```powershell
& "$env:USERPROFILE/.cargo/bin/rustup.exe" toolchain install 1.99.0 --profile minimal
Push-Location libs/vtextedit/tools/typst-backend
& "$env:USERPROFILE/.cargo/bin/cargo.exe" --version
& "$env:USERPROFILE/.cargo/bin/rustc.exe" --version
Pop-Location
```

**Linux/macOS：**
```bash
rustup toolchain install 1.99.0 --profile minimal
(cd libs/vtextedit/tools/typst-backend && cargo --version && rustc --version)
```

CMake 会将锁定的 Rust 依赖构建为 `vtextedit_typst` 共享库，由 VTextEdit 链接并随
VNote 一起部署。无需单独安装 `typst` CLI 或 Tinymist。首次构建需要联网下载依赖。

## Qt Creator

1. 打开仓库根目录的 `CMakeLists.txt`，选择与 Qt、编译器架构匹配的 Kit。
   Windows 已安装对应版本时，选择 **Desktop Qt 6.10.3 MSVC2022 64bit**。
2. 在 **Projects > Build Settings** 中使用 Ninja，并为 Debug 和 Release 分别设置全新的
   构建目录，例如 `build-typst-debug` 和 `build-typst-release`。不要复用其他工具链的旧缓存。
3. 在两个配置的 **Initial Configuration** 中添加以下 Windows CMake 参数：
   ```text
   -DVTEXTEDIT_CARGO_EXECUTABLE:FILEPATH=%{Env:USERPROFILE}/.cargo/bin/cargo.exe
   ```
   自定义 Cargo 安装应使用实际可执行文件路径。Linux/macOS 需确保 Qt Creator 构建环境的
   PATH 包含 Cargo，或将同一 CMake 变量设为其可执行文件路径。
   Initial Configuration 用于新的构建目录；已有配置的目录还需要修改当前 CMake 配置。
4. 如需 VNote 测试，在 Debug 配置中添加 `-DVNOTE_BUILD_TESTS:BOOL=ON`。
   配置完成后构建 **vnote** 和 **lrelease** 目标，运行/调试目标选择 **vnote**。
   Typst 共享库是构建依赖，不是单独的运行目标。
5. 在 VNote 中打开 `.typst` 笔记，确认预览正常渲染。
   也可在仓库根目录直接验证 Windows 共享库：
   ```powershell
   python scripts/e2e/smoke_typst_package.py --library build-typst-release/src/vtextedit_typst.dll
   ```

Typst 在 VNote 进程内的固定数量后台线程上运行。超时会丢弃结果，但不能强制终止原生调用；
退出时会等待活动调用返回。启动时必须能加载共享库，原生编译器崩溃也可能影响 VNote。

`--no-single-instance` 可启动第二个 VNote 进程，但只跳过锁和 IPC，**不会隔离用户数据**。
使用临时配置和笔记测试时，请遵循[隔离 GUI 冒烟测试步骤](../tests/AGENTS.md#agent-gui-smoke-testing-windows)。

更多开发指南，请参考 [AGENTS.md](../AGENTS.md)。
