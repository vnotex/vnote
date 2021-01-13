lessThan(QT_MAJOR_VERSION, 5): error("requires Qt 5 and above")

equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 12): error("requires Qt 5.12 and above")

QT += core gui widgets webenginewidgets webchannel network svg printsupport

CONFIG -= qtquickcompiler

unix:!mac:exists(/usr/bin/ld.gold) {
    CONFIG += use_gold_linker
}

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

TRANSLATIONS += data/core/translations/vnote_zh_CN.ts

SOURCES += \
    main.cpp

INCLUDEPATH *= $$PWD

LIBS_FOLDER = $$PWD/../libs

include($$LIBS_FOLDER/vtextedit/src/editor/editor_export.pri)

include($$LIBS_FOLDER/vtextedit/src/libs/syntax-highlighting/syntax-highlighting_export.pri)

include($$PWD/utils/utils.pri)

include($$PWD/export/export.pri)

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

macx {
    QMAKE_INFO_PLIST = data/core/Info.plist

    # Process VTextEdit framework
    vte_lib_name = VTextEdit
    vte_lib_dir = $${OUT_PWD}/../libs/vtextedit/src/editor
    vte_lib_full_name = $${vte_lib_name}.framework/Versions/1/$${vte_lib_name}
    app_bundle_dir = $${TARGET}.app/Contents/MacOS
    app_target = $${app_bundle_dir}/$${TARGET}
    QMAKE_POST_LINK += \
        install_name_tool -add_rpath $${vte_lib_dir} $${app_target} && \
        install_name_tool -change $${vte_lib_full_name} @rpath/$${vte_lib_full_name} $${app_target} &&

    # Process VSyntaxHighlighting framework
    sh_lib_name = VSyntaxHighlighting
    sh_lib_dir = $${OUT_PWD}/../libs/vtextedit/src/libs/syntax-highlighting
    sh_lib_full_name = $${sh_lib_name}.framework/Versions/1/$${sh_lib_name}
    QMAKE_POST_LINK += \
        install_name_tool -add_rpath $${sh_lib_dir} $${app_target} && \
        install_name_tool -change $${sh_lib_full_name} @rpath/$${sh_lib_full_name} $${app_target}

    # Move vnote_extra.rcc to the bundle.
    BUNDLE_EXTRA_RCC.files = $${SRC_DESTDIR}/vnote_extra.rcc
    BUNDLE_EXTRA_RCC.path = Contents/MacOS
    QMAKE_BUNDLE_DATA += BUNDLE_EXTRA_RCC
}

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

    extraresource.path = $${BINDIR}
    extraresource.extra = cp $${SRC_DESTDIR}/vnote_extra.rcc $(INSTALL_ROOT)$${BINDIR}/vnote_extra.rcc

    INSTALLS += target desktop icon16 icon32 icon48 icon64 icon128 icon256 iconsvg
    INSTALLS += extraresource
    message("VNote will be installed in prefix $${PREFIX}")
}
