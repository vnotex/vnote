#!/bin/bash
project_dir=$(pwd)

# Complain when not in Travis environment
if [ -z ${TRAVIS_COMMIT+x} ]; then
    echo "This script is intended to be used only in Travis CI environment."
    echo "To build VNote from source, please see the [documentation](https://tamlok.github.io/vnote/en_us/#!docs/Developers/Build%20VNote.md)."
    exit 1
fi

brew update > /dev/null

brew install gnu-getopt
export PATH="/usr/local/opt/gnu-getopt/bin:$PATH"

# Download Qt from Qt Installer
cd ${project_dir}
mkdir build
cd build

export VERBOSE=1
export QT_CI_PACKAGES="qt.qt5.598.clang_64,qt.qt5.598.qtwebengine"
export QT_CI_LOGIN="tamlok@qq.com"
export QT_CI_PASSWORD="TravisCI@VNote"

git clone https://github.com/tamlok/qtci.git
source qtci/path.env

install-qt 5.9.8
source qt-5.9.8.env

echo $PATH

QTDIR="${project_dir}/build/Qt/5.9.8/clang_64"
LDFLAGS=-L$QTDIR/lib
CPPFLAGS=-I$QTDIR/include

# Build your app
cd ${project_dir}/build
qmake -v
qmake CONFIG-=debug CONFIG+=release ../VNote.pro
make -j2

git clone https://github.com/aurelien-rainone/macdeployqtfix.git

# Package DMG from build/src/VNote.app directory
cd src/

sed -i -e 's/com.yourcompany.VNote/com.tamlok.VNote/g' VNote.app/Contents/Info.plist
$QTDIR/bin/macdeployqt VNote.app
python ../macdeployqtfix/macdeployqtfix.py VNote.app/Contents/MacOS/VNote $QTDIR

# Fix Helpers/QtWebEngineProcess.app
cd VNote.app/Contents/Frameworks/QtWebEngineCore.framework/Versions/5/Helpers
$QTDIR/bin/macdeployqt QtWebEngineProcess.app
python ${project_dir}/build/macdeployqtfix/macdeployqtfix.py QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess $QTDIR

cd ${project_dir}/build
mkdir -p distrib/VNote
cd distrib/VNote
mv ../../src/VNote.app ./
cp "${project_dir}/LICENSE" "LICENSE"
cp "${project_dir}/README.md" "README.md"
echo "${version}" > version
echo "${TRAVIS_COMMIT}" >> version

ln -s /Applications ./Applications

cd ..
hdiutil create -srcfolder ./VNote -format UDBZ ./VNote.dmg
mv VNote.dmg VNote-${version}-x64.dmg
cd ..

exit 0
