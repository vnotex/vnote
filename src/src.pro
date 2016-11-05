#-------------------------------------------------
#
# Project created by QtCreator 2016-10-01T11:03:59
#
#-------------------------------------------------

QT       += core gui webenginewidgets webchannel network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = VNote
TEMPLATE = app


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
    veditor.cpp \
    vnotefile.cpp \
    vdocument.cpp \
    utils/vutils.cpp \
    vpreviewpage.cpp \
    utils/peg-highlight/pmh_parser.c \
    hgmarkdownhighlighter.cpp \
    vstyleparser.cpp \
    utils/peg-highlight/pmh_styleparser.c \
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
    veditwindow.cpp

HEADERS  += vmainwindow.h \
    vdirectorytree.h \
    vnote.h \
    vnotebook.h \
    dialog/vnewdirdialog.h \
    vconfigmanager.h \
    vfilelist.h \
    dialog/vnewfiledialog.h \
    vedit.h \
    veditor.h \
    vconstants.h \
    vnotefile.h \
    vdocument.h \
    utils/vutils.h \
    vpreviewpage.h \
    utils/peg-highlight/pmh_parser.h \
    hgmarkdownhighlighter.h \
    utils/peg-highlight/pmh_definitions.h \
    vstyleparser.h \
    utils/peg-highlight/pmh_styleparser.h \
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
    veditwindow.h

RESOURCES += \
    vnote.qrc

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../hoedown/release/ -lhoedown
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../hoedown/debug/ -lhoedown
else:unix: LIBS += -L$$OUT_PWD/../hoedown/ -lhoedown

INCLUDEPATH += $$PWD/../hoedown
DEPENDPATH += $$PWD/../hoedown
