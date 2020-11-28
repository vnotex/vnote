@echo off
rem Update .ccls project file for ccls LPS and compile_flags.txt for clangd

if "%~1"=="" (
    echo missing argument: the location of Qt's include directory
    EXIT /B 0
)

set qt_inc=%~1
set qt_inc=%qt_inc:\=\\%

(
    echo clang
    echo -fcxx-exceptions
    echo -std=c++14
    echo -Isrc\\core
    echo -Isrc
    echo -Ilibs\\vtextedit\\src\\editor\\include
    echo -Ilibs\\vtitlebar\\src
    echo -I%qt_inc%
    echo -I%qt_inc%\\QtCore
    echo -I%qt_inc%\\QtWebEngineWidgets
    echo -I%qt_inc%\\QtSvg
    echo -I%qt_inc%\\QtPrintSupport
    echo -I%qt_inc%\\QtWidgets
    echo -I%qt_inc%\\QtWebEngineCore
    echo -I%qt_inc%\\QtGui
    echo -I%qt_inc%\\QtWebChannel
    echo -I%qt_inc%\\QtNetwork
    echo -I%qt_inc%\\QtTest
) > ".ccls"

copy /Y .ccls compile_flags.txt
