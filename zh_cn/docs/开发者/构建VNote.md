# 构建VNote
您需要**Qt 5.15**或以上版本才能从源代码创建VNote。

## 获取VNote的源代码
VNote的源代码可在[GitHub](https://github.com/vnotex/vnote)上获取。您可以下载代码的ZIP存档。请注意，VNote依赖于某些子模块，因此您还应该下载这些模块的源代码。

推荐的方法是像以下方式使用**git**：

```
git clone https://github.com/vnotex/vnote.git vnote.git
cd vnote.git
git submodule update --init --recursive
```

## 获取Qt 5.15
您可以从[Qt Downloads](http://info.qt.io/download-qt-for-application-development)获取完整的Qt SDK。对于中国大陆用户，您可以通过[TUNA镜像](https://mirrors4.tuna.tsinghua.edu.cn/qt/official_releases/qt/5.15/)加快下载速度。

## Windows
在Windows上，您需要**Visual Studio 2015**或更高版本来编译VNote(Mingw**不**受支持)。

打开**Qt Creator**并打开`vnote.git\vnote.pro`作为项目。现在您已准备好调整和编译VNote！

## Linux
在Ubuntu中，你可以像这样从PPA获得Qt 5.15：

```sh
sudo add-apt-repository ppa:beineri/opt-qt5.15.2-bionic -y
sudo apt-get update -qq
sudo apt-get -y install qt512base qt512webengine
sudo apt-get -y install qt512webchannel qt512svg qt512location qt512tools qt512translations
source /opt/qt*/bin/qt*-env.sh
```

然后像这样编译和安装VNote：

```sh
cd vnote.git
mkdir build
cd build
qmake ../vnote.pro
make
sudo make install
```

### Fcitx
如果您使用**Fcitx**作为输入方式，则需要将缺少的库`libfcitxplatforminputcontextplugin.so`复制到Qt的插件目录。

要找到`libfcitxplatforminputcontextplugin.so`的位置，您可以执行：

```sh
fcitx-diagnose | grep libfcitxplatforminputcontextplugin.so
```

如果没有这样的库，您可能需要在继续之前为Qt5正确安装和配置Fcitx。

然后您需要将库文件复制到Qt的插件目录：

```
<path_to_Qt_installation_directory>/5.15.2/gcc_64/plugins/platforminputcontexts/
```

### OpenSSL
VNote需要**openSSL 1.0**以实现联网。

安装openSSL后，您可以找到两个库文件：

```
/usr/lib/libcrypto.so.1.0.0
/usr/lib/libssl.so.1.0.0
```

将这两个文件复制到Qt的库目录中：

```
<path_to_Qt_installation_directory>/5.15.2/gcc_64/lib/
```

在Qt的库目录中，为这两个文件创建符号链接：

```sh
ln -s libcrypto.so.1.0.0 libcrypto.so
ln -s libssl.so.1.0.0 libssl.so
```

## MacOS
如果您更喜欢macOS上的命令行操作方式，则可以按照以下步骤操作。

1. 安装Xcode和Homebrew;
2. 通过Homebrew安装Qt：

    ```
    brew install qt@5.15.2
    ```

3. 在项目目录中，像下面那样创建`build_macos.sh`：

    ```sh
    QTDIR="/usr/local/opt/qt@5.15.2"
    PATH="$QTDIR/bin:$PATH"
    LDFLAGS=-L$QTDIR/lib
    CPPFLAGS=-I$QTDIR/include

    mkdir -p build
    cd build
    qmake -v
    qmake CONFIG-=debug CONFIG+=release ../vnote.pro
    make -j2
    ```

4. 使`build_macos.sh`可执行并运行它：

    ```sh
    chmod +x build_macos.sh
    ./build_macos.sh
    ```

5. 现在你得到了bundle路径`path/to/project/build/src/vnote.app`。
