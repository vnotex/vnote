# Build VNote
You need **Qt 5.9** to build VNote from source.

## Get the Source Code of VNote
VNote's source code is available on [Github](https://github.com/tamlok/vnote). You could download the ZIP archive of the code. Please be aware of that VNote depends on some submodules, so you should also download the source codes of these modules.

The recommended way is using **git** like this:

```
git clone https://github.com/tamlok/vnote.git vnote.git
cd vnote.git
git submodule update --init
```

## Get Qt 5.9
You could get the standalone Qt SDK from [Qt Downloads](http://info.qt.io/download-qt-for-application-development). For users in China, you could speed up the download via the [TUNA Mirrors](https://mirrors4.tuna.tsinghua.edu.cn/qt/official_releases/qt/5.9/).

## Windows
On Windows, you need **Visual Studio 2015** or above to compile VNote.

Open **Qt Creator** and open `vnote.git\VNote.pro` as project. Now you are ready to tune and compile VNote!

## Linux
In Ubuntu, you could get Qt 5.9 from PPA like this:

```sh
sudo add-apt-repository ppa:beineri/opt-qt591-trusty -y
sudo apt-get update -qq
sudo apt-get -y install qt59base qt59webengine
sudo apt-get -y install qt59webchannel qt59svg qt59location qt59tools qt59translations
source /opt/qt*/bin/qt*-env.sh
```

Then compile and install VNote like this:

```sh
cd vnote.git
mkdir build
cd build
qmake ../VNote.pro
make
sudo make install
```

### Fcitx
If you use **Fcitx** as the input method, you need to copy the missing library `libfcitxplatforminputcontextplugin.so` to Qt's plugin directory.

To find the place of `libfcitxplatforminputcontextplugin.so`, you could execute:

```sh
fcitx-diagnose | grep libfcitxplatforminputcontextplugin.so
```

If there is no such lib, you may need to install and configure Fcitx for Qt5 correctly before continue.

Then you need to copy the lib to Qt's plugin directory:

```
<path_to_Qt_installation_directory>/5.9.3/gcc_64/plugins/platforminputcontexts/
```

### OpenSSL
VNote needs **openSSL 1.0** for networking. To verify if it has been setup correctly, you could check for updates in the *Help* menu of VNote. If VNote fails to check for updates, then you need to copy libraries of openSSL to Qt's library directory.

After the installation of openSSL, you could find two lib files:

```
/usr/lib/libcrypto.so.1.0.0
/usr/lib/libssl.so.1.0.0
```

Copy these two files to Qt's library directory:

```
<path_to_Qt_installation_directory>/5.9.3/gcc_64/lib/
```

In Qt's library directory, create symlinks for these two files:

```sh
ln -s libcrypto.so.1.0.0 libcrypto.so
ln -s libssl.so.1.0.0 libssl.so
```

## MacOS
If you prefer command line on macOS, you could follow these steps.

1. Install Xcode and Homebrew;
2. Install Qt 5.9.1 via Homebrew:

    ```
    brew install qt@5.9.1
    ```

3. In the project directory, create `build_macos.sh` like this:

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

4. Make `build_macos.sh` executable and run it:

    ```sh
    chmod +x build_macos.sh
    ./build_macos.sh
    ```

5. Now you got the bundle `path/to/project/build/src/VNote.app`.