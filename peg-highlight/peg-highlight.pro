# PEG-Markdown-Highlight
# Github: https://github.com/ali-rantakari/peg-markdown-highlight

QT -= core gui

TARGET = peg-highlight

TEMPLATE = lib

CONFIG += warn_off
CONFIG += staticlib

SOURCES += pmh_parser.c \
    pmh_styleparser.c

HEADERS += pmh_parser.h \
    pmh_styleparser.h \
    pmh_definitions.h
