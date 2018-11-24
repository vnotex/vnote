# Downloads
::: alert-info

To upgrade VNote, you could just simply remove the old package and download the new one.

:::

## Windows
### Official Zip ![](https://ci.appveyor.com/api/projects/status/github/tamlok/vnote?svg=true)

- [Github releases](https://github.com/tamlok/vnote/releases)
- Latest builds on master: [ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

::: alert-warning

**NOT** supported in XP since QtWebEngineProcess used by VNote could not work in XP.

:::

### Scoop
VNote can be installed from `extras` bucket of Scoop.

```shell
scoop bucket add extras
scoop install vnote
scoop update vnote
```

## Linux
### AppImage ![](https://travis-ci.org/tamlok/vnote.svg?branch=master)

There is an AppImage format standalone executable of VNote for major Linux distributions. **Any help for packaging and distribution on Linux is appreciated!**

- [Github releases](https://github.com/tamlok/vnote/releases)
- Latest builds on master: [ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

### openSUSE
Currently `vnote` on openSUSE Tumbleweed can be installed from `home:opensuse_zh` project on OBS. You can execute the following command directly:

```shell
sudo zypper ar https://download.opensuse.org/repositories/home:/opensuse_zh/openSUSE_Tumbleweed/ home:opensuse_zh
sudo zypper ref
sudo zypper in vnote
```

For other architectures, please search for `vnote` at [software.opensuse.org](https://software.opensuse.org).

We don't support Leap 42 and below due to the Qt version. Please use AppImage or build it yourself.

### Arch Linux
VNote on Arch Linux can be installed from the AUR as [vnote](https://aur.archlinux.org/packages/vnote-bin/):

```shell
git clone https://aur.archlinux.org/vnote-bin.git
cd vnote-bin
makepkg -sic
```

There is also a development version that tracks the latest master [vnote-git](https://aur.archlinux.org/packages/vnote-git/).

## MacOS ![](https://travis-ci.org/tamlok/vnote.svg?branch=master)

- [Github releases](https://github.com/tamlok/vnote/releases)
- Latest builds on master: [ ![Download](https://api.bintray.com/packages/tamlok/vnote/vnote/images/download.svg) ](https://bintray.com/tamlok/vnote/vnote/_latestVersion)

You can also install VNote using homebrew, through the cask tap:

```shell
brew cask install vnote
```
