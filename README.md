# VNote
![CI-Windows](https://github.com/vnotex/vnote/actions/workflows/ci-win.yml/badge.svg?branch=master) ![CI-Linux](https://github.com/vnotex/vnote/actions/workflows/ci-linux.yml/badge.svg?branch=master) ![CI-MacOS](https://github.com/vnotex/vnote/actions/workflows/ci-macos.yml/badge.svg?branch=master)

[简体中文](README_zh_CN.md)

[Project on Gitee](https://gitee.com/vnotex/vnote)

A pleasant note-taking platform.

For more information, please visit [**VNote's Home Page**](https://vnotex.github.io/vnote).

![VNote](pics/vnote.png)

## Description
**VNote** is a Qt-based, free and open source note-taking application, focusing on Markdown now. VNote is designed to provide a pleasant note-taking platform with excellent editing experience.

VNote is **NOT** just a simple editor for Markdown. By providing notes management, VNote makes taking notes in Markdown simpler. In the future, VNote will support more formats besides Markdown.

Utilizing Qt, VNote could run on **Linux**, **Windows**, and **macOS**.

![Main](pics/main.png)

![Main2](pics/main2.png)

## Downloads
Continuous builds on `master` branch could be found at the [Continuous Build](https://github.com/vnotex/vnote/releases/tag/continuous-build) release.

Latest stable builds could be found at the [latest release](https://github.com/vnotex/vnote/releases/latest). Alternative download services are available:

* [Tianyi Netdisk](https://cloud.189.cn/t/Av67NvmEJVBv)
* [Baidu Netdisk](https://pan.baidu.com/s/1lX69oMBw8XuJshQDN3HiHw?pwd=f8fk)

## Supports
* [GitHub Issues](https://github.com/vnotex/vnote/issues);
* Email: `tamlokveer at gmail.com`;
* [Telegram](https://t.me/vnotex);
* WeChat Public Account: vnotex;

Thank [users who donated to VNote](https://github.com/vnotex/vnote/wiki/Donate-List)!

## Development

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

For more development guidelines, see [AGENTS.md](AGENTS.md).

## License
VNote is licensed under [GNU LGPLv3](https://opensource.org/licenses/LGPL-3.0). Code base of VNote could be used freely by VNoteX.
