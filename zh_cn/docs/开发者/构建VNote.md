# 构建VNote
您需要**Qt 6.5**或以上版本才能从源代码创建VNote。

## 获取VNote的源代码
VNote的源代码可在[GitHub](https://github.com/vnotex/vnote)上获取。您可以下载代码的ZIP存档。请注意，VNote依赖于某些子模块，因此您还应该下载这些模块的源代码。

推荐的方法是像以下方式使用**git**：

```
git clone https://github.com/vnotex/vnote.git vnote.git
cd vnote.git
git submodule update --init --recursive
```

## 获取Qt6
您可以从[Qt Downloads](http://info.qt.io/download-qt-for-application-development)获取完整的Qt SDK。

## Windows
在Windows上，您需要**Visual Studio 2019**或更高版本来编译VNote(Mingw**不**受支持)。

打开**Qt Creator**并打开`vnote.git\CMakeLists.txt`作为项目。现在您已准备好调整和编译VNote！

## Linux
详细步骤，请参考[持续构建脚本](https://github.com/vnotex/vnote/blob/master/.github/workflows/ci-linux.yml)。

```sh
cd vnote.git
mkdir build
cd build
cmake ..
cmake --build .
sudo make install
```

## MacOS
如果您更喜欢macOS上的命令行操作方式，则可以按照以下步骤操作。

1. 安装Xcode和Homebrew;
2. 通过Homebrew安装Qt：

    ```
    brew install qt@6.5.3
    ```

3. 在项目目录中，像下面那样创建`build_macos.sh`：

    ```sh
    QTDIR="/usr/local/opt/qt@6.5.3"
    PATH="$QTDIR/bin:$PATH"
    LDFLAGS=-L$QTDIR/lib
    CPPFLAGS=-I$QTDIR/include

    mkdir -p build
    cd build
    cmake ..
    cmake --build . --target pack
    ```

4. 使`build_macos.sh`可执行并运行它：

    ```sh
    chmod +x build_macos.sh
    ./build_macos.sh
    ```

5. 现在你得到了bundle `path/to/project/build/src/VNote.dmg`。
