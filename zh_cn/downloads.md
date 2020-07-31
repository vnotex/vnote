# 下载
::: alert-info

如果是更新VNote版本，可以直接删除原来的旧版本并下载最新版本。

:::

国内的用户可以尝试在[天翼云盘](https://cloud.189.cn/t/Av67NvmEJVBv)下载 VNote 的最新发行版本。

## Windows
### 官方压缩包 ![](https://ci.appveyor.com/api/projects/status/github/tamlok/vnote?svg=true)

- [GitHub releases](https://github.com/tamlok/vnote/releases)
- master分支的最新构建：[ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

::: alert-warning

VNote不支持**XP**，因为QtWebEngineProcess无法在XP上运行。

:::

### Scoop
VNote也可以通过Scoop的`extras`仓库进行安装。

```shell
scoop bucket add extras
scoop install vnote
scoop update vnote
```

## Linux
### AppImage ![Build Status](https://travis-ci.org/tamlok/vnote.svg?branch=master)

VNote当前为主要Linux发行版提供了一个AppImage格式的独立可执行文件。希望了解Linux系统下打包发布的开发人员能提供这方面进一步的帮助！

- [GitHub releases](https://github.com/tamlok/vnote/releases)
- master分支的最新构建：[ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

### openSUSE
目前 openSUSE Tumbleweed 可以通过 OBS 上的 `home:opensuse_zh` 源直接安装。您可以直接执行以下命令：

```shell
sudo zypper ar https://download.opensuse.org/repositories/home:/opensuse_zh/openSUSE_Tumbleweed/ home:opensuse_zh
sudo zypper ref
sudo zypper in vnote
```

其他架构请直接在 [software.opensuse.org](https://software.opensuse.org) 搜索 `vnote`。

由于 Leap 42 及以下版本的 Qt 版本过低，我们无法在 OBS 上进行打包。请使用 AppImage 或自行构建。

### Arch Linux
Arch Linux可以通过AUR中的 [vnote-bin](https://aur.archlinux.org/packages/vnote-bin/) 进行安装：

```shell
git clone https://aur.archlinux.org/vnote-bin.git
cd vnote-bin
makepkg -sic
```

AUR也提供一个和最新master分支同步的开发版本 [vnote-git](https://aur.archlinux.org/packages/vnote-git/) 。

## MacOS ![Build Status](https://travis-ci.org/tamlok/vnote.svg?branch=master)

- [GitHub releases](https://github.com/tamlok/vnote/releases)
- master分支的最新构建：[ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

也可以通过 homebrew cask 进行安装：

```shell
brew cask install vnote
```
