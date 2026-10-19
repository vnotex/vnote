# VNote
![CI-Windows](https://github.com/vnotex/vnote/actions/workflows/ci-win.yml/badge.svg?branch=master) ![CI-Linux](https://github.com/vnotex/vnote/actions/workflows/ci-linux.yml/badge.svg?branch=master) ![CI-MacOS](https://github.com/vnotex/vnote/actions/workflows/ci-macos.yml/badge.svg?branch=master)

[English](README.md)

[Gitee托管项目](https://gitee.com/vnotex/vnote)

一个舒适的笔记平台！

更多信息，请访问[VNote主页](https://docs.vnote.fun/zh)。

![VNote](pics/vnote.png)

## 简介
**VNote**是一个基于Qt的开源免费笔记应用，将笔记管理与出色的编辑体验相结合，支持多种文档格式。

VNote支持**Linux**、**Windows**和**macOS**平台，需要**Qt 6.9及以上版本**。不再支持Qt 5.15和Windows 7。

### 支持的格式
**VNote不只是Markdown编辑器——以下格式现已支持：**

* **Markdown**（`.md`、`.mkd`、`.rmd`、`.markdown`）：编写、编辑和预览Markdown笔记
* **纯文本和源代码**（`.txt`、`.log`、`.py`、`.cpp`、`.js`、`.json`、`.yaml`等）：编辑文本笔记、日志、代码和配置文件
* **思维导图**（`.mmm`、`.emind`）：使用原生思维导图编辑器创建和编辑导图
* **PDF**（`.pdf`）：使用内置阅读器阅读PDF文档

![主界面](pics/main.webp)

## 下载
基于`master`分支的[持续构建版本发布](https://github.com/vnotex/vnote/releases/tag/continuous-build)。

最新的[稳定版本发布](https://github.com/vnotex/vnote/releases/latest)，或者[Gitee发布](https://gitee.com/vnotex/vnote/releases)。

## 支持
* [GitHub Issues](https://github.com/vnotex/vnote/issues)；
* 邮件：`tamlokx at gmail.com`；
* 微信公众号：`vnotex`；

感谢这些[捐赠用户](https://github.com/vnotex/vnote/wiki/Donate-List)！

## 开发

### 前置依赖

* Git、CMake 3.21+、Ninja、Python 3 和支持 C++20 的编译器
  （语法索引生成器使用 C++20；VNote 使用 C++17）
* Qt 6.9 及以上版本的开发包，包含 Core5Compat、SVG、WebChannel、WebEngineWidgets、PDF、
  LinguistTools，以及版本匹配的 Qt Gui 私有头文件
* Windows：Visual Studio 的“使用 C++ 的桌面开发”工作负载和 Windows SDK，
  搭配 64 位 MSVC Qt Kit（开发环境使用 Qt 6.10.3 MSVC2022 64bit）
* [Rustup](https://rustup.rs/) 和
  [rust-toolchain.toml](libs/vtextedit/tools/typst-backend/rust-toolchain.toml)
  锁定的 Rust 版本（当前为 **1.99.0**）；上述 Windows Kit 使用
  `x86_64-pc-windows-msvc` 主机工具链
* 可选：用于格式化钩子的 clang-format

### 初始化仓库

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

### 安装 Typst 构建工具链

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

CMake 会构建锁定的 Rust 依赖，并将 `vtextedit-typst` 复制到 VNote 可执行文件旁。
无需单独安装 `typst` CLI 或 Tinymist。首次构建需要联网下载依赖。

### Qt Creator

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
   Typst 辅助程序是构建依赖，不是单独的运行目标。
5. 在 VNote 中打开 `.typst` 笔记，确认预览正常渲染。
   也可在仓库根目录直接验证 Windows 辅助程序：
   ```powershell
   python scripts/e2e/smoke_typst_package.py --helper build-typst-release/src/vtextedit-typst.exe
   ```

`--no-single-instance` 可启动第二个 VNote 进程，但只跳过锁和 IPC，**不会隔离用户数据**。
使用临时配置和笔记测试时，请遵循[隔离 GUI 冒烟测试步骤](tests/AGENTS.md#agent-gui-smoke-testing-windows)。

更多开发指南，请参考 [AGENTS.md](AGENTS.md)。

## 许可
VNote遵循[GNU LGPLv3](https://opensource.org/licenses/LGPL-3.0)许可。

本项目附带的第三方素材及其许可要求的声明——Lucide（ISC/MIT）与 IconPark（Apache-2.0）
图标集——记录于 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。
