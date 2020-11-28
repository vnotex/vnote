QT += core gui widgets

TARGET = vtitlebar

TEMPLATE = lib

# CONFIG += warn_off
CONFIG += staticlib

SOURCES += \
    src/vtitlebar.cpp \
    src/vtoolbar.cpp

HEADERS += \
    src/vtitlebar.h \
    src/vtoolbar.h
