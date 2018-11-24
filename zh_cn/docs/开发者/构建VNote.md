# 构建VNote
您需要**Qt 5.9**才能从源代码创建VNote。

## 获取VNote的源代码
VNote的源代码可在 [Github](https://github.com/tamlok/vnote) 上获取。您可以下载代码的ZIP存档。请注意，VNote依赖于某些子模块，因此您还应该下载这些模块的源代码。

推荐的方法是像以下方式使用**git**：

```
git clone https://github.com/tamlok/vnote.git vnote.git
cd vnote.git
git submodule update --init
```

## 获取Qt 5.9
您可以从 [Qt Downloads](http://info.qt.io/download-qt-for-application-development) 获取完整的 Qt SDK。对于中国用户，您可以通过[TUNA 镜像](https://mirrors4.tuna.tsinghua.edu.cn/qt/official_releases/qt/5.9/) 加快下载速度。

## Windows
在 Windows 上，您需要 **Visual Studio 2015** 或更高版本来编译 VNote。

打开 **Qt Creator** 并打开`vnote.git\VNote.pro`作为项目。现在您已准备好调整和编译VNote！

## Linux
在Ubuntu中，你可以像这样从PPA获得 Qt 5.9：

```sh
sudo add-apt-repository ppa:beineri/opt-qt591-trusty -y
sudo apt-get update -qq
sudo apt-get -y install qt59base qt59webengine
sudo apt-get -y install qt59webchannel qt59svg qt59location qt59tools qt59translations
source /opt/qt*/bin/qt*-env.sh
```

然后像这样编译和安装VNote：

```sh
cd vnote.git
mkdir build
cd build
qmake ../VNote.pro
make
sudo make install
```

### Fcitx
如果您使用 **Fcitx** 作为输入方式，则需要将缺少的库 `libfcitxplatforminputcontextplugin.so` 复制到 Qt 的插件目录。

要找到 `libfcitxplatforminputcontextplugin.so` 的位置，您可以执行：

```sh
fcitx-diagnose | grep libfcitxplatforminputcontextplugin.so
```

如果没有这样的库，您可能需要在继续之前为 Qt5 正确安装和配置 Fcitx。

然后你需要将lib复制到Qt的插件目录：

```
<path_to_Qt_installation_directory>/5.9.3/gcc_64/plugins/platforminputcontexts/
```

### OpenSSL
VNote需要 **openSSL 1.0** 以实现联网。要验证它是否已正确设置，您可以在VNote的帮助菜单中检查更新。如果VNote无法检查更新，则需要将openSSL的库复制到Qt的库目录中。

安装openSSL后，您可以找到两个lib文件：

```
/usr/lib/libcrypto.so.1.0.0
/usr/lib/libssl.so.1.0.0
```

将这两个文件复制到Qt的库目录中：

```
<path_to_Qt_installation_directory>/5.9.3/gcc_64/lib/
```

在Qt的库目录中，为这两个文件创建符号链接：

```sh
ln -s libcrypto.so.1.0.0 libcrypto.so
ln -s libssl.so.1.0.0 libssl.so
```

## MacOS
如果您更喜欢macOS上的命令行，则可以按照以下步骤操作。

1. 安装Xcode和Homebrew;
2. 通过Homebrew安装Qt 5.9.1：

    ```
    brew install qt@5.9.1
    ```

3. 在项目目录中，像下面那样创建`build_macos.sh`：

    ```sh
    QTDIR="/usr/local/opt/qt@5.9.1"
    PATH="$QTDIR/bin:$PATH"
    LDFLAGS=-L$QTDIR/lib
    CPPFLAGS=-I$QTDIR/include

    mkdir -p build
    cd build
    qmake -v
    qmake CONFIG-=debug CONFIG+=release ../VNote.pro
    make -j2
    ```

4. 使 `build_macos.sh` 可执行并运行它：

    ```sh
    chmod +x build_macos.sh
    ./build_macos.sh
    ```

5. 现在你得到了bundle路径 `path/to/project/build/src/VNote.app` 。