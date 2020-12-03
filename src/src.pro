lessThan(QT_MAJOR_VERSION, 5): error("requires Qt 5 and above")

equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 12): error("requires Qt 5.12 and above")

QT += core gui widgets webenginewidgets webchannel network svg printsupport

CONFIG -= qtquickcompiler

# Enable message log in release build
DEFINES += QT_MESSAGELOGCONTEXT

TARGET = vnote
TEMPLATE = app

win32:CONFIG(release, debug|release) {
    SRC_DESTDIR = $$OUT_PWD/release
} else:win32:CONFIG(debug, debug|release) {
    SRC_DESTDIR = $$OUT_PWD/debug
} else {
    SRC_DESTDIR = $$OUT_PWD
}

RC_ICONS = data/core/icons/vnote.ico
ICON = data/core/icons/vnote.icns

SOURCES += \
    main.cpp

INCLUDEPATH *= $$PWD

LIBS_FOLDER = $$PWD/../libs

include($$LIBS_FOLDER/vtitlebar/vtitlebar_export.pri)

include($$LIBS_FOLDER/vtextedit/src/editor/editor_export.pri)

include($$PWD/utils/utils.pri)

include($$PWD/core/core.pri)

include($$PWD/widgets/widgets.pri)

RESOURCES += \
    $$PWD/data/core/core.qrc

RCC_BINARY_SOURCES += $$PWD/data/extra/extra.qrc

win32 {
    rcc_binary.commands = $$shell_path($$[QT_HOST_BINS]/rcc.exe) -name ${QMAKE_FILE_IN_BASE} -binary ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
    rcc_binary.depend_command = $$shell_path($$[QT_HOST_BINS]/rcc.exe) -list $$QMAKE_RESOURCE_FLAGS ${QMAKE_FILE_IN}
} else {
    rcc_binary.commands = $$[QT_HOST_BINS]/rcc -name ${QMAKE_FILE_IN_BASE} -binary ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
    rcc_binary.depend_command = $$[QT_HOST_BINS]/rcc -list $$QMAKE_RESOURCE_FLAGS ${QMAKE_FILE_IN}
}
rcc_binary.input = RCC_BINARY_SOURCES
rcc_binary.output = $$SRC_DESTDIR/vnote_${QMAKE_FILE_IN_BASE}.rcc
rcc_binary.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += rcc_binary

OTHER_FILES += $$RCC_BINARY_SOURCES

## INSTALLS
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr
    DATADIR = $${PREFIX}/share
    BINDIR = $${PREFIX}/bin
    LIBDIR = $${PREFIX}/lib
    INCLUDEDIR = $${PREFIX}/include

    # install desktop file
    desktop.path = $${DATADIR}/applications
    desktop.files += data/core/vnote.desktop

    # install icons
    icon16.path = $${DATADIR}/icons/hicolor/16x16/apps
    icon16.files = data/core/logo/16x16/vnote.png

    icon32.path = $${DATADIR}/icons/hicolor/32x32/apps
    icon32.files = data/core/logo/32x32/vnote.png

    icon48.path = $${DATADIR}/icons/hicolor/48x48/apps
    icon48.files = data/core/logo/48x48/vnote.png

    icon64.path = $${DATADIR}/icons/hicolor/64x64/apps
    icon64.files = data/core/logo/64x64/vnote.png

    icon128.path = $${DATADIR}/icons/hicolor/128x128/apps
    icon128.files = data/core/logo/128x128/vnote.png

    icon256.path = $${DATADIR}/icons/hicolor/256x256/apps
    icon256.files = data/core/logo/256x256/vnote.png

    iconsvg.path = $${DATADIR}/icons/hicolor/scalable/apps
    iconsvg.files = data/core/logo/vnote.svg

    target.path = $${BINDIR}

    extrarcc.path = $${BINDIR}
    extrarcc.files = $${SRC_DESTDIR}/vnote_extra.rcc

    INSTALLS += target extrarcc desktop icon16 icon32 icon48 icon64 icon128 icon256 iconsvg
    message("VNote will be installed in prefix $${PREFIX}")
}
