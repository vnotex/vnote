#-------------------------------------------------
#
# Project created by QtCreator 2016-10-01T11:03:59
#
#-------------------------------------------------

QT       += core gui webenginewidgets webchannel network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = VNote
TEMPLATE = app

RC_ICONS = resources/icons/vnote.ico

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
    vnofocusitemdelegate.cpp

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
    vnofocusitemdelegate.h

RESOURCES += \
    vnote.qrc

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
