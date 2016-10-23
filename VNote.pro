#-------------------------------------------------
#
# Project created by QtCreator 2016-10-01T11:03:59
#
#-------------------------------------------------

TEMPLATE = subdirs

CONFIG += c++11

SUBDIRS = hoedown \
    src

src.depends = hoedown
