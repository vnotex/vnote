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
qmake CONFIG+=release -spec linux-g++-64 ../VNote.pro
make -j2

#
# Pack AppImage using linuxdeployqt
#
mkdir dist
INSTALL_ROOT=${project_dir}/build/dist make install ; tree dist/

# Copy SVG module
mkdir -p dist/usr/plugins/iconengines
mkdir -p dist/usr/plugins/imageformats
cp "${qt_install_dir}"/Qt/5.7/gcc_64/plugins/iconengines/* dist/usr/plugins/iconengines/
cp "${qt_install_dir}"/Qt/5.7/gcc_64/plugins/imageformats/* dist/usr/plugins/imageformats/

# Copy other project files
cp "${project_dir}/README.md" "dist/README.md"
cp "${project_dir}/LICENSE" "dist/LICENSE"
echo ${version} > dist/version
echo "${TRAVIS_COMMIT}" >> dist/version

# Get linuxdeployqt tool
wget -c "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt*.AppImage
./linuxdeployqt*.AppImage ./dist/usr/share/applications/*.desktop -bundle-non-qt-libs
./linuxdeployqt*.AppImage ./dist/usr/share/applications/*.desktop -appimage

tree dist/

ls -l *.AppImage

mv VNote-*.AppImage VNote_x86_64_${version}.AppImage

cd ..

exit 0
