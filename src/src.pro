lessThan(QT_MAJOR_VERSION, 5): error("requires Qt 5 and above")

equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 12): error("requires Qt 5.12 and above")

QT += core gui widgets webenginewidgets webchannel network svg printsupport

CONFIG -= qtquickcompiler

# Enable message log in release build
DEFINES += QT_MESSAGELOGCONTEXT

TARGET = vnote
TEMPLATE = app

win32:CONFIG(release, debug|release) {
    DESTDIR = $$OUT_PWD/release
} else:win32:CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/debug
} else {
    DESTDIR = $$OUT_PWD
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

rcc_binary.commands = $$[QT_HOST_BINS]/rcc -name ${QMAKE_FILE_IN_BASE} -binary ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
rcc_binary.depend_command = $$[QT_HOST_BINS]/rcc -list $$QMAKE_RESOURCE_FLAGS ${QMAKE_FILE_IN}
rcc_binary.input = RCC_BINARY_SOURCES
rcc_binary.output = $$DESTDIR/${QMAKE_FILE_IN_BASE}.rcc
rcc_binary.CONFIG += no_link target_predeps
QMAKE_EXTRA_COMPILERS += rcc_binary

OTHER_FILES += $$RCC_BINARY_SOURCES
