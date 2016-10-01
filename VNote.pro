#-------------------------------------------------
#
# Project created by QtCreator 2016-10-01T11:03:59
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = VNote
TEMPLATE = app


SOURCES += main.cpp\
        vmainwindow.cpp \
    vdirectorytree.cpp \
    vnote.cpp \
    vnotebook.cpp

HEADERS  += vmainwindow.h \
    vdirectorytree.h \
    vnote.h \
    vnotebook.h

RESOURCES += \
    vnote.qrc
