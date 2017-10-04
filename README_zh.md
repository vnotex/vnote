# VNote
[English](./README.md)

**VNote** 是一个受Vim启发开发的专门为 **Markdown** 而优化、设计的笔记软件。VNote是一个更了解程序员和Markdown的笔记软件。

![VNote](screenshots/vnote.png)

# 下载
国内的用户可以尝试在[百度云盘](http://pan.baidu.com/s/1jI5HROq)下载VNote的最新发行版本。

## Windows
![Windows Build Status](https://ci.appveyor.com/api/projects/status/github/tamlok/vnote?svg=true)

- [Github releases](https://github.com/tamlok/vnote/releases)
- master分支的最新构建：[ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

VNote不支持**XP**，因为QtWebEngineProcess无法在XP上运行。

## Linux
[![Build Status](https://travis-ci.org/tamlok/vnote.svg?branch=master)](https://travis-ci.org/tamlok/vnote)

VNote当前为主要Linux发行版提供了一个AppImage格式的独立可执行文件。希望了解Linux系统下打包发布的开发人员能提供这方面进一步的帮助！

- [Github releases](https://github.com/tamlok/vnote/releases)
- master分支的最新构建：[ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

## MacOS
[![Build Status](https://travis-ci.org/tamlok/vnote.svg?branch=master)](https://travis-ci.org/tamlok/vnote)

- [Github releases](https://github.com/tamlok/vnote/releases)
- master分支的最新构建：[ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

# 简介

**VNote**是一个基于Qt框架的、免费的开源笔记软件。VNote专注于Markdown的编辑与阅读，以提供舒适的编辑体验为设计目标。

VNote不是一个简单的Markdown编辑器。通过提供笔记管理功能，VNote使得编写Markdown笔记更简单和舒适！

基于Qt框架，VNote能够在主流操作系统上运行，包括 **Linux**, **Windows** 以及 **macOS**（由于macOS上很不一样的交互逻辑，VNote在macOS上并没有被充分测试，我们也希望得到更多的反馈以帮助改进VNote）。

![VNote](screenshots/vnote_001.png)

# 支持
- [Github issues](https://github.com/tamlok/vnote/issues)；
- 邮箱: tamlokveer at gmail.com；
- QQ群: 487756074 (VNote使用和开发)；  
![QQ群](screenshots/qq_group.png)

# 亮点
- 支持直接从剪切板插入图片；
- 支持编辑和阅读模式下代码块的语法高亮；
- 支持编辑和阅读模式下的大纲；
- 支持自定义编辑和阅读模式的样式；
- 支持Vim模式以及一系列强大的快捷键；
- 支持无限层级的文件夹；
- 支持多个标签页和窗口分割；
- 支持[Mermaid](http://knsv.github.io/mermaid/), [Flowchart.js](http://flowchart.js.org/) 和 [MathJax](https://www.mathjax.org/)；
- 支持高分辨率；
- 支持笔记附件。

![VNote Edit](screenshots/vnote_edit.gif)

# 开发VNote的动机
## Markdown编辑器与笔记管理
VNote设计为带有笔记管理功能的Markdown编辑器，或者有良好Markdown支持的笔记软件。如果您喜欢Markdown并经常在学习、工作和生活中使用Markdown记录笔记，那么VNote就是一个适合您的工具。

## 舒适的Markdown体验
### Markdown的本质
Markdown作为一个简单标记语言，不像富文本，它的编辑和阅读有着与生俱来的隔阂。一般目前大概有三类方法来处理这个隔阂：

1. 作为一个极端，一些编辑器只是将Markdown作为无格式的纯文本处理。用户很容易在密密麻麻的黑漆漆的一片文字中找不着方向。
2. 大部分编辑器使用两个面板来同时编辑和预览Markdown笔记。从而，用户可以在编辑的同时看到优美的排版和布局。但是，两个面板基本会占据了整个屏幕，而用户的目光焦点左右频繁移动，往往也会使得用户无法专注编辑。
3. 作为另一个极端，一些编辑器在用户输入文本后立即将Markdown的标记转换为HTML元素，使得编写Markdown如同在Word文档里面编写富文本一样。

由于几乎所有的编辑器都选择第二种方法来处理隔阂，一提到Markdown人们往往会想起预览。这可能是对Markdown的一个最大的误解了。设计为一个简单的标记语言，Markdown的设计初衷就是为了在编辑的时候方便帮助跟踪文本的信息，而又能在阅读的时候被转换为HTML为发布提供美观的排版输出。所以，Markdown本身就应该在编辑的时候能够方便地跟踪和掌控文本的信息和脉络，而不需要通过预览这种接近饮鸩止渴的方法来方便编辑。

### 折中：VNote的方案
VNote尝试通过精心调配的**语法高亮**和其他一些特性，来最大程度地减小Markdown的这种割裂感，尽可能地提供一个*所见即所得*的编辑体验。用户在编辑的时候就能有效第把握内容脉络，也就没有必要进行预览或者强制更改文本为HTML元素了。

# 功能
## 基于笔记本的管理
VNote使用 **笔记本** 来管理笔记。类似于OneNote，一个笔记本可以保存在系统上的任意位置。一个笔记本对应于一个账户的概念。例如，您可以在本地文件系统上有一个笔记本，另外在某台OwnCloud服务器上保存另一个笔记本。当不同的笔记有不同的保密要求时，独立的笔记本就非常适用了。

一个笔记本对应于文件系统上的一个独立完整的文件夹（称为笔记本的 **根目录** ）。您可以将该文件夹拷贝到其他位置（或者另一台计算机上），然后将其导入到VNote中。

VNote支持一个笔记本中包含无限层级的文件夹。VNote支持在笔记本内或笔记本间拷贝或剪切文件夹和笔记。

![VNote Folder and File Panel](screenshots/vnote_002.png)

## 直观的笔记管理
所有笔记被保存为纯文本而且通过纯文本的配置文件进行管理。即使没有VNote，您也能方便访问您的数据。这样，您也可以使用第三方的文件同步服务来同步您的笔记，并在另一台计算机上导入到VNote中。

VNote支持Markdown和富文本笔记，其中Markdown笔记必须以`md`为后缀名。

## 语法高亮
VNote支持精确的Markdown语法高亮。通过精心调试的高亮样式，VNote使得您能够轻松跟踪和阅读您的文档。

VNote还支持Markdown编辑模式中代码块的语法高亮。目前的Markdown编辑器中绝大部分都尚不支持该特性。

![VNote Syntax Highlight](screenshots/vnote_003.png)

## 实时图片预览
VNote支持在编辑时原地预览图片链接。这样一来，您就能尽可能地留在编辑模式，避免频繁切换。

如果想要拷贝图片，可以选取该图片，然后复制。

![VNote Live Image Preview](screenshots/vnote_004.png)

## 良好的图片体验
编辑时，支持像其他富文本编辑器一样直接粘贴插入图片，VNote会帮您管理所插入的图片。VNote将这些图片保存在和笔记同一目录下的一个指定目录中。插入图片时，VNote会弹出一个窗口预览即将要插入的图片。另外，当您移除笔记中的图片链接时，VNote会自动删除对应的图片文件。

![VNote Image Insertion](screenshots/vnote_005.png)

## 编辑和阅读模式中的交互式大纲视图
VNote为编辑和预览模式都提供了一个用户友好的大纲视图。该大纲视图是一个项目树，而不是简单地插入一段HTML。

![VNote Outline Viewer](screenshots/vnote_006.png)

## 强大的快捷键
VNote提供很多快捷键，从而提供一个愉悦的编辑体验。其中包括 **Vim模式**、**舰长模式** 和 **导航模式**，它们能让您完全摆脱鼠标进行操作。

更多细节请参考帮助菜单中的[快捷键帮助](src/resources/docs/shortcuts_zh.md)。

## 窗口分割
VNote支持无限水平窗口分割，方便您进行笔记的整理和撰写。

![VNote Window Split](screenshots/vnote_007.png)

## 高度可定制
VNote中，几乎一切都是可以定制的，例如背景颜色、字体以及Markdown样式等。VNote使用一个纯文本文件来记录您的所有配置，因此通过拷贝该文件就能够很快地在另一台电脑上初始化一个新的VNote。

## 其他
VNote还支持其他很多的功能，比如：

- 高亮当前行；
- 高亮所选择的文本；
- 强大的页内查找；
- 自动缩进和自动列表；

# 构建与开发
VNote需要5.7版本以上的Qt进行构建。

1. 克隆代码仓库
    ```
    git clone https://github.com/tamlok/vnote.git vnote.git
    cd vnote.git
    git submodule update --init
    ```
2. 下载Qt  
下载[Qt 5.7.0](http://info.qt.io/download-qt-for-application-development)，导入`VNote.pro`创建一个工程。

## Linux
如果您的Linux发行版不提供5.7以上版本的Qt，那么您需要从其他来源获取Qt。在Ubuntu中，您可以执行以下步骤：

```
sudo add-apt-repository ppa:beineri/opt-qt571-trusty -y
sudo apt-get update -qq
sudo apt-get -y install qt57base qt57webengine qt57webchannel qt57svg qt57location qt57tools qt57translations
source /opt/qt*/bin/qt*-env.sh
```

当Qt和相关的模块准备就绪后，您可以执行以下命令来编译和安装：

```
cd vnote.git
mkdir build
cd build
qmake ../VNote.pro
make
sudo make install
```

更多细节，您可以参考源代码根目录下的 [.travis_linux.sh](.travis_linux.sh) 。

## MacOS
在macOS下，您可以执行以下步骤来编译：

1. 安装Xcode和Homebrew：
2. 通过Homebrew安装Qt5.7：

    ```
    brew install qt@5.7
    ```
3. 在VNote源码根目录下，新建一个文件`build_macos.sh`：

    ```sh
    QTDIR="/usr/local/opt/qt@5.7"
    PATH="$QTDIR/bin:$PATH"
    LDFLAGS=-L$QTDIR/lib
    CPPFLAGS=-I$QTDIR/include

    mkdir -p build
    cd build
    qmake -v
    qmake CONFIG-=debug CONFIG+=release ../VNote.pro
    make -j2
    ```
4. 修改`build_macos.sh`的执行权限，并执行：

    ```sh
    chmod +x build_macos.sh
    ./build_macos.sh
    ```
5. 此时得到VNote的Bundle `path/to/project/build/src/VNote.app`，打开即可。

# 依赖
- [Qt 5.9](http://qt-project.org) (L-GPL v3)
- [PEG Markdown Highlight](http://hasseg.org/peg-markdown-highlight/) (MIT License)
- [Hoedown 3.0.7](https://github.com/hoedown/hoedown/) (ISC License)
- [Marked](https://github.com/chjj/marked) (MIT License)
- [Highlight.js](https://github.com/isagalaev/highlight.js/) (BSD License)
- [Ionicons 2.0.1](https://github.com/driftyco/ionicons/) (MIT License)
- [markdown-it 8.3.1](https://github.com/markdown-it/markdown-it) (MIT License)
- [markdown-it-headinganchor 1.3.0](https://github.com/adam-p/markdown-it-headinganchor) (MIT License)
- [markdown-it-task-lists 1.4.0](https://github.com/revin/markdown-it-task-lists) (ISC License)
- [mermaid 7.0.0](https://github.com/knsv/mermaid) (MIT License)
- [MathJax](https://www.mathjax.org/) (Apache-2.0)
- [showdown](https://github.com/showdownjs/showdown) (Unknown)
- [flowchart.js](https://github.com/adrai/flowchart.js) (MIT License)

# 代码许可
VNote使用[MIT许可](http://opensource.org/licenses/MIT)。
