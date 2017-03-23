#!/bin/bash
project_dir=$(pwd)
qt_install_dir=/opt

cd ${qt_install_dir}
sudo wget https://github.com/adolby/qt-more-builds/releases/download/5.7/qt-opensource-5.7.0-linux-x86_64.7z
sudo 7z x qt-opensource-5.7.0-linux-x86_64.7z &> /dev/null
PATH=${qt_install_dir}/Qt/5.7/gcc_64/bin/:${PATH}

cd ${project_dir}
mkdir build
cd build
qmake -v
qmake -config release -spec linux-g++-64 ../VNote.pro
make

mkdir -p distrib/VNote
cd distrib/VNote

# Copy VNote executable
cp ../../src/VNote ./

# Copy ICU libraries
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libicui18n.so.56.1" "libicui18n.so.56"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libicuuc.so.56.1" "libicuuc.so.56"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libicudata.so.56.1" "libicudata.so.56"

mkdir platforms
cp "${qt_install_dir}/Qt/5.7/gcc_64/plugins/platforms/libqxcb.so" "platforms/libqxcb.so"
cp "${qt_install_dir}/Qt/5.7/gcc_64/plugins/platforms/libqminimal.so" "platforms/libqminimal.so"

cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5WebEngineWidgets.so.5.7.0" "libQt5WebEngineWidgets.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5WebEngineCore.so.5.7.0" "libQt5WebEngineCore.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5Widgets.so.5.7.0" "libQt5Widgets.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5WebChannel.so.5.7.0" "libQt5WebChannel.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5Core.so.5.7.0" "libQt5Core.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5Gui.so.5.7.0" "libQt5Gui.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5DBus.so.5.7.0" "libQt5DBus.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5XcbQpa.so.5.7.0" "libQt5XcbQpa.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5Qml.so.5.7.0" "libQt5Qml.so.5"
cp "${qt_install_dir}/Qt/5.7/gcc_64/lib/libQt5Network.so.5.7.0" "libQt5Network.so.5"

# Use chrpath to set up rpaths for Qt's libraries so they can find
# each other
chrpath -r \$ORIGIN/.. platforms/libqxcb.so
chrpath -r \$ORIGIN/.. platforms/libqminimal.so

# Copy other project files
cp "${project_dir}/README.md" "README.md"
cp "${project_dir}/LICENSE" "LICENSE"
echo ${version} > version
echo "${TRAVIS_COMMIT}" >> version

# Package portable executable
cd ..
tar -czvf VNote_linux_x86_64_portable_${version}.tar.gz VNote

exit 0
