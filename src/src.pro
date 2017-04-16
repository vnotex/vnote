#-------------------------------------------------
#
# Project created by QtCreator 2016-10-01T11:03:59
#
#-------------------------------------------------

QT       += core gui webenginewidgets webchannel network svg

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = VNote
TEMPLATE = app

RC_ICONS = resources/icons/vnote.ico
ICON = resources/icons/vnote.icns

TRANSLATIONS += translations/vnote_zh_CN.ts

SOURCES += main.cpp\
    vmainwindow.cpp \
    vdirectorytree.cpp \
    vnote.cpp \
    vnotebook.cpp \
    dialog/vnewdirdialog.cpp \
    vconfigmanager.cpp \
    vfilelist.cpp \
    dialog/vnewfiledialog.cpp \
    vedit.cpp \
    vdocument.cpp \
    utils/vutils.cpp \
    vpreviewpage.cpp \
    hgmarkdownhighlighter.cpp \
    vstyleparser.cpp \
    dialog/vnewnotebookdialog.cpp \
    vmarkdownconverter.cpp \
    dialog/vnotebookinfodialog.cpp \
    dialog/vdirinfodialog.cpp \
    dialog/vfileinfodialog.cpp \
    veditoperations.cpp \
    vmdeditoperations.cpp \
    dialog/vinsertimagedialog.cpp \
    vdownloader.cpp \
    veditarea.cpp \
    veditwindow.cpp \
    vedittab.cpp \
    voutline.cpp \
    vtoc.cpp \
    vsingleinstanceguard.cpp \
    vdirectory.cpp \
    vfile.cpp \
    vnotebookselector.cpp \
    vnofocusitemdelegate.cpp \
    vavatar.cpp \
    vmdedit.cpp \
    dialog/vfindreplacedialog.cpp \
    dialog/vsettingsdialog.cpp \
    dialog/vdeletenotebookdialog.cpp \
    dialog/vselectdialog.cpp \
    vcaptain.cpp \
    vopenedlistmenu.cpp \
    vorphanfile.cpp \
    vcodeblockhighlighthelper.cpp

HEADERS  += vmainwindow.h \
    vdirectorytree.h \
    vnote.h \
    vnotebook.h \
    dialog/vnewdirdialog.h \
    vconfigmanager.h \
    vfilelist.h \
    dialog/vnewfiledialog.h \
    vedit.h \
    vconstants.h \
    vdocument.h \
    utils/vutils.h \
    vpreviewpage.h \
    hgmarkdownhighlighter.h \
    vstyleparser.h \
    dialog/vnewnotebookdialog.h \
    vmarkdownconverter.h \
    dialog/vnotebookinfodialog.h \
    dialog/vdirinfodialog.h \
    dialog/vfileinfodialog.h \
    veditoperations.h \
    vmdeditoperations.h \
    dialog/vinsertimagedialog.h \
    vdownloader.h \
    veditarea.h \
    veditwindow.h \
    vedittab.h \
    voutline.h \
    vtoc.h \
    vsingleinstanceguard.h \
    vdirectory.h \
    vfile.h \
    vnotebookselector.h \
    vnofocusitemdelegate.h \
    vavatar.h \
    vmdedit.h \
    dialog/vfindreplacedialog.h \
    dialog/vsettingsdialog.h \
    dialog/vdeletenotebookdialog.h \
    dialog/vselectdialog.h \
    vcaptain.h \
    vopenedlistmenu.h \
    vnavigationmode.h \
    vorphanfile.h \
    vcodeblockhighlighthelper.h

RESOURCES += \
    vnote.qrc \
    translations.qrc

OTHER_FILES += \
    utils/highlightjs/highlight.pack.js \
    utils/markdown-it/markdown-it-headinganchor.js \
    utils/markdown-it/markdown-it-task-lists.min.js \
    utils/markdown-it/markdown-it.min.js \
    utils/marked/marked.min.js \
    utils/mermaid/mermaidAPI.min.js

macx {
    LIBS += -L/usr/local/lib
    INCLUDEPATH += /usr/local/include
}

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../hoedown/release/ -lhoedown
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../hoedown/debug/ -lhoedown
else:unix: LIBS += -L$$OUT_PWD/../hoedown/ -lhoedown

INCLUDEPATH += $$PWD/../hoedown
DEPENDPATH += $$PWD/../hoedown

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../peg-highlight/release/ -lpeg-highlight
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../peg-highlight/debug/ -lpeg-highlight
else:unix: LIBS += -L$$OUT_PWD/../peg-highlight/ -lpeg-highlight

INCLUDEPATH += $$PWD/../peg-highlight
DEPENDPATH += $$PWD/../peg-highlight

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/release/libpeg-highlight.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/debug/libpeg-highlight.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/release/peg-highlight.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/debug/peg-highlight.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../peg-highlight/libpeg-highlight.a

## INSTALLS
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr
    DATADIR = $${PREFIX}/share

    # install desktop file
    desktop.path = $${DATADIR}/applications
    desktop.files += vnote.desktop

    # install icons
    icon16.path = $${DATADIR}/icons/hicolor/16x16/apps
    icon16.files = resources/icons/16x16/vnote.png

    icon32.path = $${DATADIR}/icons/hicolor/32x32/apps
    icon32.files = resources/icons/32x32/vnote.png

    icon48.path = $${DATADIR}/icons/hicolor/48x48/apps
    icon48.files = resources/icons/48x48/vnote.png

    icon64.path = $${DATADIR}/icons/hicolor/64x64/apps
    icon64.files = resources/icons/64x64/vnote.png

    icon128.path = $${DATADIR}/icons/hicolor/128x128/apps
    icon128.files = resources/icons/128x128/vnote.png

    icon256.path = $${DATADIR}/icons/hicolor/256x256/apps
    icon256.files = resources/icons/256x256/vnote.png

    iconsvg.path = $${DATADIR}/icons/hicolor/scalable/apps
    iconsvg.files = resources/icons/vnote.svg

    target.path = $${PREFIX}/bin

    INSTALLS += target desktop icon16 icon32 icon48 icon64 icon128 icon256 iconsvg
    message("VNote will be installed in prefix $${PREFIX}")
}
