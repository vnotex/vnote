#!/bin/bash
project_dir=$(pwd)

# Install qt5.7
sudo add-apt-repository ppa:beineri/opt-qt571-trusty -y
sudo apt-get update -qq
sudo apt-get -y install qt57base qt57webengine qt57webchannel qt57svg qt57location qt57tools qt57translations
source /opt/qt*/bin/qt*-env.sh

cd ${project_dir}
mkdir build
cd build
qmake -v
qmake CONFIG+=release -spec linux-g++-64 ../VNote.pro
make -j$(nproc)

#
# Pack AppImage using linuxdeployqt
#
mkdir dist
INSTALL_ROOT=${project_dir}/build/dist make install ; tree dist/

# Copy SVG module
mkdir -p ./dist/usr/plugins/iconengines
mkdir -p ./dist/usr/plugins/imageformats
cp /opt/qt57/plugins/iconengines/* ./dist/usr/plugins/iconengines/
cp /opt/qt57/plugins/imageformats/* ./dist/usr/plugins/imageformats/

# Copy other project files
cp "${project_dir}/README.md" "dist/README.md"
cp "${project_dir}/LICENSE" "dist/LICENSE"
echo ${version} > ./dist/version
echo "${TRAVIS_COMMIT}" >> ./dist/version

# Get linuxdeployqt tool
wget -c "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt*.AppImage
unset QTDIR; unset QT_PLUGIN_PATH ; unset LD_LIBRARY_PATH
./linuxdeployqt*.AppImage ./dist/usr/share/applications/*.desktop -bundle-non-qt-libs

# Copy translations
cp /opt/qt57/translations/*_zh_CN.qm ./dist/usr/translations/

# Package it for the second time.
./linuxdeployqt*.AppImage ./dist/usr/share/applications/*.desktop -appimage

tree dist/

ls -l *.AppImage

mv VNote-*.AppImage VNote_x86_64_${version}.AppImage

cd ..

exit 0
