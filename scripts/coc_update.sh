#!/bin/sh
if [ -n "$1" ]; then
    echo Qt include directory: $1
else
    echo Please specify the Qt include directory.
    exit
fi

ccls_file=".ccls"

echo clang > $ccls_file
echo -fcxx-exceptions >> $ccls_file
echo -std=c++14 >> $ccls_file
echo -Isrc/core >> $ccls_file
echo -Isrc >> $ccls_file
echo -Ilibs/vtextedit/src/editor/include >> $ccls_file
echo -Ilibs/vtitlebar/src >> $ccls_file
echo -I$1 >> $ccls_file
echo -I$1/QtCore >> $ccls_file
echo -I$1/QtWebEngineWidgets >> $ccls_file
echo -I$1/QtSvg >> $ccls_file
echo -I$1/QtPrintSupport >> $ccls_file
echo -I$1/QtWidgets >> $ccls_file
echo -I$1/QtWebEngineCore >> $ccls_file
echo -I$1/QtGui >> $ccls_file
echo -I$1/QtWebChannel >> $ccls_file
echo -I$1/QtNetwork >> $ccls_file
echo -I$1/QtTest >> $ccls_file

cp -f .ccls compile_flags.txt
