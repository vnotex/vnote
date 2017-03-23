#!/bin/bash
project_dir=$(pwd)

brew update > /dev/null
brew install qt@5.7
QTDIR="/usr/local/opt/qt@5.7"
PATH="$QTDIR/bin:$PATH"
LDFLAGS=-L$QTDIR/lib
CPPFLAGS=-I$QTDIR/include

# Build your app
cd ${project_dir}
mkdir build
cd build
qmake -v
qmake -config release ../VNote.pro
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
mv VNote.dmg VNote_mac_X64_${version}.dmg
cd ..

exit 0
