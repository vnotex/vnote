# VNote
![CI-Windows](https://github.com/vnotex/vnote/actions/workflows/ci-win.yml/badge.svg?branch=master) ![CI-Linux](https://github.com/vnotex/vnote/actions/workflows/ci-linux.yml/badge.svg?branch=master) ![CI-MacOS](https://github.com/vnotex/vnote/actions/workflows/ci-macos.yml/badge.svg?branch=master)

[简体中文](README_zh_CN.md)

[Project on Gitee](https://gitee.com/vnotex/vnote)

A pleasant note-taking platform.

For more information, please visit [**VNote's Home Page**](https://docs.vnote.fun).

![VNote](pics/vnote.png)

## Description
**VNote** is a Qt-based, free and open source note-taking application. It combines note management with an excellent editing experience across multiple document formats.

VNote runs on **Linux**, **Windows**, and **macOS**, and requires **Qt 6**. Qt 5.15 and Windows 7 are no longer supported.

### Supported formats
**VNote is more than a Markdown editor — these formats are supported today:**

* **Markdown** (`.md`, `.mkd`, `.rmd`, `.markdown`): write, edit, and preview Markdown notes
* **Plain text and source code** (`.txt`, `.log`, `.py`, `.cpp`, `.js`, `.json`, `.yaml`, and more): edit text notes, logs, code, and configuration files
* **Mind maps** (`.mmm`, `.emind`): create and edit mind maps with the native mind-map editor
* **PDF** (`.pdf`): read PDF documents with the built-in viewer

The built-in PDF viewer requires a **Qt 6.9+** build. Builds using earlier Qt 6 versions open PDFs in the system default application.

![Main](pics/main.webp)

## Downloads
Continuous builds on `master` branch could be found at the [Continuous Build](https://github.com/vnotex/vnote/releases/tag/continuous-build) release.

Latest stable builds could be found at the [latest release](https://github.com/vnotex/vnote/releases/latest) or [Gitee release](https://gitee.com/vnotex/vnote/releases)

## Supports
* [GitHub Issues](https://github.com/vnotex/vnote/issues);
* Email: `tamlokx at gmail.com`;

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
VNote is licensed under [GNU LGPLv3](https://opensource.org/licenses/LGPL-3.0).

Bundled third-party material and the notices its licenses require — the Lucide
(ISC/MIT) and IconPark (Apache-2.0) icon sets — are recorded in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
