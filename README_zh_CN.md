# VNote
![CI-Windows](https://github.com/vnotex/vnote/actions/workflows/ci-win.yml/badge.svg?branch=master) ![CI-Linux](https://github.com/vnotex/vnote/actions/workflows/ci-linux.yml/badge.svg?branch=master) ![CI-MacOS](https://github.com/vnotex/vnote/actions/workflows/ci-macos.yml/badge.svg?branch=master)

[English](README.md)

[Gitee托管项目](https://gitee.com/vnotex/vnote)

一个舒适的笔记平台！

更多信息，请访问[VNote主页](https://vnotex.github.io/vnote)。

![VNote](pics/vnote.png)

## 简介
**VNote**是一个专注于Markdown的基于Qt的开源免费的笔记应用。VNote希望能提供一个拥有完美编辑体验的舒适的笔记平台。

VNote不是一个简单的Markdown编辑器。通过提供强大的笔记管理，VNote使得使用Markdown记笔记更轻松简单。将来，VNote会支持更多的文档格式。

得益于Qt，VNote当前可以高效地运行在**Linux**，**Windows**，以及**macOS**平台上。

![主界面](pics/main.png)

![主界面2](pics/main2.png)

## 下载
基于`master`分支的[持续构建版本发布](https://github.com/vnotex/vnote/releases/tag/continuous-build)。

最新的[稳定版本发布](https://github.com/vnotex/vnote/releases/latest)。其他下载选项：

* [天翼云盘](https://cloud.189.cn/t/Av67NvmEJVBv)
* [百度云盘](https://pan.baidu.com/s/1lX69oMBw8XuJshQDN3HiHw?pwd=f8fk)

## 支持
* [GitHub Issues](https://github.com/vnotex/vnote/issues)；
* 邮件：`tamlokveer at gmail.com`；
* [Telegram](https://t.me/vnotex)；
* 微信公众号：`vnotex`；

感谢这些[捐赠用户](https://github.com/vnotex/vnote/wiki/Donate-List)！

## 开发

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

更多开发指南，请参考 [AGENTS.md](AGENTS.md)。

## 许可
VNote遵循[GNU LGPLv3](https://opensource.org/licenses/LGPL-3.0)许可。VNote项目的代码可以自由给VNoteX项目使用。
