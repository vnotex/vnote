#-------------------------------------------------
#
# Project created by QtCreator 2016-10-01T11:03:59
#
#-------------------------------------------------

QT       += core gui webenginewidgets webchannel

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
    vtabwidget.cpp \
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
    dialog/vnewnotebookdialog.cpp

HEADERS  += vmainwindow.h \
    vdirectorytree.h \
    vnote.h \
    vnotebook.h \
    dialog/vnewdirdialog.h \
    vconfigmanager.h \
    vfilelist.h \
    dialog/vnewfiledialog.h \
    vtabwidget.h \
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
    dialog/vnewnotebookdialog.h

RESOURCES += \
    vnote.qrc
