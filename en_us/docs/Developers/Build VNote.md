# Build VNote
You need **Qt 6.5** and above, and CMake to build VNote from source.

## Get the Source Code of VNote
VNote's source code is available on [GitHub](https://github.com/vnotex/vnote). You could download the ZIP archive of the code. Please be aware of that VNote depends on some submodules, so you should also download the source codes of these modules.

The recommended way is using **git** like this:

```
git clone https://github.com/vnotex/vnote.git vnote.git
cd vnote.git
git submodule update --init --recursive
```

## Get Qt6
You could get the standalone Qt SDK from [Qt Downloads](http://info.qt.io/download-qt-for-application-development).

## Windows
On Windows, you need **Visual Studio 2019** or above to compile VNote (Mingw is **not** supported).

Open **Qt Creator** and open `vnote.git\CMakeLists.txt` as project. Now you are ready to tune and compile VNote!

## Linux
For detailed steps, please refer to the [CI script](https://github.com/vnotex/vnote/blob/master/.github/workflows/ci-linux.yml).

```sh
cd vnote.git
mkdir build
cd build
cmake ..
cmake --build .
sudo make install
```

## MacOS
If you prefer command line on macOS, you could follow these steps.

1. Install Xcode and Homebrew;
2. Install Qt6 via Homebrew:

    ```
    brew install qt@6.5.3
    ```

3. In the project directory, create `build_macos.sh` like this:

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

4. Make `build_macos.sh` executable and run it:

    ```sh
    chmod +x build_macos.sh
    ./build_macos.sh
    ```

5. Now you got the bundle `path/to/project/build/src/VNote.dmg`.
