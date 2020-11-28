TEMPLATE = subdirs

CONFIG += c++11

SUBDIRS = \
    libs \
    src \
    tests

src.depends = libs
tests.depends = libs
